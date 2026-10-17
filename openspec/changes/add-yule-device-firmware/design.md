# Design

## Context

- **Greenfield repository.** It contains only OpenSpec scaffolding, a Visual Studio `.gitignore` and a licence. All code in this change is new: firmware, server and broker setup.
- **Hardware per decoration:**
  - an ESP32 dev board (the classic ESP32-WROOM DevKit is the baseline)
  - a 5 V WS2812B-style addressable LED strip
  - a passive piezo buzzer
  - a momentary push button to ground

  Prototyping happens on a breadboard powered over USB, so total current is limited to roughly 500 mA.
- **Shared infrastructure.** Decorations sit in different homes and in several overlapping family groups, including friends' families, all sharing one Mosquitto broker on `williamb.net`. The owner runs it with their existing Docker Compose setup, on a non-standard public port, next to the new C# server.
- **Firmware drift.** Decorations are given away and are later only reachable remotely. Several firmware versions will coexist, so every payload is versioned and tolerant. Remote updates (D13) are what let the fleet move forward afterwards.
- **Future self-service.** Families should later be able to manage their own groups. This change builds the device-side contract and the server that self-service will extend (D14).
- **Behaviour is fixed by the specs.** This document covers only *how*:
  - `specs/device-provisioning`
  - `specs/member-profile`
  - `specs/family-groups`
  - `specs/thinking-of-you-messaging`
  - `specs/melody-playback`
  - `specs/light-patterns`
  - `specs/device-controls`
  - `specs/do-not-disturb`
  - `specs/firmware-updates`

## Goals / Non-Goals

**Goals:**
- Keep all firmware decision logic hardware-independent and unit-tested on a PC. This covers parsing, rendering, gestures, validation, quiet hours, message, topic and config codecs, and OTA decisions.
- Keep animations smooth and the button responsive at all times, including during TLS handshakes and reconnects.
- **The server's database is the single source of truth** for devices, groups and releases. The broker's permission state is a projection of it that can always be rebuilt.
- **Devices are unaware of how the server manages things.** Swapping today's admin pages for a self-service app must not need firmware changes.
- Keep pins, LED count, power budget, cooldown, topic root, broker endpoint and release public keys in one place.

**Non-Goals:**
- A device-side way to choose which group a press goes to, or to edit membership.
- Volume control. A passive piezo driven by PWM has no useful software volume.
- Battery operation and deep sleep.
- Authentication on the device's own settings page. The home LAN is trusted, and the broker password is write-only.
- ESP32 Secure Boot and flash encryption. They burn eFuses irreversibly, which is too risky while prototyping. Authenticity of updates comes from application-level signatures instead (D13).
- More than one administrator, and user accounts. Those arrive with self-service.

## Decisions

### D1. Repository layout

```
firmware/
  platformio.ini              # envs: esp32dev (default), esp32-c3, esp32-s3, native (tests)
  partitions.csv              # nvs, otadata, app0 ~1.9 MB, app1 ~1.9 MB, coredump
  include/
    yule_config.h             # pins, LED count, power budget, cooldown, topic root, defaults
    secrets.example.h         # broker host/port, release public key(s), optional CA PEM
  lib/yule_core/              # hardware-independent C++17: no Arduino headers
    rtttl.*  melody_player.*  effects.*  compositor.*  press_message.*  topics.*
    membership_config.*  send_tracker.*  quiet_hours.*  button_gestures.*  cooldown.*
    recent_ids.*  playback_queue.*  settings_model.*  song_library.*  tz_table.*
    device_id.*  ota_manifest.*  ota_policy.*  backoff.*  rgb.h
  src/                        # ESP32-only glue and drivers
    main.cpp  app.*
    led_driver.*  buzzer_driver.*  button_driver.*  storage.*
    wifi_manager_glue.*  mqtt_link.*  time_sync.*  web_server.*  ota_client.*
  web/index.html              # settings page source (vanilla HTML/CSS/JS)
  scripts/embed_web.py        # pre-build: gzip web/ into a generated header
  scripts/sign_firmware.py    # post-build: produce signed .bin + manifest
  test/test_native/           # Unity tests for yule_core
server/
  Yule.slnx
  src/Yule.Server/            # ASP.NET Core (.NET 10)
    Data/  Broker/  Releases/  Pages/  Program.cs
  tests/Yule.Server.Tests/              # xUnit unit tests
  tests/Yule.Server.IntegrationTests/   # xUnit + Testcontainers (real Mosquitto + dynsec)
  Dockerfile
  README.md
broker/
  docker-compose.yml          # mosquitto + yule-server
  mosquitto.conf
  README.md                   # dynsec bootstrap, certs, reverse proxy, backups
README.md                     # hardware wiring, build/flash, provisioning, releases, troubleshooting
```

