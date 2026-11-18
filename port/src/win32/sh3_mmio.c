// WINMM multimedia file I/O (mmio*) used by SH3: RIFF navigation (mmio_core.c) plus plain and buffered access,
// on a host file or on a memory file (MMIOINFO.fccIOProc = 'MEM ': SH3 opens its sounds already loaded from sd.afs,
//.
// Buffered access (mmioGetInfo/mmioAdvance/mmioSetInfo) exposes a guest buffer. For a host file the stream position
// always follows the end of the data handed out, and mmioSetInfo gives back what the game did not consume; for a
// memory file the buffer is the file itself.
#include "win32.h"
#include "mmio_core.h"
#include <stdlib.h>

#define W(name) void imp_WINMM_##name(CPU *c)
#define MMIO_RWMODE   0x0003u
#define MMIO_CREATE   0x1000u
#define MMIO_ALLOCBUF 0x10000u
#define MMIOERR_CANNOTOPEN 257u
#define FOURCC_MEM MMIO_FOURCC('M', 'E', 'M', ' ')
// MMIOINFO offsets
#define MI_FLAGS 0
#define MI_IOPROC 4
#define MI_ERR 12
#define MI_CCH 20
#define MI_BUF 24
#define MI_NEXT 28
#define MI_ENDREAD 32
#define MI_ENDWRITE 36
#define MI_DISK 44
#define MI_HMMIO 68

typedef struct { MmioStream s; uint32_t flags, buf, cap, guest_mem; } Mmio;   // guest_mem: guest address of a memory file
static Mmio *mmio_of(uint32_t h) { return h_get(h, H_MMIO); }

