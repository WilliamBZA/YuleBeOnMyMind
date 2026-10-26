# Tasks

## 1. Firmware scaffolding

- [x] 1.1 Create the `firmware/` PlatformIO project with envs `esp32dev` (default), `esp32-c3`, `esp32-s3` and `native`. Pin the platform (Arduino-ESP32 core 3.x, per design D2) and the FastLED, WiFiManager and ArduinoJson library versions. Create an empty `lib/yule_core/` and a placeholder Unity test. Verify that `pio run -e esp32dev` builds and `pio test -e native` runs the placeholder test and passes.
- [x] 1.2 Add `include/yule_config.h` holding the D11 defaults (pins, LED count, colour order, power budget, cooldown, topic root, board id per env, timing constants) and `include/secrets.example.h`:
  - broker host `williamb.net` and a port placeholder
  - one or two release public keys (PEM)
  - an optional CA PEM

  Make the build fail with a clear `#error` when `secrets.h` is missing. Verify the build fails without `secrets.h` and succeeds after copying the example.
- [x] 1.3 Add `partitions.csv` (nvs, otadata, two app slots of about 1.9 MB, coredump) and select it for every ESP32 env. Verify that `pio run` reports the custom partition table and that the image fits in one app slot.
- [x] 1.4 Add ignores to `.gitignore`: `.pio/`, `firmware/include/secrets.h`, the generated web header, and `*.pem`/`*.key` signing keys. Verify that `git status` lists none of them after a build.
- [ ] 1.5 Write the first `README.md` sections: parts list, wiring table for the default pins (level shifter, 330 Ω data resistor, 1000 µF capacitor, button to GND, passive buzzer), and firmware build/flash/test commands. Verify that each documented command runs as written.

## 2. Core: RTTTL parsing and melody playback (`melody-playback`)

- [x] 2.1 Implement the `rtttl` parser:
  - defaults `d=4,o=6,b=63` and partial defaults
  - durations 1–32, sharps, pauses, both dot positions, octaves 3–8
  - whitespace and case tolerance
  - 1-based error position for invalid input

  Verify with native tests for the spec scenarios: the Jingle example, empty defaults, and an invalid `h` note or duration `3`.
- [x] 2.2 Implement the `melody_player` state machine. Given `nowMs`, it returns the frequency to sound, with an articulation gap and a hard 30 s cutoff. It also reports total and clipped durations. Verify with native tests for note timing at a known tempo, a 50 s tune stopping at 30 s, and an empty melody.
- [x] 2.3 Add `song_library` with at least eight public-domain carols (per design D9), the feedback tones and the default chime. Verify with a native test that every entry parses, every carol is 30 s or shorter, and there are at least eight carols.

## 3. Core: light effects and compositor (`light-patterns`)

- [x] 3.1 Add the `Rgb` type, `#RRGGBB` parsing/formatting, a warm-white default and a seedable PRNG. Verify with native tests for valid and invalid colours and for PRNG determinism.
- [x] 3.2 Implement the eight effects behind one interface, using normalised coordinates, speed multipliers and colour repetition. Verify with native tests that:
  - every effect renders at n = 1, 8, 60 and 300 without out-of-bounds writes
  - `candy-cane` alternates colours 1 and 2 and scrolls
  - `solid` is identical at all speeds
  - `rainbow` ignores the colours
  - `chase` with one colour uses only that colour
  - fast `breathe` completes more cycles than slow
- [x] 3.3 Implement the compositor: a base layer, an overlay cue layer with expiry (the base timeline keeps advancing underneath), the 1 s fade-out and 5–100% brightness scaling. Verify with native tests for overlay replacement and resumption, the fade reaching black at about 1 s, and brightness leaving the hue unchanged.
- [x] 3.4 Define the D8 cue animations and the status indications: setup breathing, connecting pulse, lost-connection single-LED blink, update progress fill, idle off. Verify with native tests for each cue's duration, that the lost-connection indication lights exactly one LED, and that the progress fill is proportional to the percentage.

## 4. Core: topics, messages and membership (`thinking-of-you-messaging`, `family-groups`)

