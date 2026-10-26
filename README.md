# Yule Be On My Mind

A small, tactile way to say "I'm thinking of you". Each 3D-printed Christmas decoration has an ESP32, an LED
strip, a buzzer and a button. Press the button and the decorations of everyone who shares a **family group**
with you play your chosen song and light pattern.

| Folder | What it is |
|---|---|
| `firmware/` | PlatformIO + Arduino (C++) firmware for the decoration, with a host-tested core library |
| `server/` | The Yule server (ASP.NET Core, .NET 10): devices, family groups, firmware releases, admin pages |
| `broker/` | Docker Compose deployment of the Mosquitto MQTT broker (with dynamic security) and the server |
| `openspec/` | Specs, design and task list for the project |

This README covers the hardware and the firmware. Server and broker notes are in `server/README.md` and
`broker/README.md`.

## Parts list (per decoration)

| Part | Notes |
|---|---|
| ESP32 development board | Classic ESP32 DevKit (ESP32-WROOM, 4 MB flash) is the default. ESP32-C3 and ESP32-S3 boards are supported by build configuration. |
| WS2812B-style LED strip, 5 V | Any length from 1 to 300 LEDs. 12 LEDs is the default. |
| Passive piezo buzzer | A *passive* buzzer (it needs a square wave), not an active one that beeps when powered. |
| Momentary push button | Normally open. |
| Level shifter, 3.3 V to 5 V | 74AHCT125 or 74HCT245. Strongly recommended for the LED data line. |
| 330 ohm resistor | In series with the LED data line, close to the first LED. |
| 1000 uF capacitor, 6.3 V or more | Across the strip's 5 V and GND where the strip is connected. |
| Breadboard, jumper wires, USB cable | USB power limits the whole decoration to about 500 mA. |

## Wiring (default pins, classic ESP32 DevKit)

| Signal | ESP32 pin | Connect to |
|---|---|---|
| LED data | GPIO 13 | Level shifter input. The shifter's output goes through the 330 ohm resistor to the strip's DIN. |
| Buzzer | GPIO 25 | Buzzer `+`. Buzzer `-` goes to GND. |
| Button | GPIO 27 | One side of the button. The other side goes to GND. The firmware turns on the internal pull-up, so no external resistor is needed. |
| 5 V | VIN (USB 5 V) | Strip 5 V and the level shifter's high-side supply. |
| GND | GND | Strip GND, level shifter GND, buzzer, button and capacitor `-`. All grounds must be common. |

The 1000 uF capacitor goes across the strip's 5 V and GND, with the right polarity (the stripe marks `-`).

Other boards and pins are set in `firmware/platformio.ini` (per board) or `firmware/include/yule_config.h`:

| Board env | LED | Buzzer | Button |
|---|---|---|---|
| `esp32dev` (default) | 13 | 25 | 27 |
| `esp32-c3` | 4 | 5 | 6 |
| `esp32-s3` | 5 | 6 | 7 |

The LED count (`YULE_LED_COUNT`), colour order (`YULE_LED_COLOR_ORDER`), power budget (`YULE_LED_MAX_MA`) and
press cooldown (`YULE_COOLDOWN_MS`) are set the same way. The power budget defaults to 450 mA, which keeps a
breadboard on USB within its limit. The firmware dims the LEDs automatically to stay inside it.

## Firmware: build, flash and test

You need [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/) (`pip install platformio`,
or the PlatformIO extension for VS Code). The host-side tests also need a C++17 compiler on your PATH (GCC or
Clang; on Windows, MinGW-w64).

```sh
cd firmware

# One-time setup: deployment settings (broker port, release public key). This file is git-ignored.
cp include/secrets.example.h include/secrets.h
#   then edit include/secrets.h

# Run the host-side unit tests for the hardware-independent core. No board needed.
pio test -e native

# Build the firmware for the default board (classic ESP32).
pio run

# Other boards
pio run -e esp32-c3
pio run -e esp32-s3

# The breadboard bring-up build: no networking (see "Hardware bring-up").
pio run -e bringup

# Flash a connected board over USB, then watch the serial log (115200 baud).
pio run -t upload
pio device monitor
```

