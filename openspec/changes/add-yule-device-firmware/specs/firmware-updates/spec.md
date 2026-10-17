# Spec Delta

## Purpose

Keeps decorations that have been given away up to date remotely and safely. Only authentic, newer firmware is installed, never during playback, and an update that misbehaves rolls itself back.

## ADDED Requirements

### Requirement: Release channels
Each device SHALL follow one update channel, `stable` or `beta`, as assigned by the server, with `stable` as the default. For each channel and board type, the server SHALL publish the current release to devices through the broker.

#### Scenario: Beta release on test devices only
- **WHEN** the administrator publishes version 0.3.0 to `beta`
- **THEN** devices on the `beta` channel update to 0.3.0, and devices on `stable` stay on their current version

#### Scenario: Promotion to stable
- **WHEN** the administrator promotes 0.3.0 from `beta` to `stable`
- **THEN** devices on the `stable` channel update to 0.3.0

### Requirement: Publishing releases
The administrator SHALL be able to upload a firmware image together with its manifest through the server's admin pages. The manifest SHALL contain the version, board type, size, SHA-256 digest and signature. The server SHALL reject an upload in either of these cases:
- the image's digest does not match the manifest
- the signature does not verify against one of the configured release public keys

The server SHALL serve published images over HTTPS without requiring a login.

#### Scenario: Unsigned or tampered upload
- **WHEN** the administrator uploads an image whose signature is missing or invalid, or whose digest does not match its manifest
- **THEN** the server rejects the release with an explanatory message and publishes nothing

#### Scenario: Valid upload
- **WHEN** the administrator uploads a correctly signed image and manifest and publishes it to `beta`
- **THEN** the release becomes the current `beta` release for that board type, and its image can be downloaded over HTTPS

### Requirement: Authentic and newer firmware only
A device SHALL install a release only when all of these hold:
- the release is for its board type
- its version is newer than the running version
- the image was downloaded over TLS with certificate verification
- the downloaded image's SHA-256 digest matches the manifest
- the signature verifies against one of the release public keys built into the running firmware

Otherwise the device SHALL keep running its current firmware and report the reason.

#### Scenario: Tampered image
- **WHEN** the downloaded image's digest or signature does not match
- **THEN** the device discards it, keeps running its current firmware, and reports the update as failed with the reason

#### Scenario: Older or same version
- **WHEN** the channel's current release has a version equal to or older than the running firmware
- **THEN** the device does not download or install it

### Requirement: Install only when idle
A device SHALL start installing an update only when it is idle. It SHALL defer the update while any of these is happening:
- playing a press, preview or feedback cue
- running Wi-Fi setup
- the button is being held

Presses sent while the device is installing or restarting SHALL still be played after the restart, provided they are not stale (see `thinking-of-you-messaging`).

#### Scenario: Update arrives mid-song
- **WHEN** a new release becomes available while the device is playing a received press
- **THEN** the device finishes the playback before starting the update

#### Scenario: Press during the update restart
- **WHEN** someone presses while a device in their group is restarting after an update
- **THEN** that device plays the press once it has restarted

### Requirement: Update progress and status
While an update downloads and installs, the device SHALL show a distinct progress indication on its LEDs at its configured brightness. The settings page SHALL show the update status:
- up to date
- downloading, with a percentage
- installed
- failed, with the reason
- rolled back, with the version

#### Scenario: Owner checks after an update
- **WHEN** an update has finished installing and the owner opens the settings page
- **THEN** the page shows the new firmware version and that the update installed successfully

### Requirement: Automatic rollback
A newly installed firmware SHALL be treated as provisional. It becomes healthy once it has run for 60 seconds without restarting and, whenever Wi-Fi is available, has connected to the broker within 10 minutes. The device SHALL revert to the previous firmware in either of these cases:
- the provisional firmware restarts 3 times without becoming healthy
- the provisional firmware has Wi-Fi but cannot connect to the broker for 10 minutes

#### Scenario: Update crashes on start-up
- **WHEN** a new firmware crashes during start-up on three consecutive boots
- **THEN** the device reverts to the previous firmware and reports the update as rolled back

#### Scenario: Update breaks the broker connection
- **WHEN** a new firmware joins Wi-Fi but cannot connect to the broker for 10 minutes
- **THEN** the device reverts to the previous firmware, reconnects, and reports the update as rolled back

### Requirement: Never retry a rolled-back version
A device SHALL NOT install again a version it has rolled back from. It SHALL install a later version when one is published.

#### Scenario: Fixed release after a rollback
- **WHEN** a device rolled back from 0.3.0 and the channel's release is still 0.3.0
- **THEN** the device stays on its previous firmware, and when 0.3.1 is published it installs 0.3.1