- [x] 4.1 Implement `press_message` encode/decode. Fields: `v`, `id`, `from` (device id), `name`, optional `ts`, `song`, `pattern`. Apply the spec's normalisation fallbacks and ignore unknown fields. Verify with native tests for:
  - round-trip encode/decode
  - a payload from newer firmware (extra fields, unknown effect)
  - garbage and truncated JSON rejected without crashing
  - a missing `from` rejected
- [x] 4.2 Implement `topics`: builders and parsers for the D4 topics (`groups/<g>/press/<d>`, `devices/<d>/config|status`, `firmware/<channel>/<board>`) under the configurable root. Then implement the receive filters in D4 order: topic parses, group is in the current membership, `from` matches the topic sender, not own, not in the 32-id ring, not stale. Each filter returns a drop reason. Verify with native tests for:
  - duplicate, forged, own, not-a-member, 8-hour-old, 30-second-old and unknown-time presses
  - the same message id arriving through two groups being accepted once
- [x] 4.3 Implement `playback_queue` (capacity 5) and session timing (song clipped at 30 s, lights for at least 10 s, 1 s fade, 1 s gap). Verify with native tests for the 4-second-song case, back-to-back simultaneous presses, and a sixth queued press being dropped.
- [x] 4.4 Implement the status payload (`state`, `fw`, `board`, `name`, `ota`) and the offline LWT payload. Verify with a native test of the exact JSON produced.
- [x] 4.5 Implement `membership_config`: decode and validate the device-config payload (group ids `^g-[a-z0-9]{8}$`, names 1–40 characters, channel `stable|beta`, unknown fields ignored). Also work out the subscribe/unsubscribe differences between the old and new group sets, and whether the manifest topic changed. Verify with native tests for adding a group, removing a group, a channel change, an empty group list, and invalid entries rejected while the previous membership is kept.
- [x] 4.6 Implement `send_tracker` for sending to every group: one pending acknowledgement per group copy, success only when all are acknowledged within 5 s, failure on a timeout or with no groups. Verify with native tests for all acknowledged, one missing, a late acknowledgement after the timeout being ignored, and no groups.

## 5. Core: button gestures, cooldown and do-not-disturb (`device-controls`, `do-not-disturb`)

- [x] 5.1 Implement `button_gestures` (25 ms debounce, 2 s and 10 s thresholds, release classification). Verify with native tests for a tap, a 1.5 s hold, 20 ms of bounce, a 3 s hold, and a 12 s hold (the very long press fires at 10 s and nothing fires on release).
- [x] 5.2 Implement `cooldown`: it starts only after a successful send; please-wait during the window; configurable, default 30 s. Verify with native tests for ten presses in 5 s, a retry after a failure, and long presses being unaffected.
- [x] 5.3 Implement `quiet_hours` and the sound-permission rule: windows that cross midnight, an exclusive end, start == end meaning never quiet, an unsynchronised clock meaning not quiet, and a preview override. Verify with native tests for 23:15 and 07:00 against 21:30–07:00, start == end, disabled, unsynchronised, and a preview while muted.
- [x] 5.4 Implement the `backoff` helper (exponential with jitter, capped at 60 s). Verify with a native test that delays grow and never exceed 60 s.

## 6. Core: identity, settings model and validation (`device-provisioning`, `member-profile`)

- [x] 6.1 Implement `device_id`: turn a 48-bit MAC into `d-<12 lowercase hex>` and a 6-hex-digit suffix. Verify with native tests using known MAC values, including leading zeros.
- [x] 6.2 Implement `settings_model`: profile fields and defaults (display name "Family member", first carol, breathe in warm white, 50% brightness), `ident` (password, broker override), and JSON with a `schema` version that fills in defaults for missing fields. Verify with native tests for the defaults, a round-trip, and loading an older blob.
- [x] 6.3 Implement the validators, each returning per-field errors and warnings:
  - display name: 1–24 characters after trimming
  - custom RTTTL: 512 characters or fewer and parses, with the error position; a warning when it plays longer than 30 s
  - effect, 1–3 colours, speed 1–3
  - brightness 5–100
  - quiet-hours times
  - non-empty time-zone rule
  - broker host and port override

  Verify with native tests covering each valid and invalid case in the `member-profile` spec.
