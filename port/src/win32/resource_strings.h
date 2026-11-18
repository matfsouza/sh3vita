// String-table resources (RT_STRING) for USER32 LoadStringA (pure: works on a copy of the block).
#pragma once
#include <stddef.h>
#include <stdint.h>

// String id N lives in block (N >> 4) + 1 at index N & 15. A block holds 16 entries: u16 length, then that many
// UTF-16 code units. Converts entry `index` to 8-bit (code units < 0x100, others as '?'), writes at most cap-1
// chars plus NUL, and returns the number of chars written (0 for an empty or missing entry).
int rsrc_string_from_block(const uint8_t *block, uint32_t size, unsigned index, char *out, size_t cap);
