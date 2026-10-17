# Proposal

## Why

Family members live in different homes, and a small, tactile way to say "I'm thinking of you" is warmer than a text message. Each 3D-printed Christmas decoration has an ESP32, an LED strip, a buzzer and a button. Pressing the button makes the decorations of everyone who shares a **family group** with the presser play the presser's chosen song and light pattern.

People naturally belong to more than one group. A grandchild, for example, is in both sets of grandparents' groups. Friends' families should also be able to have their own devices on the same infrastructure, without any press crossing between groups.

The longer-term aim is a product where families set up their own groups. This change creates three things so that prototyping can begin: the device firmware, a C# server, and the broker setup. It also builds the device-side foundations that self-service will need later, so decorations given away now never have to be re-flashed by hand:
- membership pushed from the server
- identity taken from the hardware
- remote, signed firmware updates

## What Changes

- **New PlatformIO + Arduino (C++) firmware project** for ESP32-based decorations. It drives a WS2812B-style addressable LED strip, a passive piezo buzzer and a momentary push button, and all pins and the LED count are configurable.
- **First-run Wi-Fi setup** through a captive portal. A very long press re-opens it later, for example after a router change.
- **Hardware-derived device identity.** Each device id (`d-` plus 12 hex digits) comes from the chip. Its broker password is entered once, during setup. People are only ever shown by their display name.
- **Secure connection.** Devices connect over TLS to a self-hosted Mosquitto MQTT broker on `williamb.net`, on a non-standard port.
- **On-device settings page**, reachable on the home network, where each owner can:
  - pick their song, from built-in public-domain carols or as a custom RTTTL tune
  - pick their light pattern, from built-in effects with colour and speed choices
  - set brightness and do-not-disturb options
  - preview their selection on the device

  The page also shows the device id, its family groups and its firmware update status.
- **Family groups.** A device belongs to zero or more groups, and membership is managed on the server and pushed to devices live.
  - A short press reaches every group the presser is in.
  - A device receives presses from all of its groups and from no others.
  - Someone who shares several groups with the presser still plays each press only once.
- **"Thinking of you" messaging.** Devices ignore their own presses, drop duplicate and stale ones, play near-simultaneous presses one after another, tolerate messages from newer firmware, and report online/offline presence.
- **Sender confirmation** shows when every group has accepted a press, and when sending failed.
- **Press cooldown** stops one person from flooding everyone else's devices.
- **Do-not-disturb:** a mute toggle (long press or settings page) and daily quiet hours in the owner's time zone. Both keep the lights but silence the sound.
- **Remote firmware updates (OTA)** with `stable` and `beta` channels.
  - Images are signed, and devices verify them before installing.
  - Updates install only while the device is idle.
  - A failing update rolls back automatically, and a rolled-back version is never retried.
- **New Yule server (ASP.NET Core, .NET 10).** It is the source of truth for devices, family groups and firmware releases.
  - It manages broker permissions through Mosquitto's dynamic-security plugin, so changes apply live and the broker keeps working if the server is down.
  - It pushes group lists and release manifests to devices, and serves firmware downloads.
  - Minimal admin web pages (Devices, Groups, Releases, Status) sit behind an admin login.
- **Broker setup:** a Docker Compose deployment of Mosquitto (dynamic security, a public TLS listener, and an internal-only listener for the server) alongside the Yule server.
- **Firmware signing script** in the firmware build. The signing key stays offline on the build PC.
- **Tests:**
  - host-side (native) unit tests for the firmware's hardware-independent logic
  - xUnit tests for the server
  - integration tests that check group isolation against a real Mosquitto broker

**Non-goals for this change:**
- Self-service accounts, device claiming and group invites. These are a follow-up change built on this server.
- Choosing which group a press goes to.
- Editing group membership on the device.
- Replaying presses missed while offline (only brief gaps are covered).
- ESP32 Secure Boot and flash encryption.
- Receiver read-receipts.
- Enclosure and 3D-model design.

## Capabilities

### New Capabilities

- `device-provisioning`: Wi-Fi onboarding via captive portal, hardware-derived device identity, broker password setup, the TLS broker connection, and the device's local network name.
- `member-profile`: The on-device settings page and persisted owner settings. Covers song choice (built-in library or custom RTTTL), light-pattern choice (effect, colours, speed), brightness, validation, preview, and the device status display.
- `family-groups`: Group membership supplied by the server and applied live. Covers sending to all of the presser's groups, isolation between groups, playing a press once despite shared groups, behaviour with no groups, and administration through the server, including resilience when the server is down.
- `thinking-of-you-messaging`: Broadcasting a press and playing other members' presses. Covers the message format and versioning, ignoring own presses, de-duplication, staleness, queueing, sender identity enforcement, and presence.
- `melody-playback`: Playing RTTTL melodies on the passive buzzer without blocking the rest of the device.
- `light-patterns`: The LED effect engine that renders a pattern (effect, colours, speed) on any strip length, within the configured brightness.
- `device-controls`: Button gestures (press, long press, very long press), press cooldown, sender confirmation feedback, and status indications.
- `do-not-disturb`: Mute toggle and daily quiet hours (time-zone aware), which suppress sound but keep lights.
- `firmware-updates`: Release publishing and channels, verified download and install, idle-only installation, progress and status reporting, automatic rollback, and never retrying a rolled-back version.

### Modified Capabilities

_None. This is the first change in the project._

## Impact

- **New code:**
  - `firmware/`: a PlatformIO project with a host-tested core library, thin ESP32 drivers, and a signing script.
  - `server/`: a .NET 10 solution with the ASP.NET Core server, unit and integration test projects, and a Dockerfile.
  - `broker/`: Compose file, Mosquitto configuration and operations notes.
- **Firmware dependencies:** FastLED, WiFiManager, ArduinoJson, the ESP-IDF MQTT client bundled with the ESP32 Arduino core, mbedTLS (hashing and ECDSA verification), and the core's HTTPS client/updater, WebServer, mDNS, Preferences (NVS) and SNTP. A custom partition table provides two app slots of about 1.9 MB each for OTA. Unity is used for native tests.
- **Server dependencies:** ASP.NET Core Razor Pages, EF Core with SQLite, and MQTTnet. Tests use xUnit and Testcontainers, which needs Docker.
- **Infrastructure:** Mosquitto (with the dynamic-security plugin) and the Yule server run in Docker Compose on `williamb.net`.
  - The MQTT TLS port is public; the server's MQTT access goes through an internal-only listener.
  - The server's HTTPS comes through the existing reverse proxy, for example `yule.williamb.net`.
  - The firmware signing key pair is generated and kept offline on the build PC.
- **Hardware assumptions:** ESP32 dev board (classic ESP32 by default; C3 and S3 by configuration), a 5 V WS2812B strip, a passive piezo buzzer and a momentary button to ground, prototyped on a breadboard over USB power.
- **Wire protocol:** the topic layout and JSON payloads (press, device config, status, firmware manifest) become a long-lived contract between devices in the field, the server, and future self-service tooling. Every payload is versioned, and receivers tolerate unknown fields.
