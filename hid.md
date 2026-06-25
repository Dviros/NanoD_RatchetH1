
# HID — human interface device

The Nano D++ registers with the host OS as three HID interfaces simultaneously via a single USB composite descriptor:

| Report ID | Interface | HID class                      |
|-----------|-----------|-------------------------------|
| 1         | Keyboard  | `TUD_HID_REPORT_DESC_KEYBOARD` |
| 2         | Mouse     | `TUD_HID_REPORT_DESC_MOUSE`    |
| 3         | Gamepad   | `TUD_HID_REPORT_DESC_GAMEPAD`  |

The USB HID object is configured with no boot protocol (`HID_ITF_PROTOCOL_NONE`) and a 2 ms poll interval.

All three interfaces share one `Adafruit_USBD_HID` object. The device string descriptor is `"Nano_D HID"`.

If the host is suspended and a key, mouse button, or gamepad button state change occurs, the device sends a USB remote-wakeup signal before sending the HID report.

---

## Keyboard

The Nano D++ sends standard USB HID keyboard reports (Report ID 1). Up to 6 simultaneous keycodes are supported (standard 6KRO).

Key codes are sent when:
- A key action of type `KA_KEY` fires on press (`kEventPressed`) — the key code is added to the active set.
- The same action fires on release (`kEventReleased`) — the key code is removed from the active set.

The modifier byte is always 0 in the current implementation. If you need modifier keys, include the modifier keycode in the `keyCodes` array.

Configuration in the profile JSON:

```json
{ "type": "key", "keyCodes": [17] }
```

`keyCodes` is an array; only the first element (`keyCodes[0]`) is used per action in the current implementation.

---

## Mouse

Mouse button state is tracked as a bitmask. Mouse button reports (Report ID 2) are sent whenever the bitmask changes.

Configuration:

```json
{ "type": "mouse", "buttons": 1 }
```

The `buttons` field is an OR-mask of standard HID mouse button bits (1 = left, 2 = right, 4 = middle, etc.). The current implementation supports button state only; axis (scroll wheel / pointer movement) mapping is not yet implemented via the key/knob profile system.

---

## Gamepad

Gamepad reports (Report ID 3) use the `hid_gamepad_report_t` structure: axes `x`, `y`, `z`, `rz`, `rx`, `ry`, a hat switch, and a buttons bitmask. The current implementation sets all axes and hat to 0 and only populates the buttons bitmask.

Configuration:

```json
{ "type": "gamepad", "buttons": 1 }
```

`buttons` is an OR-mask of gamepad button bits. Button state is maintained across press and release events.

---

## Key mapping actions

Each of the four physical keys (A–D, indices 0–3) can have three action lists:

| List       | Fires on                              |
|------------|---------------------------------------|
| `pressed`  | `kEventPressed` (initial key-down)    |
| `released` | `kEventReleased` (key-up)             |
| `held`     | `kEventLongPressed` (≥ 500 ms hold)   |

Long-press (`held`) actions were parsed but never fired in previous firmware; this is fixed in the current release.

Available action types:

| `type`           | Effect                                              |
|------------------|-----------------------------------------------------|
| `key`            | HID keyboard keycode press/release                  |
| `mouse`          | HID mouse button press/release                      |
| `gamepad`        | HID gamepad button press/release                    |
| `midi`           | MIDI CC message send                                |
| `profile`        | Switch to a named profile                           |
| `next_profile`   | Cycle to the next profile in order                  |
| `prev_profile`   | Cycle to the previous profile in order              |

Profile-change actions are forwarded to `com_thread` via a message queue; the device emits `{"current":"<name>"}` over serial when the switch occurs.

---

## Knob value types

The knob mapping (`"type"` field in the `knob` array) determines what output the knob generates:

| `type`              | Output                                         |
|---------------------|------------------------------------------------|
| `midi`              | MIDI CC on configured channel                  |
| `gamepad`           | Gamepad axis (not yet fully wired)             |
| `mouse`             | Mouse axis (not yet fully wired)               |
| `actions`           | Step actions (CW / CCW key actions)            |
| `device_profiles`   | Shows profile list on LCD, switches on select  |

The active knob value is the first entry in the `knob` array whose `keyState` bitmask matches the currently-held key bitmask. A `keyState` of 0 matches unconditionally (no keys held).

The mapped value is computed as:

```
value = (angle - angleMin) * (valueMax - valueMin) / (angleMax - angleMin) + valueMin
```

If `step` is non-zero the result is rounded to the nearest multiple of `step`. The angle is clamped to `[angleMin, angleMax]` before mapping.

---

## MIDI from keys

Key actions of type `midi` send a MIDI CC message on key-press only (not release):

```json
{ "type": "midi", "channel": 1, "cc": 7, "val": 127 }
```

The message is sent on both USB MIDI and hardware MIDI jack depending on the `midiUsb.nano` and `midi2.nano` settings flags.
