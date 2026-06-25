/*
 * crc32_util.h — host-safe, header-only CRC-32 (IEEE 802.3 / zlib)
 *
 * Polynomial : 0xEDB88320 (reversed / LSB-first)
 * Init       : 0xFFFFFFFF
 * Final XOR  : 0xFFFFFFFF
 *
 * This header is intentionally free of Arduino, FreeRTOS, ESP-IDF, and
 * LittleFS dependencies so it can be #included by both the firmware
 * (src/sprite_store.cpp) and the PlatformIO native Unity test suite.
 *
 * Canonical test vector (RFC 3720 / ITU-V.42):
 *   nano_crc32("123456789", 9) == 0xCBF43926
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * nano_crc32_update — incremental step.
 *
 * Feed chunks of data one at a time; start with crc=0.
 * The function handles the init/final XOR bookkeeping internally via the
 * complement trick: each call inverts, accumulates, and inverts back, so
 * concatenating calls is equivalent to processing all data in one shot.
 *
 *   uint32_t crc = 0;
 *   crc = nano_crc32_update(crc, buf1, len1);
 *   crc = nano_crc32_update(crc, buf2, len2);
 *   // crc now equals nano_crc32(combined, len1+len2)
 */
static inline uint32_t nano_crc32_update(uint32_t crc,
                                          const uint8_t *buf,
                                          size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

/*
 * nano_crc32 — one-shot CRC-32 over a buffer.
 *
 * Equivalent to:  nano_crc32_update(0, buf, len)
 */
static inline uint32_t nano_crc32(const uint8_t *buf, size_t len)
{
    return nano_crc32_update(0u, buf, len);
}

#ifdef __cplusplus
} /* extern "C" */
#endif