`secrets.h`, the generated web header, signing keys and `.pio/` are git-ignored. The existing Visual Studio `.gitignore` already covers `bin/` and `obj/`.

### D2. Firmware toolchain and partitions

Target Arduino-ESP32 core 3.x (ESP-IDF 5). Use the official `espressif32` platform if it ships core 3.x at implementation time, otherwise the community `pioarduino` platform. Pin the exact platform and library versions. Core-version-specific APIs, such as LEDC and the OTA rollback hooks, are confined to the driver files.

A custom `partitions.csv` provides two OTA app slots of about 1.9 MB each on 4 MB flash, with no SPIFFS partition. The default 1.25 MB slots are too tight for Wi-Fi, TLS, WiFiManager, the web server, FastLED and the HTTPS updater together.

The `native` env builds `yule_core` with the host compiler and ArduinoJson, and tests it with Unity.

### D3. Firmware concurrency model

`loop()` runs a non-blocking tick, roughly every 1–5 ms:
1. Sample the button and feed the gesture state machine.
2. Drain the MQTT event queue and apply each event to the app.
3. Advance the playback scheduler and the melody player.
4. Render a frame every 20 ms (50 fps) and call `FastLED.show()`.
5. Service the web server or the Wi-Fi portal.
6. Run the connection state machine.
7. Poll the OTA client's progress.

MQTT uses the **ESP-IDF `esp_mqtt` client** bundled with the Arduino core. It runs in its own FreeRTOS task and handles TLS, keep-alive, reconnect and QoS 1 PUBACKs. Its events are copied into a FreeRTOS queue, so only the main loop touches app state.

| Alternative | Rejected because |
|---|---|
| PubSubClient | Publishes only at QoS 0, so there is no broker acknowledgement for sender confirmation. Its blocking TLS connect would also freeze animations. |
| `arduino-mqtt` | Publish and connect block. |
| AsyncMqttClient | TLS support on ESP32 is unreliable. |

The OTA download and flash write run in a separate FreeRTOS task. That task reports progress through an atomic counter, and the loop keeps rendering the progress indication. WiFiManager runs in non-blocking portal mode, and the settings web server never runs at the same time as the portal.

### D4. Wire protocol

**Topics** live under root `yule/v1`, set with the build-time `YULE_TOPIC_ROOT`:

| Topic | Delivery | Written by |
|---|---|---|
| `yule/v1/groups/<groupId>/press/<deviceId>` | QoS 1, not retained | the sending device |
| `yule/v1/devices/<deviceId>/config` | QoS 1, retained | the server only |
| `yule/v1/devices/<deviceId>/status` | QoS 1, retained (online, plus the offline Last Will) | the device |
| `yule/v1/firmware/<channel>/<board>` | QoS 1, retained | the server only |

**Payloads.** Each carries `"v":1`, and readers ignore unknown fields.

```json
// press
{"v":1,"id":"9f3c2a17b04e6d58","from":"d-3c71bf4a2b10","name":"Gran","ts":1797000000,
 "song":"Jingle:d=8,o=5,b=112:e,e,4e,e,e,4e,e,g,c.,16d,2e",
 "pattern":{"effect":"twinkle","colors":["#C8102E","#00843D"],"speed":2}}
// device config (server → device)
{"v":1,"groups":[{"id":"g-7f3k2abc","name":"Brander Grandparents"},
                 {"id":"g-q7m4x2td","name":"Smith Grandparents"}],"channel":"stable"}
// status (device → server)
{"v":1,"state":"online","fw":"0.3.0","board":"esp32dev","name":"Gran","ota":"ok"}
// firmware manifest (server → devices)
{"v":1,"version":"0.3.0","board":"esp32dev","size":1534016,
 "sha256":"<hex>","sig":"<base64 DER ECDSA P-256>","url":"https://yule.williamb.net/firmware/yule-0.3.0-esp32dev.bin"}
```

**Sending.** The device publishes one copy per current group, all sharing the message id (64 bits from `esp_random`, written as hex). `send_tracker` waits for a PUBACK for every copy. "Sent" means all copies were acknowledged within 5 s; any missing acknowledgement, or having no groups, means "failed".

