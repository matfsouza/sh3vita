// Tests for port/src/dx/sh3_movie_key.h: the obfuscation of SH3's data/movie/*.000.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sh3_movie_key.h"

static int fails;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

static uint32_t next(uint32_t *s) { return *s = *s * 0x5d588b65u + 1u; }

// Builds an obfuscated .000 the way the game's reader (0x405680) undoes it: 32 KB header that decodes to zeros
// except the 4 seed bytes, then data XORed with the 4 KB table that follows the header's key stream.
static uint8_t *make_movie(uint32_t seed, const uint8_t *plain, size_t n) {
    uint8_t *f = calloc(1, SH3M_HEADER + n);
    uint32_t s = seed;
    for (int i = 0; i < SH3M_HEADER / 4; i++) {
        uint32_t k = next(&s), v = k;
        memcpy(f + 4 * i, &v, 4);
    }
    // the seed is stored in clear at 4 places of the header (their decoded value is ignored)
    f[0x55e2] = (uint8_t)(seed >> 24); f[0x2a5c] = (uint8_t)(seed >> 16);
    f[0x3d40] = (uint8_t)(seed >> 8);  f[0x2dde] = (uint8_t)seed;
    uint8_t table[SH3M_TABLE];
    for (int i = 0; i < SH3M_TABLE / 4; i++) { uint32_t k = next(&s); memcpy(table + 4 * i, &k, 4); }
    for (size_t i = 0; i < n; i++) f[SH3M_HEADER + i] = plain[i] ^ table[(SH3M_HEADER + i) & (SH3M_TABLE - 1)];
    return f;
}

int main(void) {
    uint8_t plain[10000];
    for (size_t i = 0; i < sizeof plain; i++) plain[i] = (uint8_t)(i * 7 + 3);
    memcpy(plain, "\x00\x00\x01\xba\x21", 5);   // MPEG-1 pack header
    uint32_t seed = 0x5f12a07cu;
    uint8_t *f = make_movie(seed, plain, sizeof plain);
    uint8_t table[SH3M_TABLE];
    CHECK(sh3m_key(f, table) == 1);
    // Whole data in one call.
    uint8_t buf[sizeof plain];
    memcpy(buf, f + SH3M_HEADER, sizeof buf);
    sh3m_xor(table, 0, buf, sizeof buf);
    CHECK(memcmp(buf, plain, sizeof buf) == 0);
    // Any offset and length (blocks that do not start on a table boundary), position relative to the data.
    memcpy(buf, f + SH3M_HEADER, sizeof buf);
    sh3m_xor(table, 1234, buf + 1234, 5555);
    CHECK(memcmp(buf + 1234, plain + 1234, 5555) == 0);
    // A plain MPEG file (or a damaged header) is not taken for an obfuscated one.
    uint8_t mpeg[SH3M_HEADER];
    memset(mpeg, 0, sizeof mpeg);
    memcpy(mpeg, "\x00\x00\x01\xba\x21", 5);
    CHECK(sh3m_key(mpeg, table) == 0);
    f[100] ^= 1;
    CHECK(sh3m_key(f, table) == 0);
    free(f);
    if (fails) return 1;
    printf("ok\n");
    return 0;
}
