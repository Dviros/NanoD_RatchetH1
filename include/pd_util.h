/*
 * pd_util.h — host-safe, header-only USB-PD voltage clamp helper.
 *
 * Policy (matches both foc_thread.cpp:49 and hmi_thread.cpp:610):
 *   Valid range: [5.0, 9.0] V (board-safe operating window).
 *   Any value outside this range is replaced by 5.0 V (safe USB fallback).
 *
 * This header is intentionally free of Arduino/ESP-IDF dependencies so it
 * can be #included by both firmware TUs and the native Unity test suite.
 *
 * Usage in foc_thread.cpp (see run(), voltage setup block):
 *   float pdV = pd_clamp(DeviceSettings::getInstance().pdVoltage);
 *   driver.voltage_power_supply = pdV;
 *   driver.voltage_limit        = pdV;
 *
 * Usage in hmi_thread.cpp (see init_pd(), post-negotiation block):
 *   negotiated_v = pd_clamp(negotiated_v);
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * pd_clamp — clamp a negotiated PD voltage to the board-safe [5.0, 9.0] V window.
 *
 * Returns the input voltage if it falls within [5.0, 9.0]; otherwise 5.0 f.
 * This exactly matches the inline logic present in both foc_thread.cpp and
 * hmi_thread.cpp to give both call-sites a single tested source of truth.
 */
static inline float pd_clamp(float v)
{
    if (v < 5.0f || v > 9.0f)
        return 5.0f;
    return v;
}

#ifdef __cplusplus
} /* extern "C" */
#endif