- [x] 6.4 Add the curated time-zone table (about 40 IANA names mapped to POSIX rules). Verify with a native test that names are unique and every rule is non-empty.

## 7. Core: update decisions (`firmware-updates`)

- [x] 7.1 Implement `ota_manifest` decode and validation (`v`, `version`, `board`, `size`, `sha256`, `sig`, `url` as HTTPS). Verify with native tests for a valid manifest, a missing field, a non-HTTPS URL, and unknown fields ignored.
- [x] 7.2 Implement the `ota_policy` install decision: board match, strictly newer semver, not rolled back, idle. Verify with native tests for an equal version, an older version, a newer version, the wrong board, a rolled-back version, a newer version after a rollback, and not idle.
- [x] 7.3 Implement the `ota_policy` rollback state machine:
  - provisional on an OTA boot, with boot-attempt counting
  - healthy after 60 s of uptime plus a broker connection within 10 minutes of Wi-Fi being up
  - revert after 3 unhealthy boots, or 10 minutes of Wi-Fi without the broker
  - keeps a rolled-back list of 4 entries

  Verify with native tests for a crash loop, a broker failure, Wi-Fi being unavailable (no broker-based revert), and the healthy path.

## 8. ESP32 drivers and hardware bring-up

- [ ] 8.1 Implement `led_driver` on FastLED: configurable pin, count and colour order; power budget; frames from the compositor at 50 fps. Verify on the breadboard that a test sweep lights every LED in order, and that 100% white stays within the budget on a USB power meter.
- [ ] 8.2 Implement `buzzer_driver` (LEDC square wave with silence) driven from `melody_player`. Verify on hardware that a library carol plays recognisably and stops cleanly.
- [ ] 8.3 Implement `button_driver` (GPIO with internal pull-up) feeding `button_gestures`, with gestures logged to serial. Verify on hardware that a tap, a 3 s hold and a 12 s hold log the expected events.
- [ ] 8.4 Implement `storage` on NVS with the `profile`, `ident`, `membership` and `ota` namespaces, plus a Wi-Fi-only credential erase. Read the device id from the eFuse MAC and log it at boot. Verify on hardware that:
  - the same device id is logged across a re-flash
  - settings survive a power cycle
  - the Wi-Fi erase leaves the other namespaces intact
- [ ] 8.5 Add a `YULE_BRINGUP` build flag with no networking: a short press plays the owner's own song and pattern locally, and a long press toggles mute. Document bring-up in the README. Verify on the breadboard that the default carol plays with the breathe pattern smoothly, and that muting mid-song silences at once while the lights continue.

## 9. Broker (Mosquitto with dynamic security)

- [x] 9.1 Create `broker/docker-compose.yml` (a pinned `eclipse-mosquitto` 2.0.x image plus a `yule-server` service placeholder on a shared internal network) and `broker/mosquitto.conf`:
  - a TLS listener on a configurable non-standard port, using the `williamb.net` certificate
  - plain listener 1883 on the internal network only
  - the dynamic-security plugin with its config file on the data volume
  - `persistence true`, `persistent_client_expiration 2h`, `max_queued_messages 20`, `max_packet_size 4096`

  Verify that `docker compose config` validates and Mosquitto starts with the dynsec plugin loaded and no config errors.
- [x] 9.2 Write `broker/README.md` covering:
  - `mosquitto_ctrl dynsec init`
  - setting default ACL access to deny
  - creating the `yule-server` service account and role with the D5 permissions
  - mounting certificates and reloading them on renewal
  - the firewall port
  - backing up the data volume

  Verify by running the documented commands on a fresh deployment: anonymous and unknown users are refused on the TLS port, port 1883 is unreachable from the host, and `yule-server` can run `mosquitto_ctrl dynsec listClients` over the internal listener.

## 10. Server: scaffolding, configuration and data (`family-groups`)

