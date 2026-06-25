/*
 * test_pd_clamp/test_pd_clamp.cpp
 *
 * PlatformIO Unity test for include/pd_util.h.
 *
 * Verifies the USB-PD voltage clamp policy shared by foc_thread.cpp and
 * hmi_thread.cpp:
 *   - Board-safe range  : [5.0, 9.0] V — pass through unchanged.
 *   - Below range (<5.0): return 5.0 V (safe USB fallback).
 *   - Above range (>9.0): return 5.0 V (safe USB fallback, NOT 9.0 V).
 *
 * The "above range → 5.0 V" behaviour is intentional — the firmware
 * logs a warning and falls back to the guaranteed-safe 5 V USB rail rather
 * than silently operating at a partially out-of-spec voltage.  Tests here
 * encode exactly that contract, not a saturating clamp.
 *
 * References:
 *   foc_thread.cpp  : run() voltage setup block (~line 48-51)
 *   hmi_thread.cpp  : init_pd() post-negotiation block (~line 609-614)
 *
 * Build: pio test -e native
 */

#include <unity.h>
#include "pd_util.h"

void setUp(void) {}
void tearDown(void) {}

/* --- values inside the valid window pass through unchanged --- */
void test_pd_clamp_min_boundary_accepted(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5.0f, pd_clamp(5.0f));
}

void test_pd_clamp_max_boundary_accepted(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 9.0f, pd_clamp(9.0f));
}

void test_pd_clamp_midpoint_accepted(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 7.0f, pd_clamp(7.0f));
}

void test_pd_clamp_6v_accepted(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 6.0f, pd_clamp(6.0f));
}

void test_pd_clamp_8v5_accepted(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 8.5f, pd_clamp(8.5f));
}

/* --- values below 5.0 V → 5.0 V fallback --- */
void test_pd_clamp_below_min_returns_5v(void)
{
    /* 4.9 V is "almost 5 V" but just out of window — must fall back */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5.0f, pd_clamp(4.9f));
}

void test_pd_clamp_zero_returns_5v(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5.0f, pd_clamp(0.0f));
}

void test_pd_clamp_negative_returns_5v(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5.0f, pd_clamp(-1.0f));
}

/* --- values above 9.0 V → 5.0 V fallback (NOT a saturating clamp to 9.0 V) --- */
void test_pd_clamp_12v_returns_5v(void)
{
    /*
     * A PPS charger might negotiate 12 V; the board cannot handle it.
     * The policy is to fall back to 5 V, not to clamp at 9 V.
     * This matches foc_thread.cpp: if (pdV < 5.0f || pdV > 9.0f) pdV = 5.0f;
     */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5.0f, pd_clamp(12.0f));
}

void test_pd_clamp_20v_returns_5v(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5.0f, pd_clamp(20.0f));
}

void test_pd_clamp_9v1_returns_5v(void)
{
    /* Just above 9 V — confirm the > check, not >= */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5.0f, pd_clamp(9.1f));
}

/* --- confirm 5.0 V exactly is NOT clamped (boundary inclusion) --- */
void test_pd_clamp_5v_is_not_clamped(void)
{
    float result = pd_clamp(5.0f);
    /* Must return exactly 5.0, not trigger any fallback */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5.0f, result);
}

/* --- confirm 9.0 V exactly is NOT clamped (boundary inclusion) --- */
void test_pd_clamp_9v_is_not_clamped(void)
{
    float result = pd_clamp(9.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 9.0f, result);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pd_clamp_min_boundary_accepted);
    RUN_TEST(test_pd_clamp_max_boundary_accepted);
    RUN_TEST(test_pd_clamp_midpoint_accepted);
    RUN_TEST(test_pd_clamp_6v_accepted);
    RUN_TEST(test_pd_clamp_8v5_accepted);
    RUN_TEST(test_pd_clamp_below_min_returns_5v);
    RUN_TEST(test_pd_clamp_zero_returns_5v);
    RUN_TEST(test_pd_clamp_negative_returns_5v);
    RUN_TEST(test_pd_clamp_12v_returns_5v);
    RUN_TEST(test_pd_clamp_20v_returns_5v);
    RUN_TEST(test_pd_clamp_9v1_returns_5v);
    RUN_TEST(test_pd_clamp_5v_is_not_clamped);
    RUN_TEST(test_pd_clamp_9v_is_not_clamped);
    return UNITY_END();
}
