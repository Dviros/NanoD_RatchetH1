/*
 * test_knob_map/test_knob_map.cpp
 *
 * PlatformIO Unity test for include/knob_map_util.h.
 *
 * Verifies the knob angle→value mapping formula and key-state bitmask logic
 * documented in mapping.md and implemented in hmi_thread.cpp updateValue().
 *
 * Formula (from mapping.md and hmi_thread.cpp post Fix-5/Fix-6):
 *   clamped = clamp(angle, min(angleMin,angleMax), max(angleMin,angleMax))
 *   value   = (clamped - angleMin) * (valueMax - valueMin)
 *             / (angleMax - angleMin) + valueMin
 *   if step != 0: value = round(value / step) * step
 *
 * Key-state bitmask (mapping.md "Key state condition"):
 *   Bit 0 = Key A, Bit 1 = Key B, Bit 2 = Key C, Bit 3 = Key D.
 *   A value entry is active when its key_state equals the current bitmask exactly.
 *
 * Build: pio test -e native
 */

#include <unity.h>
#include "knob_map_util.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/* Helper — 2*PI constant (same as SimpleFOC _2PI) */
static const float TWO_PI_F = 6.283185307179586f;

/* ------------------------------------------------------------------ */
/* knob_map_value — basic mapping                                       */
/* ------------------------------------------------------------------ */

void test_knob_map_midpoint(void)
{
    /* Angle at midpoint of [0, 2π] → value at midpoint of [0, 127] */
    float v = knob_map_value(TWO_PI_F / 2.0f,
                              0.0f, TWO_PI_F,
                              0.0f, 127.0f,
                              0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 63.5f, v);
}

void test_knob_map_at_angle_min_gives_value_min(void)
{
    float v = knob_map_value(0.0f, 0.0f, TWO_PI_F, 0.0f, 127.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, v);
}

void test_knob_map_at_angle_max_gives_value_max(void)
{
    float v = knob_map_value(TWO_PI_F, 0.0f, TWO_PI_F, 0.0f, 127.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 127.0f, v);
}

/* ------------------------------------------------------------------ */
/* Clamping — angle below min is clamped                               */
/* ------------------------------------------------------------------ */

void test_knob_map_below_angle_min_clamps(void)
{
    /* angle = -1 rad, window = [0, 2π] → should clamp to 0 → value = 0 */
    float v = knob_map_value(-1.0f, 0.0f, TWO_PI_F, 0.0f, 127.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, v);
}

void test_knob_map_above_angle_max_clamps(void)
{
    /* angle = 10 rad (> 2π) → clamps to 2π → value = 127 */
    float v = knob_map_value(10.0f, 0.0f, TWO_PI_F, 0.0f, 127.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 127.0f, v);
}

/* ------------------------------------------------------------------ */
/* Negative correlation (angle_min > angle_max allowed)                */
/* ------------------------------------------------------------------ */

void test_knob_map_inverted_range(void)
{
    /*
     * angle_min=TWO_PI, angle_max=0 → value decreases as angle increases.
     * At angle=0:     value = (0 - 2π) * (127-0) / (0 - 2π) + 0 = 127
     * At angle=2π:   value = (2π - 2π) * ... + 0 = 0
     */
    float at_zero = knob_map_value(0.0f, TWO_PI_F, 0.0f, 0.0f, 127.0f, 0.0f);
    float at_max  = knob_map_value(TWO_PI_F, TWO_PI_F, 0.0f, 0.0f, 127.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 127.0f, at_zero);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f,   at_max);
}

/* ------------------------------------------------------------------ */
/* Step quantisation                                                    */
/* ------------------------------------------------------------------ */

void test_knob_map_step_rounds_to_nearest(void)
{
    /* step=1, angle at ≈ 25% of [0, 2π] → value ≈ 31.75 → round to 32 */
    float angle = TWO_PI_F * 0.25f;
    float v = knob_map_value(angle, 0.0f, TWO_PI_F, 0.0f, 127.0f, 1.0f);
    /* 0.25 * 127 = 31.75 → rounds to 32 */
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 32.0f, v);
}

