# Spec Delta

## Purpose

Renders a family member's chosen light pattern (an effect with colours and a speed) on whatever LED strip a decoration has, smoothly, within safe brightness and power limits.

## ADDED Requirements

### Requirement: Built-in effects
The device SHALL support at least these effects. Each uses the pattern's colours as described. Where an effect uses more colours than are supplied, the supplied colours SHALL be repeated.

| Effect | Behaviour | Colours used |
|---|---|---|
| `solid` | All LEDs lit steadily | colour 1 |
| `breathe` | All LEDs fade smoothly up and down | cycles colours 1–3 per breath |
| `twinkle` | Random LEDs fade in and out | random from colours 1–3 |
| `chase` | Lit LEDs travel along the strip | colours 1–3 in sequence |
| `rainbow` | A moving rainbow | ignores colours |
| `candy-cane` | Alternating stripes that scroll | colours 1 and 2 |
| `comet` | A bright head with a fading tail sweeps the strip | colour 1 |
| `sparkle` | Brief white or colour flashes over a dim base | base colour 1, flashes colour 2 |

#### Scenario: Candy-cane with two colours
- **WHEN** a pattern specifies `candy-cane` with red and white
- **THEN** the strip shows alternating red and white stripes that scroll along the strip

#### Scenario: Effect with fewer colours than it uses
- **WHEN** a pattern specifies `chase` with only one colour
- **THEN** the chase uses that single colour for every lit segment

### Requirement: Speed control
Each effect SHALL offer three speeds (slow, medium, fast), and each step SHALL make the motion noticeably faster than the step before. `solid` SHALL look the same at every speed.

#### Scenario: Fast breathe
- **WHEN** the same `breathe` pattern is shown at slow and then at fast speed
- **THEN** the fast version completes visibly more breaths per minute

### Requirement: Any strip length
Patterns SHALL render correctly on any configured strip length from 1 to 300 LEDs, so a pattern chosen on one family member's decoration looks recognisably the same on decorations with different numbers of LEDs.

#### Scenario: Pattern from a long strip shown on a short strip
- **WHEN** a pattern chosen by an owner with a 60-LED strip is received by a device with 8 LEDs
- **THEN** the 8-LED device shows the same effect, colours and speed scaled to its 8 LEDs

### Requirement: Brightness and power limits
All LED output SHALL be limited by the receiving device's brightness setting. Output SHALL also be limited so that the estimated LED current stays within a configured power budget, which defaults to a level safe for USB power. Limiting brightness or power SHALL NOT change the pattern's colours or timing.

#### Scenario: Full-white pattern on USB power
- **WHEN** a `solid` white pattern at 100% brightness would exceed the power budget
- **THEN** the device automatically dims the output to stay within the budget

### Requirement: Smooth animation
Animations SHALL update at least 30 times per second. They SHALL NOT freeze for more than 100 ms while the device sends, receives, connects or reconnects.

#### Scenario: Animation during broker reconnection
- **WHEN** the broker connection drops and the device reconnects while a pattern is playing
- **THEN** the animation continues without visible freezes

### Requirement: Fade-out at end of playback
When a pattern's display time ends, the LEDs SHALL fade to the idle state over about 1 second, not switch off abruptly.

#### Scenario: Pattern finishes
- **WHEN** a received pattern reaches the end of its display time
- **THEN** the LEDs fade out smoothly over roughly one second