**Receive pipeline.** The message is dropped, with a logged reason, if any of these checks fails:
1. The topic parses as `groups/<g>/press/<sender>`.
2. `<g>` is in the current membership.
3. The payload parses and `from` equals `<sender>`.
4. `from` is not the device's own id.
5. The `id` is not in the ring of the last 32 ids. This also collapses copies that arrive through several groups.
6. It is not stale: more than 300 s old when both times are known. A negative age counts as fresh.

A message that passes is normalised (fallback effect, colours and song) and enqueued, with a queue capacity of 5.

**Membership handling.** On connect, the device subscribes to its own `config` topic and to `firmware/<channel>/<board>`. When a config message arrives, the device:
1. Validates it.
2. Saves it to NVS.
3. Works out the difference in groups, and subscribes or unsubscribes `groups/<g>/press/+` accordingly.
4. Re-subscribes the manifest topic if the channel changed.

The saved config is applied at boot, so the device works before the retained message arrives.

**Why the song and pattern travel inside every press:** receivers keep no state about other members, a profile change applies to the very next press, and there is no race between a profile update and a press.

**Blip and reboot tolerance.** The device connects with `clean_session=false` and client id = device id, so the broker queues QoS 1 presses during short outages and OTA reboots. The 5-minute staleness rule drops anything old. The device waits for SNTP sync, or 15 s after Wi-Fi comes up, before connecting, so that staleness can be judged.

### D5. Identity and broker security (Mosquitto dynamic security)

- **Device id** = `d-` + the 48-bit factory base MAC (`ESP.getEfuseMac()`) as 12 lowercase hex digits. The suffix used for the AP and mDNS names is its last 6 hex digits.
- **Broker:** Mosquitto 2.0.x (pinned `eclipse-mosquitto` image) with the bundled **dynamic-security plugin**, whose state lives in `dynamic-security.json` on a data volume. It has two listeners:
  - **Public:** the TLS listener on the chosen non-standard port, using the `williamb.net` Let's Encrypt certificate.
  - **Internal:** plain listener 1883, bound to the Compose network and never published to the host.
- **Default ACL access is deny for everything.** All permissions are created by the server:
  - **Per device:** a dynsec **client** (username = device id; a random 24-character password generated by the server) with exactly one **role** `device-<id>`. The role allows:
    - publish to `yule/v1/devices/<id>/status`
    - subscribe to `yule/v1/devices/<id>/config` and `yule/v1/firmware/<channel>/+`
    - for each group the device belongs to: publish to `yule/v1/groups/<g>/press/<id>` and subscribe to `yule/v1/groups/<g>/press/+`
  - **Server service account** `yule-server`, allowed to:
    - use `$CONTROL/dynamic-security/#`
    - publish retained to `yule/v1/devices/+/config` and `yule/v1/firmware/#`
    - subscribe to `yule/v1/devices/+/status`

    It connects only over the internal listener.
- **Why a role per device rather than a dynsec group per family group:** "publish only as yourself" needs a topic containing the device's own id. A shared group role cannot express that without per-client substitution, which this design does not rely on. The server generates every role, so having one per device costs nothing.
- **Revocation.** Mosquitto checks ACLs for each message it delivers, and the server also disconnects affected clients when their permissions shrink. This is covered by integration tests (task group 11).

**Alternatives considered:**

| Alternative | Rejected because |
|---|---|
| mosquitto-go-auth calling the server over HTTP | Third-party plugin and a custom image, and devices could not connect while the server is down. |
| Server rewriting password/ACL files and sending SIGHUP | Needs shared volumes and container signalling. |
| Static pattern ACLs | Cannot express group membership. |

- **Firmware verification:** the firmware checks the broker certificate using the ESP-IDF certificate bundle, with the hostname checked. `secrets.h` can optionally pin a specific CA PEM instead.
- **Credentials on the device** live in the NVS `ident` namespace, and the device's settings API never returns the password.

### D6. Device persistence (NVS via `Preferences`)

| Namespace | Contents |
|---|---|
| `profile` | JSON blob plus `schema` int: display name, song, pattern, brightness, mute, quiet hours, time zone. Missing fields get defaults. |
| `ident` | Broker password; optional broker host/port override. |
| `membership` | Last received device-config JSON (groups and channel). |
| `ota` | Provisional flag, boot-attempt counter, the last 4 rolled-back versions, last update result. |