W(mmioOpenA) {   // (name, MMIOINFO * or NULL, flags)
    HOSTFN;
    uint32_t info = ARG(2), flags = ARG(3), rw = flags & MMIO_RWMODE;
    Mmio *m = calloc(1, sizeof *m);
    if (!m) { if (info) W16(info + MI_ERR, MMIOERR_CANNOTOPEN); STDRET(3, 0); }
    m->flags = flags;
    if (!ARG(1) && info && R32(info + MI_IOPROC) == FOURCC_MEM) {   // memory file: the caller's buffer
        m->guest_mem = R32(info + MI_BUF);
        m->s.mem = mb + m->guest_mem;
        m->s.size = R32(info + MI_CCH);
        if (!m->guest_mem || (uint64_t)m->guest_mem + m->s.size > g_mem_size) {
            free(m);
            W16(info + MI_ERR, MMIOERR_CANNOTOPEN);
            STDRET(3, 0);
        }
    } else {
        char hp[600];
        w32_host_path(GSTR(ARG(1)), hp, sizeof hp);
        m->s.f = fopen(hp, (flags & MMIO_CREATE) ? "w+b" : rw ? "r+b" : "rb");
        rt_log("mmioOpenA(\"%s\", %x) -> %s\n", GSTR(ARG(1)) ? GSTR(ARG(1)) : "(null)", flags, m->s.f ? "ok" : "failed");
        if (!m->s.f) { free(m); if (info) W16(info + MI_ERR, MMIOERR_CANNOTOPEN); STDRET(3, 0); }
        if ((flags & MMIO_ALLOCBUF) || (info && R32(info + MI_CCH))) {
            m->cap = info && R32(info + MI_CCH) ? R32(info + MI_CCH) : 8192;
            m->buf = gheap_alloc(m->cap);
        }
    }
    if (info) W16(info + MI_ERR, 0);
    STDRET(3, h_new(H_MMIO, m));
}
W(mmioClose) {
    HOSTFN;
    Mmio *m = mmio_of(ARG(1));
    if (!m) STDRET(2, 0);
    if (m->s.f) fclose(m->s.f);
    if (m->buf) gheap_free(m->buf);
    free(m);
    h_close(ARG(1));
    STDRET(2, 0);
}
W(mmioRead) {
    HOSTFN;
    Mmio *m = mmio_of(ARG(1));
    if (!m || (int32_t)ARG(3) < 0) STDRET(3, 0xffffffffu);
    STDRET(3, (uint32_t)ms_read(&m->s, mb + ARG(2), ARG(3)));
}
W(mmioWrite) {
    HOSTFN;
    Mmio *m = mmio_of(ARG(1));
    if (!m || (int32_t)ARG(3) < 0) STDRET(3, 0xffffffffu);
    STDRET(3, (uint32_t)ms_write(&m->s, mb + ARG(2), ARG(3)));
}
W(mmioSeek) {   // (h, offset, origin SEEK_SET/CUR/END) -> new position or -1
    HOSTFN;
    Mmio *m = mmio_of(ARG(1));
    if (!m || ms_seek(&m->s, (long)(int32_t)ARG(2), ARG(3) == 1 ? SEEK_CUR : ARG(3) == 2 ? SEEK_END : SEEK_SET)) STDRET(3, 0xffffffffu);
    STDRET(3, (uint32_t)ms_tell(&m->s));
}
W(mmioDescend) {   // (h, MMCKINFO *, parent MMCKINFO * or NULL, flags)
    HOSTFN;
    Mmio *m = mmio_of(ARG(1));
    if (!m) STDRET(4, MMIOERR_CANNOTSEEK);
    uint32_t ck[5], parent[5];
    memcpy(ck, mb + ARG(2), sizeof ck);
    if (ARG(3)) memcpy(parent, mb + ARG(3), sizeof parent);
    uint32_t r = mmio_descend(&m->s, ck, ARG(3) ? parent : NULL, ARG(4));
    if (!r) memcpy(mb + ARG(2), ck, sizeof ck);
    STDRET(4, r);
}
W(mmioAscend) {   // (h, MMCKINFO *, flags)
    HOSTFN;
    Mmio *m = mmio_of(ARG(1));
    if (!m) STDRET(3, MMIOERR_CANNOTSEEK);
    uint32_t ck[5];
    memcpy(ck, mb + ARG(2), sizeof ck);
    uint32_t r = mmio_ascend(&m->s, ck);
    memcpy(mb + ARG(2), ck, sizeof ck);
    STDRET(3, r);
}
W(mmioCreateChunk) {   // (h, MMCKINFO *, flags MMIO_CREATERIFF 0x20 / MMIO_CREATELIST 0x40)
    HOSTFN;
    Mmio *m = mmio_of(ARG(1));
    if (!m) STDRET(3, MMIOERR_CANNOTSEEK);
    uint32_t ck[5];
    memcpy(ck, mb + ARG(2), sizeof ck);
    if (ARG(3) & MMIO_FINDRIFF) ck[0] = MMIO_FOURCC('R', 'I', 'F', 'F');
    if (ARG(3) & MMIO_FINDLIST) ck[0] = MMIO_FOURCC('L', 'I', 'S', 'T');
    ms_write(&m->s, &ck[0], 4);
    ms_write(&m->s, &ck[1], 4);
    ck[3] = (uint32_t)ms_tell(&m->s);
    if (ARG(3) & (MMIO_FINDRIFF | MMIO_FINDLIST)) ms_write(&m->s, &ck[2], 4);
    ck[4] = MMIO_DIRTY;
    memcpy(mb + ARG(2), ck, sizeof ck);
    STDRET(3, 0);
}
W(mmioGetInfo) {   // (h, MMIOINFO *, flags)
    HOSTFN;
    Mmio *m = mmio_of(ARG(1));
    if (!m) STDRET(3, MMIOERR_CANNOTSEEK);
    uint32_t info = ARG(2);
    memset(mb + info, 0, 72);
    W32(info + MI_FLAGS, m->flags);
    W32(info + MI_HMMIO, ARG(1));
    if (m->guest_mem) {   // memory file: the whole file is the buffer, the current position is pchNext
        W32(info + MI_IOPROC, FOURCC_MEM);
        W32(info + MI_CCH, m->s.size);
        W32(info + MI_BUF, m->guest_mem);
        W32(info + MI_NEXT, m->guest_mem + m->s.pos);
        W32(info + MI_ENDREAD, m->guest_mem + m->s.size);
        W32(info + MI_ENDWRITE, m->guest_mem + m->s.size);
        STDRET(3, 0);
    }
    if (!m->buf) { m->cap = 8192; m->buf = gheap_alloc(m->cap); }   // host file: an empty buffer at the current position
    W32(info + MI_CCH, m->cap);
    W32(info + MI_BUF, m->buf); W32(info + MI_NEXT, m->buf); W32(info + MI_ENDREAD, m->buf);
    W32(info + MI_ENDWRITE, m->buf + m->cap);
    W32(info + MI_DISK, (uint32_t)ms_tell(&m->s));
    STDRET(3, 0);
}
W(mmioAdvance) {   // (h, MMIOINFO *, MMIO_READ 0 / MMIO_WRITE 1)
    HOSTFN;
    Mmio *m = mmio_of(ARG(1));
    uint32_t info = ARG(2);
    if (!m || !info) STDRET(3, MMIOERR_CANNOTSEEK);
    if (m->guest_mem) {   // nothing beyond the memory block: keep the buffer as is
        m->s.pos = R32(info + MI_NEXT) - m->guest_mem;
        STDRET(3, 0);
    }
    uint32_t buf = R32(info + MI_BUF), next = R32(info + MI_NEXT);
    if (ARG(3) & 1) {
        if (next > buf) ms_write(&m->s, mb + buf, next - buf);
        W32(info + MI_NEXT, buf);
        W32(info + MI_ENDWRITE, buf + m->cap);
    } else {
        size_t got = ms_read(&m->s, mb + buf, m->cap);
        W32(info + MI_NEXT, buf);
        W32(info + MI_ENDREAD, buf + (uint32_t)got);
    }
    W32(info + MI_DISK, (uint32_t)ms_tell(&m->s));
    STDRET(3, 0);
}
W(mmioSetInfo) {   // (h, MMIOINFO *, flags): sync the stream with what the game consumed
    HOSTFN;
    Mmio *m = mmio_of(ARG(1));
    uint32_t info = ARG(2);
    if (!m || !info) STDRET(3, MMIOERR_CANNOTSEEK);
    uint32_t next = R32(info + MI_NEXT), end = R32(info + MI_ENDREAD), buf = R32(info + MI_BUF);
    if (m->guest_mem) m->s.pos = next - m->guest_mem;
    else if ((m->flags & MMIO_RWMODE) == 0 && end > next) ms_seek(&m->s, -(long)(end - next), SEEK_CUR);
    else if ((m->flags & MMIO_RWMODE) && next > buf) ms_write(&m->s, mb + buf, next - buf);
    STDRET(3, 0);
}