The first build downloads the ESP32 toolchain and libraries, which takes a while. Library and platform versions
are pinned in `firmware/platformio.ini`. The project uses the community
[pioarduino](https://github.com/pioarduino/platform-espressif32) platform because the official PlatformIO ESP32
platform still ships Arduino-ESP32 2.x and this firmware targets 3.x. On Windows the toolchain download fails
unless long paths are enabled (the `LongPathsEnabled` registry setting); building inside a Linux container with
PlatformIO installed avoids that.

The build fails with a clear message if `include/secrets.h` is missing. Every build also turns
`firmware/web/index.html` (the settings page) into a generated header, so changing the page only needs a rebuild
and flash, no filesystem upload. With `YULE_SIGNING_KEY` set, each build is also signed for release (see
"Releasing firmware"); without it the build prints `sign_firmware: not signing` and carries on.

Flash partitions: the firmware uses a custom table (`firmware/partitions.csv`) with two application slots of
about 1.9 MB each so that over-the-air updates can write to the inactive slot, and no filesystem partition.
`pio run` prints the usable slot size on the `Flash:` line (1966080 bytes).

## Hardware bring-up

Before any networking, check the LEDs, buzzer and button on the breadboard with the bring-up build. It has no
Wi-Fi, broker or settings page: a short press plays your own song and light pattern on the decoration itself, and a
long press toggles mute.

```sh
cd firmware
pio run -e bringup -t upload
pio device monitor
```

| Check | What to do | What you should see |
|---|---|---|
| Every LED works, in order | Power up, or type `sweep` in the serial monitor | Each LED lights dimly in turn from the data input end, then all go off. If the colours are wrong, change `YULE_LED_COLOR_ORDER`; if the count is wrong, `YULE_LED_COUNT`. |
| The power budget holds | Plug in through a USB power meter and type `white` | Every LED full white at 100% for 10 seconds, with the meter at or below about 450 mA plus the board's own draw (roughly 100 mA). The firmware dims the strip as needed to stay within `YULE_LED_MAX_MA`. |
| Song and lights | Tap the button | The default carol plays and the strip breathes in warm white, smoothly, for at least 10 seconds, then fades out. |
| Mute takes effect at once | Tap, then hold the button for 2 seconds during the song and let go | The strip turns blue while you hold, then fades down; the song stops at once and the lights carry on. Hold again to unmute: a blue fade-up and a short chirp. |
| Gestures | Tap; hold 3 s; hold 12 s | The log shows `short press` then `released`; `held 2 s`, `long press`, `released`; `held 2 s`, `held 10 s` (as the 10 seconds pass), `released`, with no `long press` for the 12-second hold. |
| Settings survive power cuts | Mute, unplug, plug back in | The boot log says `muted`. |
| The id stays the same | Note the `device id` line, re-flash, look again | The same `d-...` id. |

The buzzer must be a passive one. If it only clicks or stays silent, check the wiring to GPIO 25 and that it is not
an active buzzer (those beep on their own when powered).

## Provisioning a device

Each decoration is set up once on the bench before it is given away. You need the server running with an
administrator login (`server/README.md`) and the broker reachable on the port in `secrets.h`.

1. **Flash it.** Connect the board over USB, then `cd firmware` and `pio run -t upload`. Open the serial log with
   `pio device monitor` (115200 baud). The LEDs sweep once, and the log shows the device id:

   ```
   [  0.412] [boot] device id d-3c71bf4a2b10
   ```

   The id comes from the chip's factory MAC address. It is the same after every re-flash, update and Wi-Fi reset.
   Note it down; the last six digits (`4a2b10` here) are in the device's Wi-Fi setup name and its `.local` name.

2. **Join it to Wi-Fi.** A device with no saved network opens its own Wi-Fi network called
   `YuleBeOnMyMind-<last six digits>` and breathes blue. On a phone or laptop, join that network. A setup page opens
   by itself; if it does not, browse to `http://192.168.4.1`. Choose the home network, type its password and save.
   The blue breathing stops and the log shows the device joining:

   ```
   [ 41.870] [wifi] connected to 'HomeNetwork', IP 192.168.1.57, -52 dBm
   [ 42.020] [wifi] reachable as yule-4a2b10.local (192.168.1.57)
   ```

   If the network password is wrong, the setup network stays open and the page lets you try again. A device that
   already has a network never opens the setup network by itself, even when the router is off: it keeps retrying
   (waiting longer each time, up to a minute) and joins as soon as the router is back.

3. **Open its settings page.** From a phone or computer on the same network, browse to
   `http://yule-<last six digits>.local` (for example `http://yule-4a2b10.local`), or to the IP address in the log
   if `.local` names do not work on your network (some Android phones). The **Status** box shows the device id
   and "Setup required".

4. **Register it on the server.** On the server's **Devices** page, enter the device id and a label such as
   "Gran (Brander)". The page shows the device's broker password **once**. Copy it.

5. **Give the device its password.** On the device's settings page, under **Device setup**, paste the password
   and press **Save device setup**. Leave the broker host and port empty: the device then uses the ones built into
   the firmware. The password is saved on the device and is never shown again by the page, the API or the log.
   (Without a network, typing `password PASTE-THE-PASSWORD-HERE` in the serial monitor does the same.) The device
   then connects:

   ```
   [ 43.100] [mqtt] connecting to williamb.net:18883 over TLS (181304 bytes of heap free)
   [ 44.350] [mqtt] connected to williamb.net:18883 as d-3c71bf4a2b10 (broker did not have our session)
   [ 44.360] [mqtt] announced online (update result: none)
   ```

   The settings page shows **Connection: Connected**, and the server's **Status** page shows the device online.
   Unplug it and, after about 45 seconds, the Status page shows it offline.

   Without a password the device never connects to the broker; the log says
   `broker connection: no broker password set (setup required)`. A broker whose certificate does not match its
   name is refused before the password is sent (`TLS: the broker's certificate was rejected`).

6. **Add it to its groups.** On the **Groups** page, add the device to each family group it belongs to. Within
   about ten seconds the log shows the new subscription, with no restart, and the settings page lists the group:

   ```
   [ 52.640] [member] now in 1 group(s): Brander Grandparents; channel stable
   [ 52.650] [member] subscribe yule/v1/groups/g-7f3k2abc/press/+
   ```

   Removing it from a group shows an `unsubscribe` line. The device remembers its groups across power cuts, so it
   knows them before the broker answers.

7. **Try it.** Press the button: the strip pulses green twice (sent) and the other devices in its groups play its
   song. Then hand it over: the owner joins it to their own Wi-Fi with a 10-second hold (below) and picks their
   song and lights on the settings page ("The settings page").

### Changing the Wi-Fi network later

Hold the button for 10 seconds. The device forgets its Wi-Fi network and opens `YuleBeOnMyMind-<last six digits>`
again, breathing blue; everything else (song, pattern, password, groups) is kept. Pick the new network as in step 2.
If nobody finishes the setup within 10 minutes the setup network closes and the device goes back to the network it
had, so an accidental long hold does no harm.

### Serial commands

Type these in the serial monitor (115200 baud) and press Enter:

| Command | What it does |
|---|---|
| `password <value>` | Saves the broker password for this device and connects. |
| `status` | Prints the device id, firmware, Wi-Fi, clock, broker state, groups, sound, playback and update state. |
| `white` | Lights every LED full white at 100% brightness for 10 seconds, for checking the power draw. |
| `sweep` | Lights each LED in turn, first to last. |
| `help` | Lists the commands. |

The serial log is also where to check the clock: once the device has Wi-Fi it logs
`clock synchronised: Sat 2026-10-17 15:42:10 NZDT`, in the time zone saved in its profile (UTC until the owner
chooses one). The broker connection waits for the clock for up to 15 seconds after Wi-Fi comes up.

## Using the decoration

This part is for the person who has the decoration at home.

**The button**

| Do this | And this happens |
|---|---|
| Tap it | Everyone who shares a family group with you sees your lights and hears your song. Your decoration pulses **green twice** when it has gone out, or blinks **red three times** if it could not be sent (no internet, or not in a group yet). |
| Tap again within 30 seconds of a successful send | Nothing is sent; one dim **amber** pulse means "please wait". This keeps anyone from flooding everyone else. |
| Hold it for 2 seconds, then let go | Mute on or off. The strip turns **blue** while you hold, then fades down (muted, no sound) or fades up with a chirp (sound back on). |
| Hold it for 10 seconds | Wi-Fi setup, for a new router or a new home. The strip shows a **purple** chase; let go and join the `YuleBeOnMyMind-...` network as described in "Changing the Wi-Fi network later". |

**The lights when nobody is pressing anything**

| Lights | Meaning |
|---|---|
| All off | Connected and waiting. |
| Slow blue breathing | Wi-Fi setup is open. |
| Slow amber pulse, just after switching on | Connecting. |
| One dim red light blinking every 5 seconds | It has not been able to reach the family for over a minute. It keeps trying by itself. |
| A dim white bar filling up | A firmware update is installing. It restarts by itself when done. |

When someone presses their button, your decoration plays their song (at most 30 seconds) and shows their lights
for at least 10 seconds, then fades out. If two people press at once, it plays one, pauses, then the other.

## The settings page

Each decoration has its own settings page on your home network. On a phone or computer connected to the same
Wi-Fi, open a browser and go to

    http://yule-XXXXXX.local

where `XXXXXX` is the last six characters of the decoration's id, written on its label (for example
`http://yule-4a2b10.local`). If that does not open (some Android phones do not support `.local` names), use the
decoration's address from your router's list of connected devices, such as `http://192.168.1.57`. The page works
without internet access.