Wi-Fi credentials are left to the ESP-IDF Wi-Fi stack. A very long press erases only those. `secrets.h` supplies only build-wide values (broker endpoint and release public keys), so one build serves every device.

### D7. Device settings page and HTTP API

- **Server:** the Arduino core's built-in `WebServer`.
- **Page:** a single vanilla HTML/CSS/JS file with no external resources, gzipped into a header at build time.
- **Endpoints:**

  | Endpoint | Purpose |
  |---|---|
  | `GET /` | The page |
  | `GET /api/status` | Device id, groups, SSID, broker state, time-synced flag, firmware version, update status, setup-required flag |
  | `GET /api/library` | Built-in songs, effects and the colours each uses, time-zone list |
  | `GET /api/settings` | Current settings |
  | `POST /api/settings` | Validated with core validators; returns `{ok, errors, warnings}` |
  | `POST /api/preview` | Plays locally; broadcasts nothing |
  | `POST /api/device` | Broker password and optional host/port override; triggers a reconnect |

- **Time zones:** a curated table of about 40 IANA names mapped to POSIX rules, plus a custom rule field. The page pre-selects the browser's zone using `Intl`.

### D8. Rendering: effects plus a two-layer compositor

- **Effects** sit behind `reset(seed)` and `render(tMs, Rgb* px, n)`. They work in normalised strip coordinates, map speed to period multipliers of ×2, ×1 and ×0.5, and use a seedable PRNG.
- **Compositor:**
  - The **base layer** is the playback pattern, or a status indication such as setup, connecting, lost-connection or the update progress bar (a dim white fill along the strip).
  - The **overlay layer** holds a transient cue, and the base timeline keeps running underneath it.
- **Output stage:** brightness scaling, then `FastLED.setMaxPowerInVoltsAndMilliamps(5, YULE_LED_MAX_MA)` (default 450 mA), then RMT output. NeoPixelBus is the fallback driver.
- **Cue vocabulary** (exact timings tuned on hardware):

  | Cue | Visual | Sound |
  |---|---|---|
  | sent | 2 green pulses | rising two-note tone |
  | failed | 3 red blinks | low buzz |
  | please wait | 1 dim amber pulse | silent |
  | long-press threshold | steady blue while held | none |
  | very-long-press threshold | purple chase | none |
  | muted | blue fade-down | silent |
  | unmuted | blue fade-up | chirp |

### D9. Sound: RTTTL parser and LEDC square wave

- **Parser:** `rtttl` parses into `{freqHz, durMs}` notes using an equal-temperament table for octaves 3–8. It is accepting in what it tolerates and reports the 1-based position of the first error.
- **Player:** `melody_player` is a pure state machine with an articulation gap and a 30 s cutoff.
- **Driver:** `buzzer_driver` uses LEDC at 50% duty.
- **Sound permission** is re-evaluated every tick: mute OR quiet hours, unless the playback is a preview.
- **Song library:** about ten public-domain carols: Jingle Bells, We Wish You a Merry Christmas, Deck the Halls, Silent Night, O Christmas Tree, Joy to the World, Hark! The Herald Angels Sing, God Rest Ye Merry Gentlemen, The First Noel, and Up on the Housetop.

### D10. Button gestures

`button_gestures` is a pure state machine driven by `(nowMs, rawLevel)`:
- 25 ms debounce.
- Emits `LongThresholdReached` at 2 s and `VeryLongThresholdReached` at 10 s; the very-long-press action fires at this point.
- On release, emits `ShortPress` (under 1 s), nothing (1–2 s) or `LongPress` (2–10 s).

The input is a GPIO with an internal pull-up.

### D11. Default pins and configuration (classic ESP32 DevKit)

| Setting | Default |
|---|---|
| LED data | GPIO 13 |
| Buzzer | GPIO 25 |
| Button | GPIO 27 |
| LED count | 12 |
| Colour order | GRB |
| Power budget | 450 mA |
| Cooldown | 30 s |
| Board id | `esp32dev` |

Strapping pins are avoided. The C3 and S3 envs override the pins and the board id (`esp32-c3`, `esp32-s3`). The README recommends a level shifter, a 330 Ω data resistor and a 1000 µF capacitor.

### D12. Observability for prototyping

