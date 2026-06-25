/*
 * test_crc32/test_crc32.cpp
 *
 * PlatformIO Unity test for include/crc32_util.h.
 *
 * Verifies the IEEE 802.3 / zlib CRC-32 implementation used by
 * sprite_store.cpp to validate chunked sprite uploads.
 *
 * The critical production bug was a CRC mismatch during sprite upload; this
 * test suite ensures the algorithm is correct before any hardware flash.
 *
 * Build: pio test -e native
 */

#include <unity.h>
#include "crc32_util.h"   /* nano_crc32, nano_crc32_update */
#include <string.h>       /* strlen */

void setUp(void) {}
void tearDown(void) {}

/* --- canonical RFC 3720 / ITU-V.42 test vector --- */
void test_crc32_standard_vector(void)
{
    /* "123456789" → 0xCBF43926 */
    const uint8_t input[] = "123456789";
    uint32_t result = nano_crc32(input, 9u);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, result);
}

/* --- empty buffer must return 0x00000000 --- */
void test_crc32_empty(void)
{
    uint32_t result = nano_crc32(NULL, 0u);
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, result);
}

/* --- single-byte inputs (spot checks) --- */
void test_crc32_single_byte_zero(void)
{
    const uint8_t b = 0x00;
    /* CRC32(0x00) == 0xD202EF8D */
    TEST_ASSERT_EQUAL_HEX32(0xD202EF8Du, nano_crc32(&b, 1u));
}

void test_crc32_single_byte_ff(void)
{
    const uint8_t b = 0xFF;
    /* CRC32(0xFF) == 0xFF000000 */
    TEST_ASSERT_EQUAL_HEX32(0xFF000000u, nano_crc32(&b, 1u));
}

/* --- incremental == one-shot for standard vector --- */
void test_crc32_incremental_equals_oneshot(void)
{
    /*
     * Simulate the chunked sprite-upload code path in sprite_store.cpp:
     * crc_run starts at 0 and is updated per chunk via nano_crc32_update.
     */
    const uint8_t chunk1[] = "1234";
    const uint8_t chunk2[] = "56789";

    uint32_t crc = 0u;
    crc = nano_crc32_update(crc, chunk1, 4u);
    crc = nano_crc32_update(crc, chunk2, 5u);

    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc);
}

/* --- three-chunk split also matches --- */
void test_crc32_incremental_three_chunks(void)
{
    const uint8_t a[] = "123";
    const uint8_t b[] = "456";
    const uint8_t c[] = "789";

    uint32_t crc = 0u;
    crc = nano_crc32_update(crc, a, 3u);
    crc = nano_crc32_update(crc, b, 3u);
    crc = nano_crc32_update(crc, c, 3u);

    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc);
}

/* --- all-zeros 4 bytes (known value) --- */
void test_crc32_four_zeros(void)
{
    const uint8_t z[4] = {0, 0, 0, 0};
    /* CRC32 of four 0x00 bytes == 0x2144DF1C */
    TEST_ASSERT_EQUAL_HEX32(0x2144DF1Cu, nano_crc32(z, 4u));
}

/* --- idempotency: same input always gives same result --- */
void test_crc32_idempotent(void)
{
    const uint8_t buf[] = "Binaris Nano_D++ sprite";
    uint32_t r1 = nano_crc32(buf, sizeof(buf) - 1u);
    uint32_t r2 = nano_crc32(buf, sizeof(buf) - 1u);
    TEST_ASSERT_EQUAL_HEX32(r1, r2);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc32_standard_vector);
    RUN_TEST(test_crc32_empty);
    RUN_TEST(test_crc32_single_byte_zero);
    RUN_TEST(test_crc32_single_byte_ff);
    RUN_TEST(test_crc32_incremental_equals_oneshot);
    RUN_TEST(test_crc32_incremental_three_chunks);
    RUN_TEST(test_crc32_four_zeros);
    RUN_TEST(test_crc32_idempotent);
    return UNITY_END();
}
