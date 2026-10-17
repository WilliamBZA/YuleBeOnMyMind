# Spec Delta

## Purpose

Lets each family member keep their decoration from making sound when that would be unwelcome (at night, across time zones, or on demand) while still showing who is thinking of them through the lights.

## ADDED Requirements

### Requirement: Mute toggle
The owner SHALL be able to turn mute on and off with a long press of the button, or from the settings page. The mute state SHALL be saved, so it survives power loss and reboots. While muted, the device SHALL make no sound. Incoming presses SHALL still be shown through their light patterns, and feedback cues SHALL still be shown visually.

#### Scenario: Muting by long press
- **WHEN** the owner long-presses while unmuted
- **THEN** the device becomes muted, shows the "muted" cue without any sound, and plays later incoming presses as lights only

#### Scenario: Unmuting by long press
- **WHEN** the owner long-presses while muted
- **THEN** the device unmutes, shows the "unmuted" cue and plays the "muted off" tone

#### Scenario: Mute survives a reboot
- **WHEN** a muted device is unplugged and plugged back in
- **THEN** it is still muted

### Requirement: Quiet hours
The owner SHALL be able to enable a daily quiet-hours window by setting a start time and an end time, in 24-hour local time to the minute. The window SHALL support crossing midnight; for example, 21:30 to 07:00. While the device's local time is inside the window, the device SHALL behave as if muted, without changing the saved mute setting. A start time equal to the end time SHALL mean no quiet hours.

#### Scenario: Night-time message
- **WHEN** quiet hours are 21:30–07:00 and a press arrives at 23:15 local time
- **THEN** the device shows the sender's light pattern without playing any sound

#### Scenario: Morning message
- **WHEN** quiet hours are 21:30–07:00 and a press arrives at 07:00 local time
- **THEN** the device plays the sender's song and shows their pattern

#### Scenario: Quiet hours disabled
- **WHEN** quiet hours are disabled, or start and end are equal
- **THEN** the time of day never suppresses sound

### Requirement: Time-zone aware local time
The owner SHALL be able to choose their time zone on the settings page, from a list of common time zones or as a custom POSIX time-zone rule. Daylight-saving changes SHALL be applied automatically. The device SHALL keep its clock synchronised over the internet. While the clock has not yet been synchronised since boot, quiet hours SHALL NOT be applied, and the settings page SHALL show that the time is not synchronised.

#### Scenario: Family member abroad
- **WHEN** an owner in London sets time zone "Europe/London" with quiet hours 22:00–07:00
- **THEN** quiet hours follow London local time, including across daylight-saving changes, whatever time zone the other family members are in

#### Scenario: Clock not yet synchronised
- **WHEN** a press arrives before the device has synchronised its clock after boot
- **THEN** quiet hours are not applied for that press, and the settings page shows the clock as not synchronised

### Requirement: Silence takes effect immediately
Becoming muted, or entering quiet hours, while a song is playing SHALL silence the buzzer straight away. The light pattern SHALL continue.

#### Scenario: Quiet hours begin mid-song
- **WHEN** a song is playing at 21:29:50 and quiet hours start at 21:30
- **THEN** the buzzer falls silent at 21:30 and the lights continue to the end of the playback