- **Serial log** at 115200 baud: connection-state transitions, membership changes, message ids sent and received, drop reasons (stale, duplicate, self, forged, not-a-member, malformed, queue full), validation errors and OTA steps.
- **Server Status page:** live online/offline state, firmware version, board and last OTA result for each device, read from the status topics.
- **Test device:** register a "virtual" device id such as `d-000000000001`, labelled "Tester", on the server and add it to a group. `mosquitto_pub` with its credentials can then inject presses for testing.

### D13. Remote firmware updates (OTA)

**Signing (build PC).** `scripts/sign_firmware.py` runs after the build. It:
1. Hashes the image (SHA-256).
2. Signs the digest with an **ECDSA P-256** private key, using the `cryptography` package or the `openssl` CLI.
3. Writes `yule-<version>-<board>.bin` and `.manifest.json`.

The key path comes from an environment variable and never touches the repo. The firmware embeds **up to two** release public keys from `secrets.h`, so that the signing key can be rotated: ship firmware trusting old and new keys, then sign with the new one.

**Device side** (`ota_policy` in core, `ota_client` in src):
1. Receive the retained manifest for its channel and board.
2. `ota_policy` decides whether to install. It installs only if: the board matches, the semver is newer than the running version, the version is not in the rolled-back list, and the app is idle.
3. The download task:
   - fetches the image over HTTPS (certificate bundle)
   - streams it into the inactive slot with `Update`, hashing as it goes
   - verifies the digest and signature with mbedTLS
   - switches the boot partition and reboots

   Any failure aborts the update, leaves the slot inactive, and is recorded as the `ota` status result.

**Rollback.**
- After an OTA boot, the firmware is provisional. The Arduino core's `verifyRollbackLater()` hook stops automatic validation, and `ota_policy` tracks boot attempts in NVS.
- Healthy means 60 s of uptime and, if Wi-Fi is up, a broker connection within 10 minutes. The firmware then calls `esp_ota_mark_app_valid_cancel_rollback()`.
- After 3 unhealthy boots, or 10 minutes of Wi-Fi without the broker, the device:
  1. sets the previous slot as the boot partition
  2. records the version as rolled back
  3. reboots

  The ESP-IDF bootloader rollback is also used where the platform's bootloader enables it.
- Owners can always re-flash over USB.

**Server side:** see D15, Releases.

### D14. Path to self-service

The device contract does not need to change for self-service:
- server-written `config` and manifest topics
- hardware ids
- opaque group ids with names
- tolerant, versioned payloads

A follow-up change extends the server with:
- accounts
- device claiming (for example, a claim code shown on the device page)
- group invites
- per-group admins

Today's "Register device → type password on device" step becomes device self-registration delivered by an OTA release (D13). Broker management (D5) and the projection and reconciler (D15) are reused unchanged.

### D15. Yule server (ASP.NET Core, .NET 10)

- **Stack:** ASP.NET Core Razor Pages, EF Core with SQLite (database file on a volume), MQTTnet, and xUnit with Testcontainers.
- **Deployment:** a multi-stage Docker image running as a non-root user. It joins the Compose network and sits behind the owner's existing reverse proxy for TLS, at a host such as `yule.williamb.net`.
- **Configuration** comes from environment variables and secrets, and the server fails fast if any of these are missing:
  - the admin password hash
  - broker internal address and service-account credentials
  - the public base URL
  - the release public keys
  - the data directory
- **Data model:**
  - `Device`: id, label, channel, encrypted broker password, created
  - `Group`: `g-` + 8 random base32 characters, and a name
  - `Membership`
  - `FirmwareRelease`: version, board, sha256, size, signature, file, channels, uploaded
  - `DeviceStatus`: state, firmware version, board, OTA result, last seen

  Broker passwords are stored **encrypted with ASP.NET Data Protection**, with the key ring persisted to the data volume. This lets the reconciler fully rebuild device logins if the broker's dynsec state is lost (spec `family-groups`).
- **Broker sync** (`Broker/`):
  - A pure **projection** turns the database state into the desired dynsec clients, roles and ACLs, plus the retained config payloads. It is unit-tested.
  - A **dynsec client** sends `$CONTROL/dynamic-security/v1` commands over MQTTnet, matching each reply to its request.
  - An **applier** applies changes as soon as they happen. A **reconciler** runs at startup and every 10 minutes: it lists the broker's state, works out the differences from the projection, and applies them idempotently.
  - After any membership change, the server re-publishes the affected retained `config` messages.
  - A **status listener** subscribes to `devices/+/status` and updates `DeviceStatus`.