Change what you like, then press **Save**. Saved settings are kept through power cuts and take effect from your
next press; saving does not send anything to anyone. **Preview on this decoration** plays the song and lights
currently chosen on the page, saved or not, on your own decoration only, with sound even when muted.

| Setting | What it does |
|---|---|
| **Display name** | Your name as the family sees it, 1 to 24 characters. |
| **Song** | The tune your family's decorations play when you press. Pick a carol, or choose **Custom tune** and paste a ring-tone in RTTTL format (at most 512 characters; if it is longer than 30 seconds the page warns you, and only the first 30 seconds play). If the tune has a mistake the page says where. |
| **Effect** | How your lights move: solid, breathe, twinkle, chase, rainbow, candy cane, comet or sparkle. The line under it says which colours it uses. |
| **Colours** | One to three colours for the effect. |
| **Speed** | Slow, medium or fast. |
| **Brightness on this decoration** | How bright your own decoration is, 5% to 100%, for everything it shows. It does not change how bright your lights look on anyone else's decoration. |
| **Muted** | No sound at all; presses still show their lights. The same as holding the button for 2 seconds. |
| **Quiet hours** | A daily time window with no sound, for example 21:30 until 07:00. Lights still show. Setting the same time for both means no quiet hours. |
| **Time zone** | Where you are, so quiet hours follow your local time, including daylight saving. The page suggests your phone's zone the first time. **Custom rule** takes a POSIX time-zone rule for places not in the list. |
| **Device setup** | For whoever set up the decoration: the broker password from the Yule server (never shown again once saved) and, rarely, a different broker address. |

