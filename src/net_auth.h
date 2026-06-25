#pragma once

// net_auth.h — HMAC-SHA256 helpers for the TCP mutual-auth handshake.
//
// Only pull in mbedTLS headers inside WIFI_ENABLED builds; the no-WiFi build
// must still be able to #include wifi_thread.h which does NOT include this
// file, so there is no leakage.  Callers that actually need these functions
// live entirely inside the #ifdef WIFI_ENABLED block of wifi_thread.cpp.
//
// mbedTLS is bundled with arduino-esp32 / ESP-IDF 4.4; no extra lib_dep
// is needed in platformio.ini.

#ifdef WIFI_ENABLED

#include <stdint.h>
#include <stddef.h>
#include <string.h>          // memset
#include <Arduino.h>         // esp_random(), String

// mbedTLS HMAC — available in ESP-IDF 4.4 bundled mbedTLS
#include <mbedtls/md.h>

// ---------------------------------------------------------------------------
// hmac_sha256
//   Computes HMAC-SHA256(key, msg) → out[32].
//   Returns 0 on success, non-zero on mbedTLS error.
// ---------------------------------------------------------------------------
inline int hmac_sha256(const uint8_t* key, size_t keyLen,
                       const uint8_t* msg, size_t msgLen,
                       uint8_t out[32])
{
    const mbedtls_md_info_t* info =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) return -1;
    int rc = mbedtls_md_hmac(info, key, keyLen, msg, msgLen, out);
    return rc;
}

// ---------------------------------------------------------------------------
// hex_encode
//   Encodes `len` bytes from `src` into a lowercase hex String of length 2*len.
// ---------------------------------------------------------------------------
inline String hex_encode(const uint8_t* src, size_t len)
{
    // NB: must NOT be named HEX — Arduino defines `#define HEX 16`, which would
    // turn this declaration into `const char 16[]`.
    static const char kHexDigits[] = "0123456789abcdef";
    String out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out += kHexDigits[(src[i] >> 4) & 0x0F];
        out += kHexDigits[ src[i]       & 0x0F];
    }
    return out;
}

// ---------------------------------------------------------------------------
// hex_decode
//   Decodes a hex string into `out` (caller provides buffer of len bytes).
//   Returns true on success, false if string length != 2*len or bad chars.
// ---------------------------------------------------------------------------
inline bool hex_decode(const String& hex, uint8_t* out, size_t len)
{
    if ((size_t)hex.length() != len * 2) return false;
    for (size_t i = 0; i < len; i++) {
        uint8_t hi, lo;
        char ch = hex[i * 2];
        if      (ch >= '0' && ch <= '9') hi = ch - '0';
        else if (ch >= 'a' && ch <= 'f') hi = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') hi = ch - 'A' + 10;
        else return false;

        ch = hex[i * 2 + 1];
        if      (ch >= '0' && ch <= '9') lo = ch - '0';
        else if (ch >= 'a' && ch <= 'f') lo = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') lo = ch - 'A' + 10;
        else return false;

        out[i] = (hi << 4) | lo;
    }
    return true;
}

// ---------------------------------------------------------------------------
// ct_memeq — constant-time byte-array equality.
//
// Both lengths must match first (non-secret comparison of lengths is fine —
// the length of an HMAC digest is public).  The XOR-accumulate loop avoids
// short-circuit branches so the timing does not reveal how many bytes matched.
//
// Using volatile on the accumulator prevents the compiler from optimising the
// loop away.  This is not a formal cryptographic guarantee on all compilers
// (CMSE SG / clang -ftrivial-auto-var-init can still inline constants), but
// it is the idiomatic embedded approach absent a platform ct_memcmp().
// ---------------------------------------------------------------------------
inline bool ct_memeq(const uint8_t* a, const uint8_t* b, size_t len)
{
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

// ---------------------------------------------------------------------------
// make_nonce_hex
//   Fills 16 random bytes from esp_random() and returns them as a 32-char
//   hex string.  esp_random() calls the hardware RNG on ESP32-S3.
// ---------------------------------------------------------------------------
inline String make_nonce_hex(uint8_t raw[16])
{
    // esp_random() returns 32-bit words; collect 4 of them for 16 bytes
    for (int w = 0; w < 4; w++) {
        uint32_t r = esp_random();
        raw[w * 4 + 0] = (r      ) & 0xFF;
        raw[w * 4 + 1] = (r >>  8) & 0xFF;
        raw[w * 4 + 2] = (r >> 16) & 0xFF;
        raw[w * 4 + 3] = (r >> 24) & 0xFF;
    }
    return hex_encode(raw, 16);
}

#endif // WIFI_ENABLED