- [x] 10.1 Create `server/Yule.slnx` with `src/Yule.Server` (ASP.NET Core .NET 10 Razor Pages), `tests/Yule.Server.Tests` (xUnit) and `tests/Yule.Server.IntegrationTests` (xUnit + Testcontainers). Add a multi-stage, non-root `Dockerfile` and a `/healthz` endpoint, and wire the image into `broker/docker-compose.yml`. Verify that `dotnet build` and `dotnet test` pass, and that after `docker compose up` the `/healthz` endpoint returns 200.
- [x] 10.2 Add typed configuration with startup validation (admin password hash, broker internal address and credentials, public base URL, release public keys, data directory), with Data Protection keys persisted to the data directory. Verify with a unit test that missing values fail validation, and by observing a clear startup error when one is removed.
- [x] 10.3 Add the EF Core SQLite model (`Device`, `Group`, `Membership`, `FirmwareRelease`, `DeviceStatus`) with an initial migration applied at startup. Add id generation (`g-` + 8 base32 characters), device-id validation and broker-password generation (24 random characters), with the password stored encrypted via Data Protection. Verify with unit tests for the generators and validators, password encryption round-trip, and the migration applying to an empty database.

## 11. Server: broker permission sync (`family-groups`)

- [x] 11.1 Implement the pure projection from database state to the desired dynsec clients, roles and ACLs (per design D5) and the retained device-config payloads. Verify with unit tests for:
  - a device in no groups
  - a device in two groups
  - a channel change
  - a removed device (absent from the projection)
  - the exact ACL topic list for one device
- [x] 11.2 Implement the dynsec client over MQTTnet: send `$CONTROL/dynamic-security/v1` commands, match replies to requests, and surface errors. Implement idempotent create-or-modify for clients and roles. Verify with integration tests against a Testcontainers Mosquitto with dynsec: creating a client lets it connect, and re-applying is a no-op.
- [x] 11.3 Implement the change-driven applier, the reconciler (at startup and every 10 minutes), retained config publishing after membership changes, and disconnecting a client when its permissions shrink. Verify with integration tests that:
  - A device in G1 cannot subscribe to or publish into G2.
  - A device in G1 and G2 receives both.
  - Removing membership stops delivery within about 10 seconds, even with a persistent session.
  - A removed device is disconnected and refused.
  - Deleting a role or the whole dynsec state by hand is restored by the reconciler with the same passwords.
  - With the server stopped, devices keep exchanging presses.
- [x] 11.4 Implement the status listener on `devices/+/status`, updating `DeviceStatus`. Verify with an integration test that online, offline (LWT) and firmware-version changes are recorded.

## 12. Server: admin pages (`family-groups`)

- [x] 12.1 Implement the single-admin login: cookie auth, anti-forgery, login throttling, and every page except `/login`, `/firmware/*` and `/healthz` requiring login. Verify with tests that unauthenticated requests redirect to login, a wrong password is rejected, and repeated failures are throttled.
- [x] 12.2 Implement the Devices page:
  - register by device id and label, showing the generated password exactly once
  - edit label and channel
  - remove with confirmation
  - show each device's groups and live status

  Verify with page tests, and manually that a registered device can connect with the shown password.
- [x] 12.3 Implement the Groups page: create, rename and delete groups; add and remove members. Verify with an integration test that a membership change made through the page reaches the broker and the device's retained config within about 10 seconds.
- [x] 12.4 Implement the Status page: per-device state, firmware version, board, last OTA result and last seen. Verify manually against two running devices or test clients.
- [x] 12.5 Write `server/README.md`: running locally, configuration and secrets, generating the admin password hash, deploying behind the reverse proxy, and backups. Verify by deploying from the README onto the Compose stack.

## 13. Firmware networking: Wi-Fi, time and broker link (`device-provisioning`, `family-groups`)

- [ ] 13.1 Implement the Wi-Fi glue on WiFiManager in non-blocking mode:
  - portal only when no credentials are saved, with AP name `YuleBeOnMyMind-<suffix>`
  - background retry with back-off
  - very long press opens the portal, which times out after 10 minutes
  - setup-mode indication
  - the settings server never runs while the portal is active

  Verify on hardware: a fresh device shows the AP and blue breathing; it joins and saves credentials; with the router off it does not open the portal and later reconnects; a 12 s hold opens the portal, which closes after 10 minutes.