The **Status** box at the top shows the decoration's id, the family groups it belongs to (or "Not in any family group
yet"; groups are changed by whoever runs the Yule server, not here), the Wi-Fi network, whether it is connected,
the clock ("Time not synchronised yet" just after switching on), whether sound is on, and the firmware version and
update status: up to date, downloading with a percentage, installed, failed with the reason, or rolled back.

## Releasing firmware

Firmware updates are installed over the air on every decoration that follows the release's channel. Images are
signed; decorations and the server accept only images signed with a key whose public half they trust.

### Once: the signing key

```sh
openssl ecparam -name prime256v1 -genkey -noout -out yule-release.pem   # the private key
openssl ec -in yule-release.pem -pubout -out yule-release.pub.pem        # the public key
```

- Keep `yule-release.pem` **outside the repository** (for example in `~/keys/`), readable only by you, with a backup
  on separate offline storage. Losing it means devices can only be updated over USB; leaking it lets someone else
  sign firmware. (`*.pem` and `*.key` are git-ignored as a safety net.)
- Put the public key in `firmware/include/secrets.h` as `YULE_RELEASE_PUBKEY_1`. This prints the lines to paste
  (delete the `\` at the end of the last line):

  ```sh
  sed -e 's/\r$//' -e 's/.*/  "&\\n" \\/' yule-release.pub.pem
  ```

- Give the server the same public key as `Yule__ReleasePublicKeys__0` (`server/README.md`, "Release keys").

Decorations trust the keys built into the firmware they run, so flash them over USB with a build that already has
the real key. A decoration whose firmware has no usable key logs
`no usable release public key in secrets.h: firmware updates cannot be installed`.

### Each release

1. **Bump the version.** In `firmware/platformio.ini`, raise `fw_version` under `[common]` (for example `0.3.0` to
   `0.3.1`). Decorations only install a version newer than the one they run.
2. **Build and sign** every board in use, with the private key's path in `YULE_SIGNING_KEY`:

   ```sh
   cd firmware
   YULE_SIGNING_KEY=~/keys/yule-release.pem pio run -e esp32dev -e esp32-c3 -e esp32-s3
   # PowerShell: $env:YULE_SIGNING_KEY = "$HOME\keys\yule-release.pem"; pio run -e esp32dev
   ```

   Each build writes `firmware/dist/yule-<version>-<board>.bin` and `.manifest.json` (git-ignored). An image built
   earlier can also be signed by hand:
   `python scripts/sign_firmware.py --image .pio/build/esp32dev/firmware.bin --version 0.3.1 --board esp32dev --key ~/keys/yule-release.pem`.
   The script uses Python's `cryptography` package when present (PlatformIO's own Python has it), otherwise the
   `openssl` tool.
3. **Upload** the `.bin` and its `.manifest.json` on the server's **Releases** page. The server checks the board,
   size, digest and signature, and refuses the upload with the reason if anything is wrong. A good upload becomes
   the current **beta** release for its board at once.
4. **Try it on beta devices.** Put your own test decorations on the `beta` channel (Devices page, Edit). Within a
   minute of being idle, each shows the dim white progress bar, restarts, and reports the new version on the
   **Status** page. After about a minute of healthy running its update result becomes `ok`.
5. **Promote to stable** on the Releases page once the beta devices have run it for a while. Every stable decoration
   then updates the same way, each when it is idle (never in the middle of playing a press).

If a release misbehaves, decorations go back to their previous firmware by themselves: when it restarts 3 times
before it has proved healthy (a minute of running and, with Wi-Fi, a broker connection), or when it has had Wi-Fi
for 10 minutes without reaching the broker. They report `rolled-back:<version>` on the
Status page and never install that version again. Fix the problem, bump the version, and release again. Any
decoration can always be re-flashed over USB.

### Changing the signing key

Two public keys can be trusted at once, so the key can be replaced without stranding any decoration:

1. Generate a new key pair as above.
2. Add the new public key as `YULE_RELEASE_PUBKEY_2` in `secrets.h` and as `Yule__ReleasePublicKeys__1` on the
   server.
3. Release a new version signed with the **old** key, and wait until every decoration runs it (Status page). They
   now trust both keys.
4. Sign later releases with the new key. Once every decoration runs one of those, move the new key to
   `YULE_RELEASE_PUBKEY_1`, empty `YULE_RELEASE_PUBKEY_2`, and remove the old key from the server.

If the old key has leaked, do the same quickly, and in step 3 release firmware that trusts only the new key.

## Troubleshooting

### Reading the serial log

Connect over USB and run `pio device monitor` (115200 baud). Each line is `[seconds since start] [area] message`:

| Area | What it reports |
|---|---|
| `boot` | Firmware version, device id, owner, saved groups, mute, setup required. |
| `wifi` | Joining, losing and re-joining Wi-Fi (with the reason), the setup portal, the `.local` name. |
| `time` | The clock synchronising, in the owner's time zone. |
| `mqtt` | Broker connection: why it is not connecting yet, TLS or login problems, reconnecting with back-off, announcing online. |
| `member` | Group changes from the server and the subscriptions they cause. |
| `button` | Every gesture: `short press`, `held 2 s`, `long press`, `held 10 s`, `released`. |
| `send` | Each press sent: its id, how many groups, and whether the broker accepted every copy within 5 seconds. |
| `recv` | Each press received: who from and through which group, playing or queued, or why it was dropped. |
| `play` | What is playing: whose press or a preview, the effect, how long the song and lights last. |
| `sound` | Mute on and off. |
| `status` | Status indication changes (connecting, connection lost, idle, setup, updating). |
| `web` | The settings page starting, settings saved or refused, previews. |
| `setup` | Broker password and broker address changes (never the password itself). |
| `ota` | Release manifests received, why one is not installed, download progress, failures, restarts and rollbacks. |

A received press that is not played is logged as `dropped (<reason>)`:

| Reason | Meaning |
|---|---|
| `bad-topic` | It arrived on a topic that is not a group press topic. |
| `not-a-member` | It came through a group this decoration is not (or no longer) in. |
| `malformed` | Not a valid press message (garbage, truncated, or missing its message id). |
| `forged` | The sender inside the message is not the device whose topic it was published on. |
| `own` | This decoration's own press coming back; only the sent/failed cue shows. |
| `duplicate` | A message id seen recently, including the second copy of a press that arrived through two shared groups. |
| `stale` | Sent more than 5 minutes before it arrived (both clocks known), for example while this decoration was switched off. |
| `queue-full` | Five presses were already waiting to play. |

Other common lines:

| Log line | What to do |
|---|---|
| `broker connection: no broker password set (setup required)` | Enter the password from the Devices page under Device setup. |
| `the broker refused the login: wrong password, or this device is not registered on the server` | Check the device is registered with this id; if the password was lost, remove and re-register the device and enter the new one. |
| `TLS: the broker's certificate was rejected ...` | The broker address does not match its certificate, or the certificate has expired. Check any broker override under Device setup. |
| `send failed: not every group was acknowledged within 5 s` | The connection is poor or the broker is struggling; try again. |
| `not sent: this device is not in any family group yet` | Add it to a group on the server's Groups page. |
| `release 0.3.1: not installing now (not-idle)` | It installs once nothing is playing and the button is not held. |
| `update to 0.3.1 failed: ...` | The reason is shown; the decoration kept its firmware and tries again in 30 minutes. |

### Injecting test presses

Register a virtual device on the server, such as `d-000000000001` labelled "Tester", and add it to the same group as
the decoration under test (`broker/README.md`, "Test device"). With its password, `mosquitto_pub` can then send
presses as that device. The client id must be the device id. Replace the group id, password, host and port with
yours; with a Let's Encrypt certificate `--capath /etc/ssl/certs` checks the broker (on Windows use `--cafile` with
a CA bundle).

