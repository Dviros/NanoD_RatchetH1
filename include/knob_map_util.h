/*
 * knob_map_util.h — host-safe, header-only knob angle→value mapping.
 *
 * Implements the formula documented in mapping.md and used verbatim in
 * hmi_thread.cpp HmiThread::updateValue() (look for the "Fix 5" comment block):
 *
 *   clamped_angle = clamp(shaft_angle,
 *                         min(angle_min, angle_max),
 *                         max(angle_min, angle_max))
 *   value = (clamped_angle - angle_min) * (value_max - value_min)
 *           / (angle_max - angle_min) + value_min
 *   if step != 0: value = round(value / step) * step
 *
 * Key state bitmask semantics (mapping.md "Key state condition"):
 *   Bit 0 = Key A, Bit 1 = Key B, Bit 2 = Key C, Bit 3 = Key D.
 *   A knob value entry is active when its key_state field equals the
 *   current 4-bit bitmask exactly.
 *
 * This header is intentionally free of Arduino/ESP-IDF dependencies so it
 * can be #included by both firmware TUs and the native Unity test suite.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*
 * Internal helpers — no external dependencies (math.h fminf/fmaxf/roundf are
 * available on both host clang and xtensa-gcc).
 */
#include <math.h>

/*
 * knob_map_clamp — clamp angle to the effective [lo, hi] window.
 *
 * angle_min / angle_max may be in either order (the firmware handles negative
 * correlations by allowing angle_min > angle_max).
 */
static inline float knob_map_clamp(float angle, float angle_min, float angle_max)
{
    float lo = angle_min < angle_max ? angle_min : angle_max;
    float hi = angle_min < angle_max ? angle_max : angle_min;
    if (angle < lo) return lo;
    if (angle > hi) return hi;
    return angle;
}

/*
 * knob_map_value — full angle→value mapping with optional step quantisation.
 *
 * Matches hmi_thread.cpp updateValue() exactly (post Fix-5 and Fix-6).
 *
 * Special case: if angle_min == angle_max the output is always value_min
 * (avoids division by zero, matching the firmware guard).
 */
static inline float knob_map_value(float angle,
                                    float angle_min, float angle_max,
                                    float value_min, float value_max,
                                    float step)
{
    float clamped = knob_map_clamp(angle, angle_min, angle_max);

    float value;
    if (angle_max == angle_min) {
        value = value_min;
    } else {
        value = (clamped - angle_min) * (value_max - value_min)
                / (angle_max - angle_min) + value_min;
    }

    if (step != 0.0f) {
        value = roundf(value / step) * step;
    }

    return value;
}

/*
 * keystate_matches — returns non-zero if the given bitmask equals the entry's
 * required key_state (mapping.md: first matching entry wins).
 *
 * Bit positions: 0=A, 1=B, 2=C, 3=D (lower nibble only; upper bits reserved).
 */
static inline int keystate_matches(uint8_t current_state, uint8_t entry_state)
{
    return (current_state & 0x0Fu) == (entry_state & 0x0Fu);
}

#ifdef __cplusplus
} /* extern "C" */
#endif
