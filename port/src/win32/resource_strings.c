// String-table resources (RT_STRING) for USER32 LoadStringA.
#include "resource_strings.h"
#include <string.h>

static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

int rsrc_string_from_block(const uint8_t *block, uint32_t size, unsigned index, char *out, size_t cap) {
    if (cap) out[0] = 0;
    uint32_t at = 0;
    for (unsigned i = 0; i < index; i++) {          // skip the entries before `index`
        if (at + 2 > size) return 0;
        at += 2 + 2u * rd16(block + at);
    }
    if (at + 2 > size || !cap) return 0;
    uint32_t len = rd16(block + at), n = 0;
    at += 2;
    if (at + 2 * len > size) return 0;
    for (; n < len && n + 1 < cap; n++) {
        uint16_t ch = rd16(block + at + 2 * n);
        out[n] = (char)(ch < 0x100 ? ch : '?');
    }
    out[n] = 0;
    return (int)n;
}