```sh
B="-h williamb.net -p 18883 --capath /etc/ssl/certs -u d-000000000001 -P TESTER-PASSWORD -i d-000000000001 -q 1"
T=yule/v1/groups/g-7f3k2abc/press/d-000000000001

# A valid press: the decoration plays the tune and twinkles red and green.
mosquitto_pub $B -t $T -m '{"v":1,"id":"test0001","from":"d-000000000001","name":"Tester","song":"Test:d=8,o=5,b=140:c,e,g,4c6","pattern":{"effect":"twinkle","colors":["#C8102E","#00843D"],"speed":2}}'

# The same id again: logged as dropped (duplicate), not played.
mosquitto_pub $B -t $T -m '{"v":1,"id":"test0001","from":"d-000000000001","name":"Tester"}'

# Sent 10 minutes ago: dropped (stale).
mosquitto_pub $B -t $T -m '{"v":1,"id":"test0002","from":"d-000000000001","name":"Tester","ts":'$(( $(date +%s) - 600 ))'}'

# Claims to be someone else: dropped (forged).
mosquitto_pub $B -t $T -m '{"v":1,"id":"test0003","from":"d-aaaaaaaaaaaa","name":"Gran"}'

# Not a press at all: dropped (malformed).
mosquitto_pub $B -t $T -m 'hello'

# Two presses at once: they play one after the other, with a short pause between.
mosquitto_pub $B -t $T -m '{"v":1,"id":"test0004","from":"d-000000000001","name":"Tester"}'
mosquitto_pub $B -t $T -m '{"v":1,"id":"test0005","from":"d-000000000001","name":"Tester","pattern":{"effect":"candy-cane","colors":["#FFFFFF","#C8102E"],"speed":3}}'
```

