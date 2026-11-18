// RIFF chunk navigation for WINMM mmioDescend/mmioAscend, on a host file or a memory file.
#include "mmio_core.h"
#include <string.h>

static const uint32_t RIFF = MMIO_FOURCC('R', 'I', 'F', 'F'), LIST = MMIO_FOURCC('L', 'I', 'S', 'T');

long ms_tell(MmioStream *s) { return s->f ? ftell(s->f) : (long)s->pos; }

int ms_seek(MmioStream *s, long offset, int origin) {
    if (s->f) return fseek(s->f, offset, origin);
    long base = origin == SEEK_CUR ? (long)s->pos : origin == SEEK_END ? (long)s->size : 0, at = base + offset;
    if (at < 0 || at > (long)s->size) return -1;
    s->pos = (uint32_t)at;
    return 0;
}

size_t ms_read(MmioStream *s, void *dst, size_t n) {
    if (s->f) return fread(dst, 1, n, s->f);
    size_t left = s->size - s->pos, k = n < left ? n : left;
    memcpy(dst, s->mem + s->pos, k);
    s->pos += (uint32_t)k;
    return k;
}

size_t ms_write(MmioStream *s, const void *src, size_t n) {
    if (s->f) return fwrite(src, 1, n, s->f);
    size_t left = s->size - s->pos, k = n < left ? n : left;
    memcpy(s->mem + s->pos, src, k);
    s->pos += (uint32_t)k;
    return k;
}

uint32_t mmio_descend(MmioStream *s, uint32_t *ck, const uint32_t *parent, uint32_t flags) {
    uint32_t want_id = ck[0], want_type = ck[2];
    long end = -1;
    if (parent) end = (long)(parent[3] + parent[1]);
    for (;;) {
        long at = ms_tell(s);
        if (at < 0) return MMIOERR_CANNOTSEEK;
        if (end >= 0 && at + 8 > end) return MMIOERR_CHUNKNOTFOUND;
        uint32_t hdr[2];
        if (ms_read(s, hdr, 8) != 8) return MMIOERR_CHUNKNOTFOUND;
        uint32_t id = hdr[0], size = hdr[1], type = 0;
        int container = id == RIFF || id == LIST;
        if (container && ms_read(s, &type, 4) != 4) return MMIOERR_CHUNKNOTFOUND;
        int match = (flags & MMIO_FINDRIFF) ? id == RIFF && (!want_type || type == want_type)
                  : (flags & MMIO_FINDLIST) ? id == LIST && (!want_type || type == want_type)
                  : (flags & MMIO_FINDCHUNK) ? id == want_id
                  : 1;
        if (match) {
            ck[0] = id; ck[1] = size; ck[2] = type; ck[3] = (uint32_t)(at + 8); ck[4] = 0;
            return 0;
        }
        if (ms_seek(s, at + 8 + (long)size + (long)(size & 1), SEEK_SET)) return MMIOERR_CHUNKNOTFOUND;
    }
}

uint32_t mmio_ascend(MmioStream *s, uint32_t *ck) {
    if (ck[4] & MMIO_DIRTY) {
        long here = ms_tell(s);
        if (here < 0) return MMIOERR_CANNOTSEEK;
        ck[1] = (uint32_t)(here - (long)ck[3]);
        if (here & 1) { uint8_t pad = 0; ms_write(s, &pad, 1); }
        if (ms_seek(s, (long)ck[3] - 4, SEEK_SET) || ms_write(s, &ck[1], 4) != 4) return MMIOERR_CANNOTSEEK;
        ck[4] &= ~MMIO_DIRTY;
    }
    long next = (long)ck[3] + (long)ck[1] + (long)(ck[1] & 1);
    return ms_seek(s, next, SEEK_SET) ? MMIOERR_CANNOTSEEK : 0;
}
