# Spec Delta

## Purpose

Plays a family member's chosen tune on the decoration's passive buzzer. Tunes are described in the widely used RTTTL ring-tone format, so that existing carols can be pasted in directly.

## ADDED Requirements

### Requirement: RTTTL format support
The device SHALL play melodies written in RTTTL: `name:defaults:notes`.
- **Defaults section:** optionally sets duration `d`, octave `o` and tempo `b`. Any value not given SHALL default to `d=4`, `o=6`, `b=63`.
- **Notes:** each note SHALL support an optional duration (1, 2, 4, 8, 16, 32), a pitch (`c d e f g a b`, optionally sharpened with `#`) or a pause `p`, an optional dot that extends the duration by half, and an optional octave from 4 to 7.
- **Tolerance:** whitespace and upper-case letters SHALL be accepted. A dot placed either before or after the octave SHALL be accepted.

#### Scenario: Standard RTTTL tune
- **WHEN** the device is asked to play `Jingle:d=8,o=5,b=112:e,e,4e,e,e,4e,e,g,c.,16d,2e`
- **THEN** it plays the notes at the correct pitches and relative durations for a tempo of 112 beats per minute

#### Scenario: Defaults omitted
- **WHEN** a tune has an empty defaults section, as in `Tune::c,e,g`
- **THEN** each note plays as a quarter note in octave 6 at 63 beats per minute

#### Scenario: Invalid tune detected
- **WHEN** a tune contains an invalid note such as `h` or a duration of `3`
- **THEN** the tune is reported as invalid, with the position of the first problem

### Requirement: Non-blocking playback
Melody playback SHALL NOT block the device. LED animations SHALL continue, button presses SHALL be detected, and network messages SHALL be processed while a melody plays.

#### Scenario: Button pressed during a song
- **WHEN** a received song is playing and the owner presses their button
- **THEN** the press is detected and handled while the song continues

### Requirement: Playback duration limit
A melody SHALL stop after 30 seconds of playing time, even if notes remain.

#### Scenario: Overlong tune truncated
- **WHEN** a received tune would take 50 seconds to play
- **THEN** the buzzer stops after 30 seconds

### Requirement: Immediate silencing
Playback SHALL stop sounding immediately when the device becomes muted, and SHALL produce no sound at all while do-not-disturb is in effect (see `do-not-disturb`). The rest of the visual playback timing SHALL be unaffected.

#### Scenario: Mute during a song
- **WHEN** a song is playing and the owner long-presses to mute
- **THEN** the buzzer goes silent within 100 ms, while the light pattern continues for its normal duration

### Requirement: Built-in feedback tones
The device SHALL provide short, distinct tones for "sent", "send failed", "muted off" and the default chime used for missing or invalid songs. These tones SHALL follow the same do-not-disturb rules as songs.

#### Scenario: Sent tone in quiet hours
- **WHEN** a press is sent successfully during quiet hours
- **THEN** the "sent" tone is not played; only the visual confirmation is shown
