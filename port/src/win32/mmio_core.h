// RIFF chunk navigation for WINMM mmioDescend/mmioAscend (pure: works on a host FILE or a block of memory).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define MMIO_FOURCC(a, b, c, d) ((uint32_t)(uint8_t)(a) | (uint32_t)(uint8_t)(b) << 8 | (uint32_t)(uint8_t)(c) << 16 | (uint32_t)(uint8_t)(d) << 24)
#define MMIO_FINDCHUNK 0x0010u
#define MMIO_FINDRIFF  0x0020u
#define MMIO_FINDLIST  0x0040u
#define MMIO_DIRTY     0x10000000u
#define MMIOERR_CHUNKNOTFOUND 265u
#define MMIOERR_CANNOTSEEK    263u

// An mmio stream: a host file (f) or a memory file.
typedef struct { FILE *f; uint8_t *mem; uint32_t size, pos; } MmioStream;
long ms_tell(MmioStream *s);
int ms_seek(MmioStream *s, long offset, int origin);   // 0 on success; a memory stream cannot leave [0, size]
size_t ms_read(MmioStream *s, void *dst, size_t n);
size_t ms_write(MmioStream *s, const void *src, size_t n);   // a memory stream never grows

// ck = MMCKINFO {ckid, cksize, fccType, dwDataOffset, dwFlags}. parent may be NULL (search to end of stream).
// On success the stream is positioned at the chunk data (after fccType for RIFF/LIST) and 0 is returned.
uint32_t mmio_descend(MmioStream *s, uint32_t *ck, const uint32_t *parent, uint32_t flags);
// Seek past the chunk (data + pad byte); for a chunk created for writing (MMIO_DIRTY), rewrite its size first.
uint32_t mmio_ascend(MmioStream *s, uint32_t *ck);