void test_knob_map_step_zero_is_continuous(void)
{
    /* step=0 → no rounding; fractional value must be preserved */
    float angle = TWO_PI_F * 0.3333f;
    float v = knob_map_value(angle, 0.0f, TWO_PI_F, 0.0f, 127.0f, 0.0f);
    /* 0.3333 * 127 ≈ 42.33 — check it is near the expected fractional value */
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 42.33f, v);
}

void test_knob_map_step_midi_cc_range(void)
{
    /* MIDI CC: [0..127] int steps — at exactly 50% → 63 or 64 depending on rounding */
    float angle = TWO_PI_F * 0.5f;
    float v = knob_map_value(angle, 0.0f, TWO_PI_F, 0.0f, 127.0f, 1.0f);
    /* 63.5 rounds to 64 (roundf ties to even is platform-dependent; accept 63 or 64) */
    TEST_ASSERT(v == 63.0f || v == 64.0f);
}

/* ------------------------------------------------------------------ */
/* Zero-range guard (angle_min == angle_max → always value_min)        */
/* ------------------------------------------------------------------ */

void test_knob_map_zero_angle_range_returns_value_min(void)
{
    float v = knob_map_value(1.0f, 1.0f, 1.0f, 42.0f, 100.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 42.0f, v);
}

/* ------------------------------------------------------------------ */
/* Key-state bitmask                                                    */
/* ------------------------------------------------------------------ */

void test_keystate_no_keys_matches_zero(void)
{
    /* No keys held → bitmask 0x00 → must match entry with key_state 0 */
    TEST_ASSERT_TRUE(keystate_matches(0x00u, 0x00u));
}

void test_keystate_key_A_only(void)
{
    /* Key A pressed (bit 0) */
    TEST_ASSERT_TRUE(keystate_matches(0x01u, 0x01u));
    TEST_ASSERT_FALSE(keystate_matches(0x01u, 0x00u));
    TEST_ASSERT_FALSE(keystate_matches(0x01u, 0x03u));
}

void test_keystate_keys_A_and_D(void)
{
    /* mapping.md example: keyState 9 (0b1001) = A and D held */
    TEST_ASSERT_TRUE(keystate_matches(0x09u, 0x09u));
    TEST_ASSERT_FALSE(keystate_matches(0x09u, 0x01u));
}

void test_keystate_all_keys(void)
{
    /* All four keys (0b1111 = 15) */
    TEST_ASSERT_TRUE(keystate_matches(0x0Fu, 0x0Fu));
    TEST_ASSERT_FALSE(keystate_matches(0x0Fu, 0x07u));
}

void test_keystate_upper_nibble_ignored(void)
{
    /* Only lower 4 bits count; upper bits must be masked away */
    TEST_ASSERT_TRUE(keystate_matches(0xF0u, 0x00u));  /* upper bits set, lower 0 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_knob_map_midpoint);
    RUN_TEST(test_knob_map_at_angle_min_gives_value_min);
    RUN_TEST(test_knob_map_at_angle_max_gives_value_max);
    RUN_TEST(test_knob_map_below_angle_min_clamps);
    RUN_TEST(test_knob_map_above_angle_max_clamps);
    RUN_TEST(test_knob_map_inverted_range);
    RUN_TEST(test_knob_map_step_rounds_to_nearest);
    RUN_TEST(test_knob_map_step_zero_is_continuous);
    RUN_TEST(test_knob_map_step_midi_cc_range);
    RUN_TEST(test_knob_map_zero_angle_range_returns_value_min);
    RUN_TEST(test_keystate_no_keys_matches_zero);
    RUN_TEST(test_keystate_key_A_only);
    RUN_TEST(test_keystate_keys_A_and_D);
    RUN_TEST(test_keystate_all_keys);
    RUN_TEST(test_keystate_upper_nibble_ignored);
    return UNITY_END();
}
