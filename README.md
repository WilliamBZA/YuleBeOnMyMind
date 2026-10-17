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

# Flash a connected board over USB, then watch the serial log (115200 baud).
pio run -t upload
pio device monitor
```

The first build downloads the ESP32 toolchain and libraries, which takes a while. Library and platform versions
are pinned in `firmware/platformio.ini`. The project uses the community
[pioarduino](https://github.com/pioarduino/platform-espressif32) platform because the official PlatformIO ESP32
platform still ships Arduino-ESP32 2.x and this firmware targets 3.x.

The build fails with a clear message if `include/secrets.h` is missing.

Flash partitions: the firmware uses a custom table (`firmware/partitions.csv`) with two application slots of
about 1.9 MB each so that over-the-air updates can write to the inactive slot, and no filesystem partition.
`pio run` prints the usable slot size on the `Flash:` line (1966080 bytes).

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

3. **Register it on the server.** On the server's **Devices** page, enter the device id and a label such as
   "Gran (Brander)". The page shows the device's broker password **once**. Copy it.

4. **Give the device its password.** In the serial monitor type

   ```
   password PASTE-THE-PASSWORD-HERE
   ```

   and press Enter. The password is saved on the device and never printed back. (The owner-facing settings page will
   take the password in its "Device setup" section; until then the serial command is the way.) The device then
   connects:

   ```
   [ 43.100] [mqtt] connecting to williamb.net:18883 over TLS (181304 bytes of heap free)
   [ 44.350] [mqtt] connected to williamb.net:18883 as d-3c71bf4a2b10 (broker did not have our session)
   [ 44.360] [mqtt] announced online
   ```

   The server's **Status** page now shows the device online. Unplug it and, after about 45 seconds, the
   page shows it offline.

   Without a password the device never connects to the broker; the log says
   `broker connection: no broker password set (setup required)`.

5. **Add it to its groups.** On the **Groups** page, add the device to each family group it belongs to. Within
   about ten seconds the log shows the new subscription, with no restart:

   ```
   [ 52.640] [member] now in 1 group(s): Brander Grandparents; channel stable
   [ 52.650] [member] subscribe yule/v1/groups/g-7f3k2abc/press/+
   ```

   Removing it from a group shows an `unsubscribe` line. The device remembers its groups across power cuts, so it
   knows them before the broker answers.

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
| `status` | Prints the device id, firmware, Wi-Fi, clock, broker state and groups. |
| `help` | Lists the commands. |

The serial log is also where to check the clock: once the device has Wi-Fi it logs
`clock synchronised: Sat 2026-10-17 15:42:10 NZDT`, in the time zone saved in its profile (UTC until the owner
chooses one). The broker connection waits for the clock for up to 15 seconds after Wi-Fi comes up.