A press without a song plays the short default chime, and one without a pattern breathes in warm white. To check
that a press arriving through two shared groups plays once, put the Tester and the decoration in two groups and
publish the same id to both group topics. Use a new id for every other test: the decoration remembers the last 32.

### The settings API from the command line

The settings page is a front end for a small JSON API on the decoration, handy for checking it with `curl`. Requests
must name the decoration (`yule-XXXXXX.local`, its name under the router's domain, or its IP address), and changes
must be sent as JSON; this keeps other web pages from changing its settings.

```sh
D=http://yule-4a2b10.local
J='Content-Type: application/json'

curl -s $D/api/status      # id, group names, Wi-Fi, broker, clock, firmware and update status
curl -s $D/api/settings    # the saved profile
curl -s $D/api/library     # songs, effects and their colours, time zones

# Invalid input is refused field by field, and nothing changes:
curl -s -X POST -H "$J" -d '{"name":"   ","song":{"kind":"custom","rtttl":"Bad:d=4,o=5,b=100:c,h"}}' $D/api/settings
#   {"ok":false,"errors":[{"field":"name",...},{"field":"song","message":"... (at character 21)."}],"warnings":[]}
curl -s $D/api/settings    # still the old values

# A valid save is used by the very next press, without a restart:
curl -s -X POST -H "$J" -d '{"name":"Gran","song":{"kind":"library","id":"silent-night"},"pattern":{"effect":"twinkle","colors":["#C8102E","#00843D"],"speed":2}}' $D/api/settings

# A preview plays here only. A mosquitto_sub on the group's press topics (as the Tester) sees nothing:
curl -s -X POST -H "$J" -d '{"song":{"kind":"library","id":"jingle-bells"},"pattern":{"effect":"rainbow","colors":["#FFFFFF"],"speed":3}}' $D/api/preview

# The broker password is accepted, then never returned (the device reconnects with it):
curl -s -X POST -H "$J" -d '{"password":"PASTE-THE-PASSWORD-HERE"}' $D/api/device
curl -s $D/api/status $D/api/settings | grep -c PASTE-THE-PASSWORD-HERE    # 0
```

## Hardware test checklist

The end-to-end checks that need real decorations (task 17.2), with three decorations A, B and C and the Tester on
the deployed stack: A in group G1, B in G1 and G2, C in G2. Record the date and result of each run.

| Check | Expected | Result |
|---|---|---|
| A presses | Plays on B, not on C | |
| C presses | Plays on B, not on A | |
| B presses | Plays once on A and once on C | |
| A and C press within a second | B plays both, one after the other | |
| A presses ten times in five seconds | One press goes out; nine "please wait" cues | |
| B presses while A's press is playing on it | B shows the sent cue; A's playback carries on | |
| Quiet hours in two time zones | Lights only inside each owner's local window | |
| Power cut | Settings and groups are the same afterwards | |
| Router change via a 10-second hold | Reconnects with song, pattern, password and groups kept | |
| Join and leave a group on the Groups page | Takes effect within about 10 s, no restart | |
| Server stopped | Presses still flow between A, B and C | |
| Broker restarted | Devices back within about 60 s; red blink while it is down for over a minute | |
| 30-second Wi-Fi blip on B while A presses | B plays the press after reconnecting | |
| B off for over 5 minutes while A presses | B does not play the old press when switched back on | |
| beta then stable release across all three, with one forced rollback on a beta device | All update; the broken build rolls back and is not retried | |
