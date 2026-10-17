# Spec Delta

## Purpose

Defines how the decoration's single button is used and how the decoration tells its owner what happened. Covers sending a press, muting, re-running Wi-Fi setup, cooldown, sender confirmation and connection status.

## ADDED Requirements

### Requirement: Button gestures
The device SHALL recognise these gestures by how long the button is held. Gestures SHALL be debounced so that switch bounce never produces extra presses.

| Gesture | Hold time | Action |
|---|---|---|
| Short press | less than 1 s | Send "thinking of you" (see `thinking-of-you-messaging`) |
| Long press | at least 2 s and less than 10 s | Toggle mute (see `do-not-disturb`) |
| Very long press | at least 10 s | Re-open Wi-Fi setup (see `device-provisioning`) |

A hold of between 1 and 2 seconds SHALL do nothing. Actions SHALL be carried out when the button is released. The exception is the very long press, which SHALL act as soon as the 10-second threshold is reached.

#### Scenario: Quick tap
- **WHEN** the owner presses and releases the button within 1 second
- **THEN** exactly one send is attempted

#### Scenario: Hesitant press
- **WHEN** the owner holds the button for 1.5 seconds and releases it
- **THEN** nothing is sent and mute is not toggled

#### Scenario: Bouncy switch
- **WHEN** the switch contacts bounce for 20 ms during a single tap
- **THEN** only one short press is registered

### Requirement: Hold-progress cues
While the button is held, the device SHALL show a visual cue when the long-press threshold (2 s) is reached, and a different cue when the very-long-press threshold (10 s) is reached. This lets the owner know when to let go.

#### Scenario: Owner holding to mute
- **WHEN** the owner has held the button for 2 seconds
- **THEN** the LEDs show the long-press cue, telling them that releasing now will toggle mute

### Requirement: Press cooldown
After a press is sent successfully, the device SHALL ignore further short presses for a cooldown period, 30 seconds by default (a build-time setting). A short press during cooldown SHALL send nothing and SHALL show a brief "please wait" cue. A failed send SHALL NOT start the cooldown. Long and very long presses SHALL work during cooldown.

#### Scenario: Child mashes the button
- **WHEN** the owner short-presses ten times in five seconds
- **THEN** only the first press is broadcast, and the other nine each show the "please wait" cue

#### Scenario: Retry after failure
- **WHEN** a send fails and the owner presses again straight away
- **THEN** the device attempts to send again

### Requirement: Sender confirmation feedback
After a short press, the device SHALL give clear feedback on the result:
- **Accepted:** when the broker acknowledges the press for every one of the device's family groups, a "sent" cue (visual, plus the "sent" tone subject to do-not-disturb).
- **Failed:** when the device is not connected, is not in any family group, or not every acknowledgement arrives within 5 seconds, a "send failed" cue (visual, plus the "send failed" tone subject to do-not-disturb).

The cue SHALL be shown even while an incoming press is playing, and the incoming playback SHALL continue afterwards.

#### Scenario: Successful send
- **WHEN** the owner short-presses while connected and the broker acknowledges the press for each of the device's groups within 5 seconds
- **THEN** the device shows the "sent" cue and plays the "sent" tone, unless do-not-disturb is in effect

#### Scenario: No internet
- **WHEN** the owner short-presses while the home internet is down
- **THEN** the device shows the "send failed" cue straight away, without waiting for the 5-second timeout

#### Scenario: Not in any group
- **WHEN** the owner short-presses on a device that is not a member of any family group
- **THEN** nothing is published and the device shows the "send failed" cue straight away

#### Scenario: Reply while a message is playing
- **WHEN** Gran's song is playing on Mum's device and Mum short-presses to reply
- **THEN** Mum's press is sent, her device briefly shows the "sent" cue, and Gran's playback then carries on

### Requirement: Status indications
The device SHALL show its connection state on the LED strip while it is idle:
- **Setup mode:** slow blue breathing while the Wi-Fi setup access point is open.
- **Connecting:** a slow amber pulse while connecting after boot, until it is connected to the broker or 60 seconds have passed.
- **Connection lost:** after it has been disconnected from the broker for more than 60 seconds, a single dim red LED blinking about every 5 seconds.
- **Connected:** LEDs off when connected and idle.
- **Updating:** a progress indication while a firmware update downloads and installs (see `firmware-updates`).

Except for the updating indication, status indications SHALL give way to message playback and feedback cues, and SHALL resume when those finish.

#### Scenario: Broker down for a long time
- **WHEN** the device has been unable to reach the broker for more than 60 seconds and is idle
- **THEN** one dim red LED blinks about every 5 seconds until the connection is restored

#### Scenario: Healthy idle device
- **WHEN** the device is connected to the broker and nothing is playing
- **THEN** all LEDs are off