- [ ] 13.2 Advertise mDNS `yule-<suffix>.local`. Verify by browsing to it from a phone on the same network.
- [ ] 13.3 Implement `time_sync` (`configTzTime` with the profile's rule, a synced flag) and delay the broker connection until the clock syncs or 15 s have passed. Verify in the serial log that the clock syncs, local time matches the chosen zone, and the connection waits for sync.
- [ ] 13.4 Implement `mqtt_link` on `esp_mqtt`:
  - TLS with the certificate bundle and hostname verification (optional pinned PEM)
  - username and client id = device id
  - `clean_session=false`, 2 KB buffer, retained offline LWT
  - an event queue that `loop()` drains
  - on connect, subscribe to its own `config` and its channel/board manifest, and publish online status
  - no connection without a password

  Verify that the server's Status page shows the device online, and offline after unplugging. A hostname or IP the broker certificate does not cover is refused, and a device without a password never connects.
- [ ] 13.5 Apply membership: use the saved membership at boot, then apply each config message (save, subscribe/unsubscribe group press topics, switch the manifest topic on a channel change). Verify on hardware that:
  - adding the device to a group on the server makes the serial log show a new subscription within about 10 seconds, without a restart
  - removing it unsubscribes
  - after a reboot with the broker unreachable, the settings page still shows the last-known groups
- [ ] 13.6 Document device provisioning in the README: flashing, Wi-Fi via the portal (including the `192.168.4.1` fallback), reading the device id, registering it on the server, entering the password, and assigning groups. Verify by provisioning one device using only the README.

## 14. App: sending, receiving and controls (`thinking-of-you-messaging`, `device-controls`, `do-not-disturb`)

- [ ] 14.1 Wire short press to send:
  - fail immediately if disconnected or in no groups
  - otherwise publish one copy per group at QoS 1 with a shared id, tracked by `send_tracker`
  - show the sent or failed cue and tone (respecting do-not-disturb)
  - start the cooldown only on success, with please-wait during cooldown

  Verify, while subscribed as the server service account over the internal listener, that a device in two groups publishes exactly two copies with the same id per accepted press. The failed cue appears at once with Wi-Fi off or no groups, and repeated presses within 30 s publish nothing.
- [ ] 14.2 Wire receiving: run the decode and filters into the queue, then playback, with brightness and sound permission applied each tick. The sender cue overlays any ongoing playback. Log every drop reason. Verify with the registered test device and `mosquitto_pub` that:
  - a valid press plays
  - the same `id` published twice plays once
  - the same `id` published to two shared groups plays once
  - a `ts` 10 minutes old is dropped as stale
  - a mismatched `from` is dropped as forged
  - garbage is dropped
  - two presses published together play back-to-back
- [ ] 14.3 Wire long press to toggle mute (persisted, with cues and the muted-off tone), and apply quiet hours from the profile. Verify on hardware that muting mid-song silences within 100 ms while the lights continue, that mute survives a power cycle, and that quiet hours covering the current time give lights only.
- [ ] 14.4 Wire the status indications into the compositor base layer (connecting, lost connection after 60 s, idle off). Verify by stopping the Mosquitto container: after about 60 s the red blink appears; after restarting it, the device reconnects within about 60 s, the LEDs go off, and animations did not freeze.
- [ ] 14.5 Document troubleshooting in the README: serial log meanings, drop reasons, and injecting test presses with the test device's credentials. Verify that the documented `mosquitto_pub` command triggers playback on a device in the same group.

## 15. Device settings page and API (`member-profile`, `device-provisioning`)

- [ ] 15.1 Add `scripts/embed_web.py` as a pre-build script that gzips `web/index.html` into a generated header. Verify that editing the HTML and rebuilding changes the served page, with no filesystem upload.
- [ ] 15.2 Implement the D7 endpoints using the core validators. The password is write-only, and saving device settings triggers a reconnect. Verify with documented `curl` commands that:
  - invalid input returns field errors and leaves stored values unchanged
  - a valid save applies to the next press without a reboot
  - preview publishes nothing
  - `GET` responses never contain the password
  - `/api/status` lists the device id and its group names
- [ ] 15.3 Build `web/index.html` in vanilla HTML/CSS/JS with no external resources. It contains:
  - a status panel: device id, group names or "not in any family group yet", "setup required", time not synchronised, firmware version and update status
  - display name
  - song picker plus a custom RTTTL box with error and 30 s warnings
  - effect, colours and speed
  - brightness, mute, quiet hours, and time zone (`Intl` pre-select plus a custom rule)
  - a Preview button
  - a "Device setup" section (password, broker override)

  Verify on a phone with the internet disconnected that every field loads, saves and survives a power cycle, and that errors show the device's messages.
- [ ] 15.4 Add a settings-page section to the README for owners (`yule-<suffix>.local`, what each setting does). Verify that a non-developer can change their song and pattern by following it.

## 16. Firmware updates end to end (`firmware-updates`)

- [x] 16.1 Add `scripts/sign_firmware.py` as a post-build step: it reads the version and board, hashes the image (SHA-256), signs it with ECDSA P-256 using a key from an environment variable, and writes `yule-<version>-<board>.bin` plus `.manifest.json`. Document key-pair generation with `openssl`. Verify that the script's output passes the server verifier's unit test fixture, and that a one-byte-modified image fails it.
- [x] 16.2 Implement the server's Releases page and `/firmware/{file}`:
  - upload a `.bin` and manifest
  - verify the board, size, digest and signature against the configured keys
  - store the file, then publish the retained `firmware/beta/<board>` manifest with a URL from the public base URL
  - promote to `stable`

  Verify with unit and integration tests that tampered or unsigned uploads are rejected and nothing is published, that valid uploads publish the retained manifest, that promotion publishes to `stable`, and that the file downloads without login.
- [ ] 16.3 Implement `ota_client`: download in its own task over HTTPS (certificate bundle), stream into the inactive slot while hashing, verify the digest and signature with mbedTLS against the embedded keys, switch the boot partition and reboot. Install only when `ota_policy` says so, show the progress indication, and report the result in status and on the settings page. Verify on hardware that:
  - a `beta` release installs on a `beta` device only
  - a tampered `.bin` served in place of the real one is rejected and the device keeps running
  - an older version is ignored
  - an update arriving mid-song waits for the playback to end
  - a press sent during the update reboot plays after restart
- [ ] 16.4 Implement rollback: the `verifyRollbackLater()` hook, provisional state and boot attempts in NVS, health marking, and reverting to the previous slot with the version recorded as rolled back. Verify with deliberately broken `beta` builds that:
  - a build that crashes at boot reverts after 3 boots
  - a build with a wrong broker port reverts after 10 minutes on Wi-Fi
  - the rolled-back version is not retried
  - a newer good release then installs
- [ ] 16.5 Document the release workflow in the README: version bump, build, sign, upload, `beta` on test devices, promote to `stable`, plus key backup and rotation using the second public key. Verify by performing one release purely from the README.

## 17. End-to-end integration

- [x] 17.1 Verify that `pio test -e native` and `dotnet test` (unit and integration) pass, and that `pio run -e esp32dev -e esp32-c3 -e esp32-s3` builds all variants.
- [ ] 17.2 Set up three physical devices plus the test device on the deployed stack: A in G1, B in G1 and G2, C in G2. Walk through every spec scenario that can only be checked on hardware, and record the results in the README test checklist:
  - A's press plays on B but not C
  - C's press plays on B but not A
  - B's press plays once each on A and C
  - simultaneous presses queue
  - cooldown
  - reply during playback
  - quiet hours in two time zones
  - a power cut keeps settings and groups
  - a router change via very long press
  - live join and leave from the Groups page
  - with the server stopped, presses still flow
  - the broker restarts and devices recover within about 60 s
  - a 30 s Wi-Fi blip still delivers a press
  - a device off for over 5 minutes does not replay old presses
  - a `beta` → `stable` release across all three devices, with one forced rollback on a `beta` device
