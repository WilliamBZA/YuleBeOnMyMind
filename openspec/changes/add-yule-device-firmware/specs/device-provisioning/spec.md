# Spec Delta

## Purpose

Gets a decoration onto its owner's home Wi-Fi and securely connected to the broker under its own hardware identity, so that non-technical owners never deal with credentials or code.

## ADDED Requirements

### Requirement: Captive-portal Wi-Fi onboarding
When the device has no saved Wi-Fi credentials, it SHALL open a setup access point named `YuleBeOnMyMind-<suffix>`, where `<suffix>` is the last 6 hex digits of its device id. Connecting to that access point SHALL present a captive setup page where the user chooses a Wi-Fi network and enters its password. Once the device has joined the network successfully, it SHALL save the credentials and close the access point.

#### Scenario: First boot with no saved network
- **WHEN** a device with no saved Wi-Fi credentials is powered on
- **THEN** it opens the `YuleBeOnMyMind-<suffix>` access point and shows the setup-mode status indication

#### Scenario: User completes Wi-Fi setup
- **WHEN** the user connects to the setup access point, picks their home network and enters the correct password
- **THEN** the device joins that network, saves the credentials, closes the access point and continues to connect to the broker

#### Scenario: Wrong Wi-Fi password entered
- **WHEN** the user enters a password the network rejects
- **THEN** the device stays in setup mode and the setup page lets the user try again

### Requirement: Resilient reconnection to saved Wi-Fi
When saved Wi-Fi credentials exist but the network is unavailable, the device SHALL keep retrying in the background with increasing back-off, capped at 60 seconds between attempts. It SHALL NOT open the setup access point automatically.

#### Scenario: Home router is temporarily down
- **WHEN** the device boots and its saved network is unreachable for several minutes
- **THEN** the device keeps retrying, does not open the setup access point, and joins the network automatically once it returns

### Requirement: Re-open Wi-Fi setup on demand
A very long press of the button (see `device-controls`) SHALL clear only the saved Wi-Fi credentials and open the setup access point. The owner's profile, do-not-disturb settings, broker password, last-known group membership and update state SHALL be preserved. If no network is configured within 10 minutes, the setup access point SHALL close. The device SHALL then return to retrying the previously saved network, if there was one.

#### Scenario: Owner changes their router
- **WHEN** the owner holds the button for the very-long-press duration
- **THEN** the device opens the setup access point, and after new Wi-Fi details are entered it reconnects with its song, pattern, broker password and groups unchanged

#### Scenario: Setup started by accident
- **WHEN** the setup access point was opened by a very long press and nobody configures it within 10 minutes
- **THEN** the access point closes and the device resumes connecting to its previously saved network

### Requirement: Hardware-derived device identity
Each device SHALL have a device id of the form `d-` followed by 12 lowercase hex digits, derived from the device's factory-programmed hardware address. The device id SHALL stay the same across reboots, re-flashing, firmware updates and Wi-Fi resets, and SHALL NOT be editable. The device id SHALL be the device's broker username. The settings page, the serial log and the setup access point name SHALL show the device id, or its suffix in the case of the access point, so that an administrator can register the device on the server.

#### Scenario: Id survives re-flashing
- **WHEN** a device is re-flashed over USB or updated over the air
- **THEN** it reports the same device id as before

#### Scenario: Reading the id for registration
- **WHEN** the builder opens a new device's settings page
- **THEN** the page shows the device id read-only, for example `d-3c71bf4a2b10`, ready to be entered on the server's admin pages

### Requirement: Broker password setup
The settings page SHALL have a "Device setup" section that accepts the device's broker password, which the server issues when the device is added (see `family-groups`). The section SHALL also accept an optional override of the broker host and port. The stored password SHALL never be shown back in any page or response. Until a broker password is set, the device SHALL NOT connect to the broker and SHALL show a "setup required" indication on its settings page.

#### Scenario: Provisioning a new device before gifting it
- **WHEN** the builder enters the password the server showed for this device id and saves
- **THEN** the device stores it, connects to the broker with its device id as the username, and the settings page shows the password field as empty or masked, never the stored value

#### Scenario: Device without a password
- **WHEN** a device has Wi-Fi but no broker password
- **THEN** it does not attempt a broker connection and its settings page shows that device setup is required

### Requirement: Secure broker connection
The device SHALL connect to the configured MQTT broker over TLS, and SHALL verify the broker's certificate against a trusted certificate authority before sending credentials. Broker host and port SHALL default to build-time values: host `williamb.net` and a non-standard port chosen at build time. The device SHALL authenticate with its device id and its broker password. If certificate verification fails, the device SHALL refuse to connect.

#### Scenario: Valid broker certificate
- **WHEN** the device connects to the broker and the broker presents a certificate valid for the configured host and issued by a trusted authority
- **THEN** the device completes the connection and authenticates as its device id

#### Scenario: Impostor or misconfigured broker
- **WHEN** the broker presents a certificate that is expired, self-signed but not trusted, or for a different host
- **THEN** the device does not send its credentials, does not connect, and reports the broker as unreachable

### Requirement: Automatic broker reconnection
After losing the broker connection, the device SHALL reconnect automatically with increasing back-off, capped at 60 seconds between attempts. LED animations and button handling SHALL continue to work while it reconnects.

#### Scenario: Broker restarted
- **WHEN** the broker restarts while devices are connected
- **THEN** each device reconnects automatically within about 60 seconds of the broker becoming available, without user action

### Requirement: Local network name
Once on Wi-Fi, the device SHALL advertise itself on the local network as `yule-<suffix>.local`, where `<suffix>` is the last 6 hex digits of its device id. The settings page SHALL be reachable at that name and at the device's IP address.

#### Scenario: Owner opens settings by name
- **WHEN** the owner's phone or computer is on the same network and browses to `http://yule-4a2b10.local`
- **THEN** the settings page of the device whose id ends in `4a2b10` loads