- **Admin pages** (`Pages/`):
  - A single admin login (cookie auth with anti-forgery, and login throttling). Every page except `/login`, `/firmware/*` and `/healthz` requires it.
  - **Devices:** register by id (validated `^d-[0-9a-f]{12}$`) and label. The generated password is shown once. Edit label and channel; remove with confirmation. Shows the device's groups and live status.
  - **Groups:** create, rename, delete; add and remove members.
  - **Releases:** upload a `.bin` and manifest. The server verifies the board, digest and signature against the configured public keys, stores the file, and publishes to `beta`; a release can then be promoted to `stable`. The current release per channel and board is shown.
  - **Status:** a fleet overview.
- **Public endpoints:** `GET /firmware/{file}` (immutable, content-addressed file names) and `GET /healthz`.
- **Alternatives considered:**
  - A REST API with an OpenAPI UI. Rejected in favour of pages, for day-to-day usability.
  - A SPA. Too much for a single admin.
  - Python or a shell script as the stand-in backend. Rejected in favour of a C# service that can grow into the product backend.

## Risks / Trade-offs

- **WS2812B flicker while Wi-Fi is active, or a marginal 3.3 V data signal.** → Use FastLED's RMT driver with a pinned version, NeoPixelBus as fallback, and a level shifter as documented.
- **USB brown-outs on bright patterns.** → Firmware power budget, a brightness setting and a bulk capacitor.
- **A bad update bricks remote devices.** → Signature and digest checks, installing only when idle, rollback on crash loops or a lost broker connection, a rolled-back version is never retried, `beta` first on your own devices, and USB recovery.
- **Signing key lost or leaked.** → Keep it offline with a backup. Two trusted public keys allow rotation. A leaked key can be retired by shipping a release that trusts only the new key.
- **Broker permissions drift or the dynsec file is lost.** → The database is authoritative, and the reconciler rebuilds everything, including passwords, from encrypted storage. Back up the server and Mosquitto data volumes.
- **Server outage.** → The broker keeps enforcing the last permissions, and devices are unaffected. Only admin changes and releases pause.
- **A publicly exposed broker and admin pages.** → TLS only, deny-by-default dynsec, a long random password per device, a non-standard port, admin login with throttling, and the server's MQTT access internal-only. Optionally restrict `/` (but not `/firmware`) at the reverse proxy.
- **Persistent sessions consume broker memory.** → Set Mosquitto's `persistent_client_expiration` (for example `2h`) and `max_queued_messages 20`.
- **The clock is unsynchronised after a reboot.** → Delay the broker connection for up to 15 s for SNTP. The rare noise just after a night-time power cut is accepted.
- **Removing someone from a group doesn't take effect immediately.** → Per-message ACL checks plus the server disconnecting affected clients, verified by integration tests.
- **TLS heap use** (about 40–50 KB). → MQTT and the OTA download are the only TLS sessions, and they fit on all target boards.

## Migration Plan

This is greenfield, so there is no data migration. Rollout:
1. On `williamb.net`:
   - Bring up `broker/docker-compose.yml` (Mosquitto plus the server).
   - Initialise dynsec (`mosquitto_ctrl dynsec init`) and create the `yule-server` service account and role, as documented.
   - Set the server's secrets and point the reverse proxy at the server.
   - Open the MQTT TLS port.
2. On the build PC:
   - Generate the release signing key pair, using the documented `openssl` commands.
   - Put the public key in `secrets.h` and in the server configuration.
3. Flash each device over USB with the same build. For each device:
   1. Read the device id from its settings page.
   2. Register it on the server's Devices page.
   3. Enter the password shown there on the device.
   4. Add the device to its groups on the Groups page.
4. Give the devices away. Owners re-run Wi-Fi setup at home with a very long press.
5. Release later updates through the Releases page: `beta` on your own devices first, then promote to `stable`.

**Rollback:**
- Device updates roll back automatically.
- Any device can be re-flashed over USB.
- The server can be redeployed from its image plus the data volumes, and the reconciler restores the broker's state.

## Open Questions

- The exact public MQTT port, and whether the server lives at `yule.williamb.net` or under a path on `williamb.net`. Both are configuration only.
- The final list of built-in carols, and the cue colours and timings. These will be tuned on real hardware.
- Whether the first enclosures need pins other than the defaults in D11. This is configuration only.
- The detailed self-service design (accounts, claiming, invites), which is a follow-up change building on D14.
