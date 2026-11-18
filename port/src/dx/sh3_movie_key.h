// SH3's data/movie/*.000 are MPEG-1 program streams behind a light obfuscation, undone by the game's own file
// reader. Pure: tested by port/test/sh3_movie_key_test.c.
// - 32 KB header: a 32-bit seed is spread over 4 bytes (0x55e2, 0x2a5c, 0x3d40, 0x2dde, high to low byte); the
//   header XORed with the LCG stream s = s * 0x5d588b65 + 1 (one value per dword) is all zeros apart from them.
// - the next 1024 values of the stream form a 4 KB table; the byte at file offset p (p >= 32 KB) is XORed with
//   table[p & 0xfff].
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SH3M_HEADER 0x8000
#define SH3M_TABLE 0x1000

// Fills table from the first SH3M_HEADER bytes of a .000; returns 0 if they are not an SH3 movie header.
static inline int sh3m_key(const uint8_t *head, uint8_t *table) {
    uint32_t s = (uint32_t)head[0x55e2] << 24 | (uint32_t)head[0x2a5c] << 16 | (uint32_t)head[0x3d40] << 8 | head[0x2dde];
    for (int i = 0; i < SH3M_HEADER / 4; i++) {
        uint32_t v;
        memcpy(&v, head + 4 * i, 4);
        s = s * 0x5d588b65u + 1u;
        v ^= s;
        for (int b = 0; b < 4; b++) {
            int at = 4 * i + b;
            if (at == 0x55e2 || at == 0x2a5c || at == 0x3d40 || at == 0x2dde) v &= ~(0xffu << (8 * b));
        }
        if (v) return 0;
    }
    for (int i = 0; i < SH3M_TABLE / 4; i++) {
        s = s * 0x5d588b65u + 1u;
        memcpy(table + 4 * i, &s, 4);
    }
    return 1;
}

// Undoes the obfuscation of n bytes of movie data that start pos bytes after the header.
static inline void sh3m_xor(const uint8_t *table, size_t pos, uint8_t *buf, size_t n) {
    for (size_t i = 0; i < n; i++) buf[i] ^= table[(pos + i) & (SH3M_TABLE - 1)];
}
