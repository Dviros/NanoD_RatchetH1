# Mapping definition

This document describes how key and knob mappings are defined in a profile, and how the firmware resolves them to HID, MIDI, and profile-switching outputs.

---

## Profile structure overview

A profile (`HapticProfile`) contains:
- Identity: `name`, `desc`, `profileTag`, `profileType`
- LED config: per-button idle/press colors, ring colors, brightness
- HMI config: `keys[4]` action lists + `knob[]` value list
- Haptic config: embedded in each knob value entry
- Audio config: click sound type and level (`NANO_AUDIO` builds only)

---

## Knob values

The `knob` array contains one or more value entries. At runtime the firmware scans the array from index 0 and activates the first entry whose `keyState` matches the current key bitmask.

### Value entry fields

| Field        | Type   | Description                                                      |
|--------------|--------|------------------------------------------------------------------|
| `keyState`   | uint8  | Bitmask of keys that must be held (0 = always active)            |
| `angleMin`   | float  | Start of the mapped angle range (radians)                        |
| `angleMax`   | float  | End of the mapped angle range (radians)                          |
| `valueMin`   | float  | Output value at `angleMin`                                       |
| `valueMax`   | float  | Output value at `angleMax`                                       |
| `step`       | float  | Quantisation step; 0 = continuous                                |
| `wrap`       | bool   | (reserved; not yet implemented in motor haptic)                  |
| `type`       | string | Output type (see below)                                          |
| `haptic`     | object | Haptic detent profile for this value (see below)                 |

Output types:

| `type`            | Additional fields               | Effect                                    |
|-------------------|---------------------------------|-------------------------------------------|
| `"midi"`          | `channel`, `cc`                 | Sends MIDI CC; value clamped to [0, 127]  |
| `"gamepad"`       | (axis config TBD)               | Gamepad axis output                       |
| `"mouse"`         | (axis config TBD)               | Mouse axis output                         |
| `"actions"`       | (CW/CCW action lists TBD)       | Step actions per detent                   |
| `"device_profiles"` | —                             | LCD shows profile list; turning switches profile |

### Value mapping formula

```
clamped_angle = clamp(shaft_angle, min(angleMin, angleMax), max(angleMin, angleMax))
value = (clamped_angle - angleMin) * (valueMax - valueMin) / (angleMax - angleMin) + valueMin
if step != 0: value = round(value / step) * step
```

The clamped angle replaces the live shaft angle so the output stays within bounds. The computed value is sent only when it differs from the previous send (`currentValue != lastValue`).

### Haptic sub-object fields

| Field             | Type  | Description                                             |
|-------------------|-------|---------------------------------------------------------|
| `mode`            | int   | 0=REGULAR, 1=VERNIER, 2=VISCOSE, 3=SPRING               |
| `startPos`        | float | Start position of the haptic range (radians)            |
| `endPos`          | float | End position of the haptic range (radians)              |
| `detentCount`     | int   | Number of detent positions                              |
| `vernier`         | int   | Fine-position count between each coarse detent (VERNIER)|
| `kxForce`         | bool  | Enable spring-back force at range limits                |
| `outputRamp`      | float | Force ramp-up rate                                      |
| `detentStrength`  | float | Detent holding force                                    |

Only the first (index 0) knob value's haptic profile is dispatched to the FOC thread. Multiple knob values share the same haptic feel unless you build separate profiles per key-state layer.

---

## Key actions

Each of the four physical keys has three action lists:

| List                    | Event                         |
|-------------------------|-------------------------------|
| `pressed[]`             | `kEventPressed` (initial down)|
| `released[]`            | `kEventReleased` (key up)     |
| `held[]`                | `kEventLongPressed` (≥ 500 ms)|

### Action types

#### Keyboard key

```json
{ "type": "key", "keyCodes": [17] }
```

Sends HID keyboard report on press; removes keycode from active set on release. Up to 6 simultaneous keycodes (6KRO).

#### Mouse button

```json
{ "type": "mouse", "buttons": 1 }
```

Adds button bits on press; clears them on release. Bitmask follows standard HID mouse button assignment.

#### Gamepad button

```json
{ "type": "gamepad", "buttons": 1 }
```

Adds/clears gamepad button bits in the same way.

#### MIDI CC

```json
{ "type": "midi", "channel": 1, "cc": 7, "val": 127 }
```

Sends a MIDI CC message on key-press only (not release). Sent on USB MIDI and/or hardware MIDI depending on `midiUsb.nano` / `midi2.nano` settings.

#### Profile change

```json
{ "type": "profile", "name": "Blender" }
```

Switches to the named profile immediately on press. The device emits `{"current":"Blender"}` over serial.

#### Next / previous profile

```json
{ "type": "next_profile" }
{ "type": "prev_profile" }
```

Cycles through profiles in order.

---

## Key state condition

A knob value or layer is active when the current key bitmask equals the entry's `keyState`. Bit positions:

| Bit | Key |
|-----|-----|
| 0   | A (index 0) |
| 1   | B (index 1) |
| 2   | C (index 2) |
| 3   | D (index 3) |

Examples:
- `"keyState": 0` — active when no keys are held (always, if it is the first entry and no keys are held)
- `"keyState": 9` (0b1001) — active when keys A and D are held simultaneously
- `"keyState": 15` (0b1111) — active only when all four keys are held

The firmware scans from index 0 and uses the first match. Order in the array determines priority.

---

## Device orientation and LED mapping

`deviceOrientation` (stored in `DeviceSettings`) maps 0–3 to a rotation offset for the LED ring:

| `deviceOrientation` | LED ring offset |
|---------------------|-----------------|
| 0                   | 0°              |
| 1                   | 90°             |
| 2                   | 180°            |
| 3                   | 270°            |

The LED pointer position tracks `haptic_state.current_pos` (uint16 haptic position) mapped to the ring LED count.

---

## Global mapping

The firmware does not currently implement a global cross-profile macro layer. A profile change is the only mechanism to alter the active key/knob mappings. A global mapping layer (applying on top of any active profile) is tracked as a future feature.
