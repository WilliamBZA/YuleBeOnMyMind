# Spec Delta

## Purpose

Lets each family member choose, preview and save the song and light pattern that represent them, plus their device's brightness, from a simple settings page served by their own decoration.

## ADDED Requirements

### Requirement: On-device settings page
The device SHALL serve a settings page over HTTP on the local network. The page SHALL work in current mobile and desktop browsers without internet access. It SHALL show and allow editing of:
- display name
- song
- light pattern
- LED brightness
- do-not-disturb settings (see `do-not-disturb`)

It SHALL also show read-only device status:
- device id
- the names of its family groups, or that it is not in any family group yet (see `family-groups`)
- Wi-Fi network name
- broker connection state
- whether the clock is synchronised
- firmware version and update status (see `firmware-updates`)

#### Scenario: Owner opens the settings page
- **WHEN** the owner browses to the device's address on the home network
- **THEN** the page shows the current display name, song, pattern, brightness and do-not-disturb settings, together with the device status

#### Scenario: Device in two groups
- **WHEN** the device is a member of "Brander Grandparents" and "Smith Grandparents"
- **THEN** the status section lists both group names, and offers no way to change membership

#### Scenario: Page works offline from the internet
- **WHEN** the home network has no internet access but the device is reachable locally
- **THEN** the settings page still loads and functions fully, because it needs no external resources

### Requirement: Display name
The owner SHALL be able to set a display name of 1–24 characters. Display names longer than 24 characters, or empty after trimming whitespace, SHALL be rejected with an explanatory message.

#### Scenario: Owner sets their name
- **WHEN** the owner enters "Gran" and saves
- **THEN** the display name is stored and included in every future press they send

### Requirement: Song selection
The owner SHALL be able to choose their song either from a built-in library or as a custom melody in RTTTL format. The built-in library SHALL contain at least eight public-domain Christmas melodies. Custom melodies SHALL be at most 512 characters and SHALL parse as valid RTTTL. A custom melody whose playing time exceeds 30 seconds SHALL be accepted, but the page SHALL warn the owner that it will be cut off at 30 seconds.

#### Scenario: Pick a built-in carol
- **WHEN** the owner selects "Jingle Bells" from the library and saves
- **THEN** that melody becomes their song

#### Scenario: Paste a valid custom tune
- **WHEN** the owner pastes a valid RTTTL string of 512 characters or fewer and saves
- **THEN** the device stores it as their song

#### Scenario: Invalid custom tune
- **WHEN** the owner pastes text that is not valid RTTTL, or is longer than 512 characters
- **THEN** the device rejects the save, the page explains what is wrong (for example the position of the first invalid note), and the previous song is kept

#### Scenario: Long custom tune
- **WHEN** the owner saves a valid custom tune that would play for 45 seconds
- **THEN** the device saves it and the page warns that only the first 30 seconds will play

### Requirement: Light pattern selection
The owner SHALL be able to choose a light pattern made up of:
- an effect from the built-in effect list (see `light-patterns`)
- one to three colours
- a speed of slow, medium or fast

The page SHALL show which colours an effect uses. Invalid values SHALL be rejected.

#### Scenario: Choose a twinkle pattern
- **WHEN** the owner chooses the "twinkle" effect with red and green at medium speed and saves
- **THEN** that pattern becomes their light pattern and is included in every future press they send

### Requirement: Brightness setting
The owner SHALL be able to set the maximum LED brightness for their own device, from 5% to 100%. It applies to everything the device displays: incoming messages, previews, feedback and status indications. It SHALL NOT affect how bright the owner's pattern appears on other devices.

#### Scenario: Dim a bedroom decoration
- **WHEN** the owner sets brightness to 20% and saves
- **THEN** all subsequent LED output on that device is limited to 20% of full brightness, while other family devices are unaffected

### Requirement: Preview on device
The settings page SHALL offer a preview that plays the currently selected song and light pattern on the device itself, including unsaved selections, without broadcasting anything to other devices. A preview SHALL play sound even when the device is muted or in quiet hours, because it is an explicit local request.

#### Scenario: Preview before saving
- **WHEN** the owner selects a song and pattern and taps "Preview" without saving
- **THEN** the device plays that song and shows that pattern, no message is sent to other devices, and the saved profile is unchanged

### Requirement: Settings persistence
Saved settings SHALL survive power loss and reboots, and SHALL take effect for the next button press without a restart. Saving a setting SHALL NOT itself notify other devices.

#### Scenario: Power cut
- **WHEN** the device loses power and restarts
- **THEN** the display name, song, pattern, brightness and do-not-disturb settings are the same as before the power cut

#### Scenario: Change applies to next press
- **WHEN** the owner saves a new song and then presses the button
- **THEN** the other devices play the new song

### Requirement: First-run defaults
A device with no saved profile SHALL use defaults:
- display name: "Family member"
- song: the first built-in carol
- pattern: the "breathe" effect in warm white at medium speed
- brightness: 50%

The device SHALL therefore be usable before anyone opens the settings page.

#### Scenario: Press before configuring anything
- **WHEN** a freshly provisioned device's button is pressed before its owner has opened the settings page
- **THEN** the other devices play the default song and pattern, labelled with the default display name
