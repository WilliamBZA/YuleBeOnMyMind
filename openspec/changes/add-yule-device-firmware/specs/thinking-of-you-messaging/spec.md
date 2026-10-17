# Spec Delta

## Purpose

Delivers a "thinking of you" press from one decoration to the decorations of everyone who shares a family group with the sender, so each receiving decoration plays the sender's own song and light pattern, reliably and without spam, duplicates or impersonation.

## ADDED Requirements

### Requirement: Broadcast on short press
A short press of the button SHALL broadcast a press when all of these hold:
- the device is connected to the broker
- it is not in cooldown
- it is a member of at least one family group (see `family-groups`)

The device SHALL publish the press once to each of its family groups, with every copy carrying the same message id. The message SHALL contain:
- the protocol version (`1`)
- a unique message id
- the sender's device id and display name
- the send time (UTC epoch seconds), or no time if the clock is not synchronised
- the sender's song as RTTTL
- the sender's light pattern (effect, colours, speed)

Each copy SHALL be published with at-least-once delivery to the broker, to that group's press topic for the sender's own device id.

#### Scenario: Member of two groups presses
- **WHEN** a device that is a member of "Brander Grandparents" and "Smith Grandparents" registers a short press
- **THEN** one press message is published to each of the two groups, both with the same message id, carrying the sender's device id, display name, song and pattern

#### Scenario: Press while disconnected
- **WHEN** the owner short-presses while the device is not connected to the broker
- **THEN** no message is stored for later sending, and the device shows the send-failed feedback (see `device-controls`)

### Requirement: Receive and play presses from group members
Every connected device SHALL receive press messages from every family group it is a member of. On receiving a valid press from another device, it SHALL play the sender's song on its buzzer and show the sender's light pattern on its LED strip. The receiving device's brightness and do-not-disturb settings SHALL apply. Playback SHALL last for the song's duration, capped at 30 seconds, with the light pattern shown for at least 10 seconds. The LEDs SHALL then fade out within about 1 second and the device SHALL return to idle.

#### Scenario: Other members' devices react
- **WHEN** Gran sends a press, and Mum and Uncle Bob share a group with her and are online
- **THEN** both of their devices play Gran's song and show Gran's light pattern

#### Scenario: Very short song
- **WHEN** a received press carries a song that lasts 4 seconds
- **THEN** the song plays once and the lights keep showing the pattern until 10 seconds have passed, then fade out

### Requirement: Ignore own presses
A device SHALL NOT play press messages that carry its own device id.

#### Scenario: Sender's device stays quiet
- **WHEN** a device receives a copy of the press it just published
- **THEN** it does not play its own song or pattern; it shows only the sender confirmation feedback

### Requirement: De-duplicate presses
A device SHALL play each message id at most once. This SHALL hold however many of its groups the press arrives through, and whether or not the broker delivers a copy more than once.

#### Scenario: Duplicate delivery
- **WHEN** the same press message (same message id) arrives twice through the same group
- **THEN** the receiving device plays it only once

#### Scenario: Press arrives through two shared groups
- **WHEN** the sender and the receiver are both members of two groups, and the receiver gets the press once through each group
- **THEN** the receiving device plays it only once

### Requirement: Discard stale presses
When both the message's send time and the receiving device's clock are known, a press sent more than 5 minutes before it is received SHALL be discarded without playing. When either time is unknown, the press SHALL be played.

#### Scenario: Brief Wi-Fi blip
- **WHEN** a device loses its connection for 30 seconds, a press is sent during that gap, and the device then reconnects
- **THEN** the device receives and plays that press after reconnecting

#### Scenario: Device offline overnight
- **WHEN** a device was switched off for 8 hours and presses were sent in that time
- **THEN** on reconnecting the device does not play those old presses

### Requirement: Queue near-simultaneous presses
If a press arrives while another is playing, the device SHALL queue it and play it after the current one finishes, with a pause of about 1 second in between. At most 5 presses SHALL be waiting; further presses received while the queue is full SHALL be discarded.

#### Scenario: Two people press at the same time
- **WHEN** Gran and Uncle Bob press their buttons within a second of each other
- **THEN** every other device that shares a group with both of them plays one of their songs and patterns in full, pauses briefly, then plays the other's

### Requirement: Tolerate malformed and newer messages
Receivers SHALL ignore unknown message fields and SHALL handle messages from newer firmware versions, as long as the fields they understand are present.
- Messages that are not valid, or that lack a sender device id, SHALL be discarded.
- An unknown effect SHALL be shown as the "breathe" effect using the supplied colours.
- Missing or invalid colours SHALL default to warm white.
- A song that is missing, invalid or longer than 512 characters SHALL be replaced by a short default chime.

A malformed message SHALL NEVER crash or hang the device.

#### Scenario: Message from newer firmware with a new effect
- **WHEN** a device receives a press naming an effect it does not know, plus extra unknown fields
- **THEN** it plays the song and shows the "breathe" effect in the supplied colours

#### Scenario: Garbage payload
- **WHEN** a device receives a payload that is not a valid press message
- **THEN** it discards the payload and carries on normally

### Requirement: Sender identity is enforced
A device SHALL only be able to publish presses under its own device id, and only into family groups it is a member of. The broker SHALL be configured to reject anything else. Receivers SHALL also discard any press whose sender device id does not match the sender id in the topic it arrived on.

#### Scenario: Impersonation attempt via topic
- **WHEN** a device tries to publish a press under another device's id
- **THEN** the broker does not deliver the message to any device

#### Scenario: Forged sender in payload
- **WHEN** a press arrives on the topic for device `d-aaaaaaaaaaaa` but its payload claims sender `d-bbbbbbbbbbbb`
- **THEN** receiving devices discard it

### Requirement: Presence reporting
On connecting, each device SHALL publish a retained "online" status under its own device id. The status SHALL include its firmware version, board type, display name and last firmware-update result. The broker SHALL publish a retained "offline" status for that device when it disconnects unexpectedly. Device status SHALL be readable by the server; other devices SHALL NOT be able to read it.

#### Scenario: Checking devices during prototyping
- **WHEN** the administrator opens the server's status page
- **THEN** it shows each registered device's online/offline state, firmware version and last update result

#### Scenario: Device unplugged
- **WHEN** a connected device loses power
- **THEN** its status changes to "offline" once the broker detects the lost connection
