// Textures, surfaces, vertex/index buffers. Lockable data lives in guest memory;
// GL textures are (re)uploaded lazily at draw time.
#include "d3d8.h"
#include "cube_faces.h"
#include "prep.h"
#include "audit.h"
#include "dxt16.h"
#include <stdlib.h>
#ifdef __vita__
#include <psp2/gxm.h>
#include <arm_neon.h>
#include <SDL.h>
#endif

#define M(p, name) static void p##_##name(CPU *c)
static void addref(void *o) { if (o) ((ComObj *)o)->refs++; }

// ---------- formats ----------
int fmt_is_dxt(uint32_t f) { return f >= FMT_DXT1 && f <= FMT_DXT5 && (f & 0xffffff) == 0x545844; }
uint32_t fmt_bpp(uint32_t f) {
    switch (f) {
    case FMT_A8R8G8B8: case FMT_X8R8G8B8: case FMT_D24S8: case FMT_D24X8: case FMT_INDEX32: return 32;
    case FMT_R8G8B8: return 24;
    case FMT_R5G6B5: case FMT_X1R5G5B5: case FMT_A1R5G5B5: case FMT_A4R4G4B4: case FMT_A8L8:
    case FMT_D16: case FMT_INDEX16: return 16;
    case FMT_A8: case FMT_L8: case FMT_P8: return 8;
    default: return fmt_is_dxt(f) ? 0 : 32;
    }
}
static uint32_t dxt_block(uint32_t f) { return f == FMT_DXT1 ? 8 : 16; }
uint32_t fmt_pitch(uint32_t f, uint32_t w) {
    if (fmt_is_dxt(f)) return ((w + 3) / 4 ? (w + 3) / 4 : 1) * dxt_block(f);
    return w * fmt_bpp(f) / 8;
}
uint32_t fmt_level_size(uint32_t f, uint32_t w, uint32_t h) {
    if (fmt_is_dxt(f)) return fmt_pitch(f, w) * ((h + 3) / 4 ? (h + 3) / 4 : 1);
    return fmt_pitch(f, w) * h;
}

// Conversion of one level to RGBA8 (uncompressed formats).
static void to_rgba(uint32_t f, const uint8_t *src, uint8_t *dst, uint32_t n) {
    for (uint32_t i = 0; i < n; i++, dst += 4) {
        uint32_t v;
        switch (f) {
        case FMT_A8R8G8B8: dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = src[3]; src += 4; break;
        case FMT_X8R8G8B8: dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = 255; src += 4; break;
        case FMT_R8G8B8: dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = 255; src += 3; break;
        case FMT_R5G6B5:
            v = src[0] | src[1] << 8; src += 2;
            dst[0] = (v >> 11 & 31) * 255 / 31; dst[1] = (v >> 5 & 63) * 255 / 63; dst[2] = (v & 31) * 255 / 31; dst[3] = 255;
            break;
        case FMT_X1R5G5B5: case FMT_A1R5G5B5:
            v = src[0] | src[1] << 8; src += 2;
            dst[0] = (v >> 10 & 31) * 255 / 31; dst[1] = (v >> 5 & 31) * 255 / 31; dst[2] = (v & 31) * 255 / 31;
            dst[3] = f == FMT_X1R5G5B5 || (v & 0x8000) ? 255 : 0;
            break;
        case FMT_A4R4G4B4:
            v = src[0] | src[1] << 8; src += 2;
            dst[0] = (v >> 8 & 15) * 17; dst[1] = (v >> 4 & 15) * 17; dst[2] = (v & 15) * 17; dst[3] = (v >> 12) * 17;
            break;
        case FMT_A8: dst[0] = dst[1] = dst[2] = 0; dst[3] = src[0]; src += 1; break;
        case FMT_L8: dst[0] = dst[1] = dst[2] = src[0]; dst[3] = 255; src += 1; break;
        case FMT_A8L8: dst[0] = dst[1] = dst[2] = src[0]; dst[3] = src[1]; src += 2; break;
        default: dst[0] = dst[1] = dst[2] = dst[3] = 255; src += fmt_bpp(f) / 8; break;
        }
    }
}

// CPU decoding of one DXT1/3/5 level to RGBA8 (fallback if the Vita's hardware DXT is wrong).
static void dxt_to_rgba(uint32_t f, const uint8_t *src, uint8_t *dst, uint32_t w, uint32_t h) {
    uint32_t bs = dxt_block(f);
    for (uint32_t by = 0; by < (h + 3) / 4; by++)
        for (uint32_t bx = 0; bx < (w + 3) / 4; bx++, src += bs) {
            const uint8_t *cb = src + (bs == 8 ? 0 : 8);
            uint32_t c0 = cb[0] | cb[1] << 8, c1 = cb[2] | cb[3] << 8, idx = cb[4] | cb[5] << 8 | cb[6] << 16 | (uint32_t)cb[7] << 24;
            uint8_t pal[4][4];
            uint32_t cc[2] = {c0, c1};
            for (int k = 0; k < 2; k++) {
                pal[k][0] = (cc[k] >> 11 & 31) * 255 / 31; pal[k][1] = (cc[k] >> 5 & 63) * 255 / 63; pal[k][2] = (cc[k] & 31) * 255 / 31; pal[k][3] = 255;
            }
            for (int k = 0; k < 3; k++) {
                if (c0 > c1 || bs == 16) { pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3; pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3; }
                else { pal[2][k] = (pal[0][k] + pal[1][k]) / 2; pal[3][k] = 0; }
            }
            pal[2][3] = 255; pal[3][3] = (c0 > c1 || bs == 16) ? 255 : 0;
            uint8_t al[16];
            if (f == FMT_DXT1) for (int i = 0; i < 16; i++) al[i] = 255;
            else if (f == FMT_DXT5) {
                uint8_t a0 = src[0], a1 = src[1], ap[8];
                uint64_t bits = 0; for (int i = 0; i < 6; i++) bits |= (uint64_t)src[2 + i] << (8 * i);
                ap[0] = a0; ap[1] = a1;
                int nn = a0 > a1 ? 7 : 5;
                for (int k = 1; k < nn; k++) ap[1 + k] = (uint8_t)(((nn - k) * a0 + k * a1) / nn);
                if (a0 <= a1) { ap[6] = 0; ap[7] = 255; }
                for (int i = 0; i < 16; i++) al[i] = ap[bits >> (3 * i) & 7];
            } else for (int i = 0; i < 16; i++) al[i] = (src[i >> 1] >> (4 * (i & 1)) & 15) * 17;
            for (uint32_t y = 0; y < 4; y++)
                for (uint32_t x = 0; x < 4; x++) {
                    uint32_t px = bx * 4 + x, py = by * 4 + y;
                    if (px >= w || py >= h) continue;
                    uint8_t *o = dst + 4 * (py * w + px);
                    const uint8_t *p = pal[idx >> (2 * (y * 4 + x)) & 3];
                    o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; o[3] = f == FMT_DXT1 ? p[3] : al[y * 4 + x];
                }
        }
}

static GLenum dxt_gl(uint32_t f) {
    return f == FMT_DXT1 ? 0x83F1 : (f == FMT_DXT2 || f == FMT_DXT3) ? 0x83F2 : 0x83F3;
}

// ---------- surfaces ----------
Surface *surf_create(uint32_t w, uint32_t h, uint32_t fmt, int depth) {
    Surface *s = calloc(1, sizeof *s);
    s->w = w; s->h = h; s->fmt = fmt; s->is_depth = depth;
    return s;
}

uint64_t g_mem_tex, g_mem_vb, g_mem_ds;
// Textures made and destroyed since the start, logged with [mem] (memory over hours of play: textures of the rooms
// left behind must go away, or the guest heap runs out after a few area changes).
uint32_t g_tex_created, g_tex_freed;
// A release is consumed by the render thread before its guest allocation can be
// returned. A scene change may allocate the next room before that queue drains.
// Synchronize only on allocation failure, so normal rendering pays no cost.
static uint32_t guest_calloc_reclaim(uint32_t size) {
    uint32_t mem = gheap_calloc(size);
    if (!mem) {
        extern uint64_t g_heap_used;
        rt_log("[heap] reclaim before retry: used=%llu KB tex=%llu KB vb=%llu KB audio=%llu KB\n",
               (unsigned long long)(g_heap_used >> 10), (unsigned long long)(g_mem_tex >> 10),
               (unsigned long long)(g_mem_vb >> 10), (unsigned long long)(g_mem_ds >> 10));
        prep_wait_all();
        prep_gdrain();
        mem = gheap_calloc(size);
        rt_log("[heap] reclaim retry %s (%u bytes)\n", mem ? "succeeded" : "failed", size);
    }
    return mem;
}
static uint32_t surf_mem(Surface *s) {
    if (s->pending) surf_flush_pending(s);
    if (!s->mem) {
        uint32_t size = fmt_level_size(s->fmt, s->w, s->h);
        s->mem = guest_calloc_reclaim(size);
        if (s->mem) g_mem_tex += size;
    }
    return s->mem;
}

M(surf, QueryInterface) { STDRET(3, 0x80004002u); }
// A level surface shares its texture's counter (as in D3D8).
static ComObj *owner(ComObj *o) {
    return o && o->kind == K_SURFACE && ((Surface *)o)->tex ? &((Surface *)o)->tex->com : o;
}
void d3d_addref(void *p) { ComObj *o = owner(p); if (o) o->refs++; }
M(surf, AddRef) { HOSTFN; ComObj *o = owner(com_get(ARG(1))); STDRET(1, o ? ++o->refs : 0); }
M(surf, Release) {
    HOSTFN;
    ComObj *o = owner(com_get(ARG(1)));
    if (!o) STDRET(1, 0);
    int32_t r = o->refs - 1;
    d3d_release(o);
    STDRET(1, r < 0 ? 0 : (uint32_t)r);
}
M(surf, GetDevice) { HOSTFN; W32(ARG(2), g_dev ? g_dev->com.guest : 0); if (g_dev) g_dev->com.refs++; STDRET(2, HR_OK); }
M(surf, SetPrivateData) { STDRET(5, HR_OK); }
M(surf, GetPrivateData) { STDRET(4, 0x88760866u); }
M(surf, FreePrivateData) { STDRET(2, HR_OK); }
M(surf, GetContainer) {
    HOSTFN; THIS(Surface, K_SURFACE);
    if (self && self->tex) { addref(self->tex); W32(ARG(3), self->tex->com.guest); STDRET(3, HR_OK); }
    W32(ARG(3), 0);
    STDRET(3, D3DERR_INVALIDCALL);
}
static void write_desc(uint32_t p, uint32_t fmt, uint32_t type, uint32_t usage, uint32_t pool, uint32_t w, uint32_t h) {
    HOSTFN;
    W32(p, fmt); W32(p + 4, type); W32(p + 8, usage); W32(p + 12, pool);
    W32(p + 16, fmt_level_size(fmt, w, h)); W32(p + 20, 0); W32(p + 24, w); W32(p + 28, h);
}
M(surf, GetDesc) {
    HOSTFN; THIS(Surface, K_SURFACE);
    write_desc(ARG(2), self->fmt, 1, self->usage, self->pool, self->w, self->h);
    STDRET(2, HR_OK);
}
// Guest memory exhausted: D3D error (OUTOFVIDEOMEMORY) rather than a null pointer written by the game.
static uint32_t lock_rect(uint32_t mem, uint32_t fmt, uint32_t w, uint32_t locked, uint32_t rect) {
    HOSTFN;
    if (!mem) { W32(locked, 0); W32(locked + 4, 0); return 0x8876017Cu; }
    uint32_t pitch = fmt_pitch(fmt, w), off = 0;
    if (rect) {
        uint32_t x = R32(rect), y = R32(rect + 4);
        off = fmt_is_dxt(fmt) ? (y / 4) * pitch + (x / 4) * dxt_block(fmt) : y * pitch + x * fmt_bpp(fmt) / 8;
    }
    W32(locked, pitch);
    W32(locked + 4, mem + off);
    return HR_OK;
}
void surf_readback(Surface *s);
M(surf, LockRect) {
    HOSTFN; THIS(Surface, K_SURFACE);
    HOST_TIMED(g_t_lock, g_n_lock);
    static int trace_copy = -1;
    if (trace_copy < 0) trace_copy = getenv("SH3_TRACE_COPY") != NULL;
    if (trace_copy && (self->w >= 512 || self->w * self->h <= 16)) { extern unsigned g_frame; rt_log("[lock] frame %u surf %p %ux%u tex=%d usage=%u gl=%d\n", g_frame, (void *)self, self->w, self->h, self->tex != NULL, self->usage, surf_is_gl(self)); }
    {   // SH3_TRACE_LOCK=1: every surface lock with its size, kind and caller
        static int on = -1; static unsigned n;
        if (on < 0) on = getenv("SH3_TRACE_LOCK") != NULL;
        if (on && n++ < 3000) { extern unsigned g_frame; rt_log("[lock-all] frame %u %ux%u fmt %u tex=%d usage=%u gl=%d bb=%d caller %08x\n", g_frame, self->w, self->h, self->fmt, self->tex != NULL, self->usage, surf_is_gl(self), self->is_backbuffer, R32(c->esp)); }
    }
    if (self->tex) {
        Texture *t = self->tex;
        prep_wait_use(t->use);
        uint32_t lw = t->w >> self->level ? t->w >> self->level : 1;
        STDRET(4, lock_rect(surf_mem(self), t->fmt, lw, ARG(2), ARG(3)));
    } else {
        if (self->pending) surf_flush_pending(self);
        if (self->is_backbuffer || (self->usage & 1)) surf_readback(self);
        STDRET(4, lock_rect(surf_mem(self), self->fmt, self->w, ARG(2), ARG(3)));
    }
}
M(surf, UnlockRect) {
    HOSTFN; THIS(Surface, K_SURFACE);
    if (self->tex) __atomic_fetch_or(&self->tex->dirty, 1u << self->level, __ATOMIC_RELAXED);
    STDRET(1, HR_OK);
}
const gfn surf_vt[] = {surf_QueryInterface, surf_AddRef, surf_Release, surf_GetDevice, surf_SetPrivateData,
                       surf_GetPrivateData, surf_FreePrivateData, surf_GetContainer, surf_GetDesc, surf_LockRect,
                       surf_UnlockRect};

uint32_t surf_wrap(Surface *s) { return com_wrap(&s->com, K_SURFACE, surf_vt, 11, "IDirect3DSurface8"); }

// ---------- textures ----------
Texture *tex_create(uint32_t w, uint32_t h, uint32_t levels, uint32_t usage, uint32_t fmt, uint32_t pool);
M(tex, QueryInterface) { STDRET(3, 0x80004002u); }
M(tex, GetType) { STDRET(1, 3); }
M(tex, SetPriority) { STDRET(2, 0); }
M(tex, GetPriority) { STDRET(1, 0); }
M(tex, PreLoad) { STDRET(1, 0); }
M(tex, SetLOD) { STDRET(2, 0); }
M(tex, GetLOD) { STDRET(1, 0); }
M(tex, GetLevelCount) { HOSTFN; THIS(Texture, K_TEXTURE); STDRET(1, self->levels); }
M(tex, GetLevelDesc) {
    HOSTFN; THIS(Texture, K_TEXTURE);
    uint32_t l = ARG(2);
    if (l >= self->levels) STDRET(3, D3DERR_INVALIDCALL);
    uint32_t w = self->w >> l, h = self->h >> l;
    write_desc(ARG(3), self->fmt, 1, self->usage, self->pool, w ? w : 1, h ? h : 1);
    STDRET(3, HR_OK);
}
M(tex, GetSurfaceLevel) {
    HOSTFN; THIS(Texture, K_TEXTURE);
    uint32_t l = ARG(2);
    if (l >= self->levels) STDRET(3, D3DERR_INVALIDCALL);
    addref(self);
    W32(ARG(3), self->lv[l]->com.guest);
    STDRET(3, HR_OK);
}
// Small dynamic textures rewritten every frame (film grain, noise: 128x128, written by the game at 0x4361d8 and
// 0x436470) used to make each LockRect wait for the render thread to finish the previous frame's draws, because the
// upload read the guest memory at bind time: 30-48 ms per frame on the Vita. Like a driver, UnlockRect now
// copies such a level and queues its upload in order; the render thread never reads their guest memory, so LockRect
// does not wait.
#define SNAP_MAX_BYTES (128u << 10)
static const uint8_t *s_up_src;   // snapshot of the level to upload instead of the guest memory (tex_upload)
static int snap_eligible(const Texture *t, uint32_t level) {
    // Opt-in (SH3_TEXSNAP=1) while is open: the VPK that enabled it by default showed a zoomed, flickering
    // picture and white logos on the Vita (not reproduced on the PC, even with the threaded prep mode).
    static int off = -1;
    if (off < 0) off = getenv("SH3_TEXSNAP") == NULL;
    return !off && !t->cube && !(t->usage & 1) && t->levels == 1 && level == 0 && !t->movie &&
           fmt_level_size(t->fmt, t->w, t->h) <= SNAP_MAX_BYTES;
}
typedef struct { Texture *t; uint8_t *data; } SnapUp;
static void tex_snap_upload(void *a) {   // render thread
    SnapUp *u = a;
    Texture *t = u->t;
    if (!t->gl) tex_gl(t);   // first use: creates the GL texture (the dirty bits it sets are cleared below)
    __atomic_store_n(&t->dirty, 0, __ATOMIC_RELAXED);
    glBindTexture(GL_TEXTURE_2D, t->gl);
    s_up_src = u->data;
    tex_upload(t, 0);
    s_up_src = NULL;
    glBindTexture(GL_TEXTURE_2D, 0);   // the draws bind their own textures; leave no stale binding behind
    free(u->data);
    free(u);
}
M(tex, LockRect) {
    HOSTFN; THIS(Texture, K_TEXTURE);
    HOST_TIMED(g_t_lock_tex, g_n_lock_tex);
    {   // SH3_TRACE_LOCK=1 (see surf_LockRect)
        static int on = -1; static unsigned n;
        if (on < 0) on = getenv("SH3_TRACE_LOCK") != NULL;
        if (on && n++ < 3000) { extern unsigned g_frame; rt_log("[lock-tex] frame %u %ux%u fmt %u level %u usage %u pool %u flags %x caller %08x\n", g_frame, self->w, self->h, self->fmt, ARG(2), self->usage, self->pool, ARG(4), R32(c->esp)); }
    }
    uint32_t l = ARG(2);
    if (l >= self->levels) STDRET(5, D3DERR_INVALIDCALL);
    uint32_t w = self->w >> l;
    if (!self->snap) prep_wait_use(self->use);   // snapshot textures: the render thread never reads this memory
    STDRET(5, lock_rect(surf_mem(self->lv[l]), self->fmt, w ? w : 1, ARG(3), ARG(4)));
}
M(tex, UnlockRect) {
    HOSTFN; THIS(Texture, K_TEXTURE);
    uint32_t l = ARG(2);
    if (snap_eligible(self, l) && self->lv[0]->mem) {
        uint32_t size = fmt_level_size(self->fmt, self->w, self->h);
        SnapUp *u = malloc(sizeof *u);
        uint8_t *data = u ? malloc(size) : NULL;
        if (data) {
            memcpy(data, g_mem + self->lv[0]->mem, size);
            u->t = self; u->data = data;
            if (!self->snap) rt_log("[tex] snapshot uploads: %ux%u fmt %u\n", self->w, self->h, self->fmt);
            self->snap = 1;
            prep_post(tex_snap_upload, u);
            STDRET(2, HR_OK);
        }
        free(u);
    }
    if (l < 32) __atomic_fetch_or(&self->dirty, 1u << l, __ATOMIC_RELAXED);
    STDRET(2, HR_OK);
}
// The game writes the movie frame through the LockRect pointer and only signals AddDirtyRect (never UnlockRect).
M(tex, AddDirtyRect) {
    HOSTFN; THIS(Texture, K_TEXTURE);
    __atomic_store_n(&self->dirty, 0xffffffffu, __ATOMIC_RELAXED); STDRET(2, HR_OK); }
const gfn tex_vt[] = {tex_QueryInterface, surf_AddRef, surf_Release, surf_GetDevice, surf_SetPrivateData,
                      surf_GetPrivateData, surf_FreePrivateData, tex_SetPriority, tex_GetPriority, tex_PreLoad,
                      tex_GetType, tex_SetLOD, tex_GetLOD, tex_GetLevelCount, tex_GetLevelDesc, tex_GetSurfaceLevel,
                      tex_LockRect, tex_UnlockRect, tex_AddDirtyRect};

Texture *tex_create(uint32_t w, uint32_t h, uint32_t levels, uint32_t usage, uint32_t fmt, uint32_t pool) {
    if (!w || !h) return NULL;
    uint32_t maxl = 1;
    while ((w >> maxl) || (h >> maxl)) maxl++;
    if (!levels || levels > maxl) levels = maxl;
    Texture *t = calloc(1, sizeof *t);
    t->w = w; t->h = h; t->levels = levels; t->usage = usage; t->fmt = fmt; t->pool = pool;
    t->lv = calloc(levels, sizeof *t->lv);
    for (uint32_t l = 0; l < levels; l++) {
        uint32_t lw = w >> l, lh = h >> l;
        Surface *s = surf_create(lw ? lw : 1, lh ? lh : 1, fmt, 0);
        s->tex = t;
        s->level = (int)l;
        s->usage = usage;
        surf_wrap(s);
        s->com.refs = 0; // owned by the texture
        t->lv[l] = s;
    }
    if (usage & 1) t->dirty = 1; // RENDERTARGET: GL storage to create
    com_wrap(&t->com, K_TEXTURE, tex_vt, 19, "IDirect3DTexture8");
    g_tex_created++;
    return t;
}

// ---------- cube textures ----------
// Same Texture object (kind K_TEXTURE, so SetTexture accepts it) with cube = 1 and 6 mip chains in lv.
static Surface *cube_surf(Texture *t, uint32_t face, uint32_t level) {
    return cube_face_valid(face) && level < t->levels ? t->lv[cube_surface_index(face, level, t->levels)] : NULL;
}
M(cube, GetType) { STDRET(1, 5); }   // D3DRTYPE_CUBETEXTURE
M(cube, GetCubeMapSurface) {   // (face, level, IDirect3DSurface8 **)
    HOSTFN; THIS(Texture, K_TEXTURE);
    Surface *s = cube_surf(self, ARG(2), ARG(3));
    if (!s) STDRET(4, D3DERR_INVALIDCALL);
    addref(self);
    W32(ARG(4), s->com.guest);
    STDRET(4, HR_OK);
}
M(cube, LockRect) {   // (face, level, D3DLOCKED_RECT *, RECT *, flags)
    HOSTFN; THIS(Texture, K_TEXTURE);
    Surface *s = cube_surf(self, ARG(2), ARG(3));
    if (!s) STDRET(6, D3DERR_INVALIDCALL);
    prep_wait_use(self->use);
    STDRET(6, lock_rect(surf_mem(s), self->fmt, s->w, ARG(4), ARG(5)));
}
M(cube, UnlockRect) { HOSTFN; THIS(Texture, K_TEXTURE); __atomic_fetch_or(&self->dirty, 1u, __ATOMIC_RELAXED); STDRET(3, HR_OK); }
M(cube, AddDirtyRect) { HOSTFN; THIS(Texture, K_TEXTURE); __atomic_fetch_or(&self->dirty, 1u, __ATOMIC_RELAXED); STDRET(3, HR_OK); }
static const gfn cube_vt[] = {tex_QueryInterface, surf_AddRef, surf_Release, surf_GetDevice, surf_SetPrivateData,
                              surf_GetPrivateData, surf_FreePrivateData, tex_SetPriority, tex_GetPriority, tex_PreLoad,
                              cube_GetType, tex_SetLOD, tex_GetLOD, tex_GetLevelCount, tex_GetLevelDesc, cube_GetCubeMapSurface,
                              cube_LockRect, cube_UnlockRect, cube_AddDirtyRect};

Texture *cube_create(uint32_t edge, uint32_t levels, uint32_t usage, uint32_t fmt, uint32_t pool) {
    if (!edge) return NULL;
    uint32_t maxl = 1;
    while (edge >> maxl) maxl++;
    if (!levels || levels > maxl) levels = maxl;
    Texture *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->cube = 1;
    t->w = t->h = edge; t->levels = levels; t->usage = usage; t->fmt = fmt; t->pool = pool;
    t->lv = calloc(CUBE_FACES * levels, sizeof *t->lv);
    if (!t->lv) { free(t); return NULL; }
    for (uint32_t f = 0; f < CUBE_FACES; f++)
        for (uint32_t l = 0; l < levels; l++) {
            uint32_t e = edge >> l;
            Surface *s = surf_create(e ? e : 1, e ? e : 1, fmt, 0);
            s->tex = t;
            s->level = (int)l;
            s->usage = usage;
            surf_wrap(s);
            s->com.refs = 0;   // owned by the cube texture
            t->lv[cube_surface_index(f, l, levels)] = s;
        }
    com_wrap(&t->com, K_TEXTURE, cube_vt, 19, "IDirect3DCubeTexture8");
    g_tex_created++;
    rt_log("[cube] create %u px, %u levels, fmt=%u usage=%u\n", edge, levels, fmt, usage);
    return t;
}

// Upload destination: GL_TEXTURE_2D, or one cube face (set by tex_gl around tex_upload; render thread only).
static GLenum s_up_target = GL_TEXTURE_2D;
static uint32_t s_up_face;
GLuint tex_gl(Texture *t) {
    if (!t->gl) {
        rt_log("[rt] tex_gl %ux%u lv=%u usage=%u fmt=%u\n", t->w, t->h, t->levels, t->usage, t->fmt);
        glGenTextures(1, &t->gl);
        glBindTexture(tex_target(t), t->gl);
#ifndef __vita__
        glTexParameteri(tex_target(t), GL_TEXTURE_MAX_LEVEL, (GLint)t->levels - 1);
#ifdef SH3_PC_VITA_TEX
        unsigned limit = getenv("SH3_TEXMAX") ? (unsigned)atoi(getenv("SH3_TEXMAX")) : 256;
        unsigned skip = 0;
        while (skip + 1 < t->levels && ((t->w >> skip) > limit || (t->h >> skip) > limit)) skip++;
        glTexParameteri(tex_target(t), GL_TEXTURE_MAX_LEVEL, (GLint)(t->levels - skip - 1));
#endif
#endif
        if ((t->usage & 1) && t->cube)
            rt_log("[cube] WARN: render-target cube texture %u px: GL storage not created\n", t->w);
        else if (t->usage & 1)
            { unsigned sh = rt_shift(t->lv[0]); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)(t->w >> sh), (GLsizei)(t->h >> sh), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL); }
        if (!(t->usage & 1)) __atomic_fetch_or(&t->dirty, 0xffffffffu, __ATOMIC_RELAXED);
    }
    if (__atomic_load_n(&t->dirty, __ATOMIC_RELAXED) && !(t->usage & 1)) {
        // ponytail: movie texture recognised by its size during playback (single level, not a render target);
        // it may be unlocked several times per presented frame: only one upload per frame.
        // The game's texture is the frame size rounded up to powers of 2.
        t->movie = g_movie_w && t->levels == 1 && t->w >= (uint32_t)g_movie_w && t->h >= (uint32_t)g_movie_h &&
                   t->w < 2u * (uint32_t)g_movie_w && t->h < 2u * (uint32_t)g_movie_h;
        if (t->movie && t->up_frame == g_pframe + 1) return t->gl;
        t->up_frame = g_pframe + 1;
        uint32_t dirty = __atomic_exchange_n(&t->dirty, 0, __ATOMIC_ACQ_REL);
#ifdef __vita__
        // vitaGL re-allocates the storage when level 0 is re-specified and the other mips are lost (texture shown empty): all levels or none.
        if (t->levels > 1 && !t->movie) dirty = 0xffffffffu;
#endif
        glBindTexture(tex_target(t), t->gl);
        extern uint64_t g_t_tex;
        uint64_t tu0 = SDL_GetPerformanceCounter();
        if (t->cube) {   // any dirty level re-uploads every face and level (cube maps are small)
            for (uint32_t f = 0; f < CUBE_FACES; f++) {
                s_up_target = cube_gl_target(f);
                s_up_face = f;
                for (uint32_t l = 0; l < t->levels; l++) tex_upload(t, (int)l);
            }
            s_up_target = GL_TEXTURE_2D;
            s_up_face = 0;
        } else {
            for (uint32_t l = 0; l < t->levels; l++)
                if (dirty & (1u << (l < 31 ? l : 31))) tex_upload(t, (int)l);
        }
        g_t_tex += SDL_GetPerformanceCounter() - tu0;
    }
    return t->gl;
}

static void free_p(void *p) { free(*(void **)p); }

#ifdef __vita__
// 32-bit textures (A8R8G8B8 / X8R8G8B8: bytes B G R A) straight to the Vita's storage formats with NEON, 8 pixels at a
// time ( a cutscene cut uploads ~13 textures of 512x512 through the scalar to_rgba + dither loops,
// 150-480 ms in one frame). Same results as the generic path: 4x4 ordered dither before the 16-bit packing.
static const uint8_t argb_bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static int argb_opaque(const uint8_t *src, size_t n) {
    uint8x8_t m = vdup_n_u8(255);
    size_t i = 0;
    for (; i + 8 <= n; i += 8) m = vand_u8(m, vld4_u8(src + 4 * i).val[3]);
    uint8_t a = 255;
    for (int k = 0; k < 8; k++) a &= vget_lane_u8(m, 0), m = vext_u8(m, m, 1);
    for (; i < n; i++) a &= src[4 * i + 3];
    return a == 255;
}
static void argb_to_565(const uint8_t *src, uint16_t *o, uint32_t w, uint32_t h) {
    for (uint32_t y = 0; y < h; y++, src += 4 * w, o += w) {
        const uint8_t *bd = argb_bayer + 4 * (y & 3);
        uint8_t pat[8] = {bd[0], bd[1], bd[2], bd[3], bd[0], bd[1], bd[2], bd[3]};
        uint8x8_t d = vld1_u8(pat), d1 = vshr_n_u8(d, 1), d2 = vshr_n_u8(d, 2);
        uint32_t x = 0;
        for (; x + 8 <= w; x += 8) {
            uint8x8x4_t p = vld4_u8(src + 4 * x);
            uint8x8_t r = vqadd_u8(p.val[2], d1), g = vqadd_u8(p.val[1], d2), b = vqadd_u8(p.val[0], d1);
            uint16x8_t v = vsriq_n_u16(vshll_n_u8(r, 8), vshll_n_u8(g, 8), 5);
            vst1q_u16(o + x, vsriq_n_u16(v, vshll_n_u8(b, 8), 11));
        }
        for (; x < w; x++) {
            unsigned dd = bd[x & 3], r = src[4 * x + 2] + (dd >> 1), g = src[4 * x + 1] + (dd >> 2), b = src[4 * x] + (dd >> 1);
            if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
            o[x] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
        }
    }
}
static void argb_to_4444(const uint8_t *src, uint16_t *o, uint32_t w, uint32_t h) {
    for (uint32_t y = 0; y < h; y++, src += 4 * w, o += w) {
        const uint8_t *bd = argb_bayer + 4 * (y & 3);
        uint8_t pat[8] = {bd[0], bd[1], bd[2], bd[3], bd[0], bd[1], bd[2], bd[3]};
        uint8x8_t d = vld1_u8(pat);
        uint32_t x = 0;
        for (; x + 8 <= w; x += 8) {
            uint8x8x4_t p = vld4_u8(src + 4 * x);
            uint8x8_t r = vqadd_u8(p.val[2], d), g = vqadd_u8(p.val[1], d), b = vqadd_u8(p.val[0], d);
            uint16x8_t v = vsriq_n_u16(vshll_n_u8(r, 8), vshll_n_u8(g, 8), 4);
            v = vsriq_n_u16(v, vshll_n_u8(b, 8), 8);
            vst1q_u16(o + x, vsriq_n_u16(v, vshll_n_u8(p.val[3], 8), 12));
        }
        for (; x < w; x++) {
            unsigned dd = bd[x & 3], r = src[4 * x + 2] + dd, g = src[4 * x + 1] + dd, b = src[4 * x] + dd, a = src[4 * x + 3];
            if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
            o[x] = (uint16_t)((r >> 4) << 12 | (g >> 4) << 8 | (b >> 4) << 4 | a >> 4);
        }
    }
}
static void argb_to_rgba(const uint8_t *src, uint8_t *o, size_t n) {
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint8x8x4_t p = vld4_u8(src + 4 * i);
        uint8x8_t b = p.val[0];
        p.val[0] = p.val[2]; p.val[2] = b;
        vst4_u8(o + 4 * i, p);
    }
    for (; i < n; i++) { o[4 * i] = src[4 * i + 2]; o[4 * i + 1] = src[4 * i + 1]; o[4 * i + 2] = src[4 * i]; o[4 * i + 3] = src[4 * i + 3]; }
}
#endif
void tex_upload(Texture *t, int l) {
    SLOW("tex_upload", t->w, l);
    Surface *s = t->lv[cube_surface_index(s_up_face, (uint32_t)l, t->levels)];
    uint32_t w = s->w, h = s->h;
    uint8_t *zbuf __attribute__((cleanup(free_p))) = NULL;   // level never locked: black (the guest heap belongs to the game thread)
    uint32_t smem = s->mem;
    if (!smem && !s_up_src) {
        zbuf = calloc(1, fmt_level_size(s->fmt, w, h));
        if (!zbuf) { rt_log("[tex] WARN: allocation failed for empty %ux%u level %d\n", w, h, l); return; }
    }
    const uint8_t *src = s_up_src ? s_up_src : smem ? g_mem + smem : zbuf;
    int gl_l = l;
#if defined(__vita__) || defined(SH3_PC_VITA_TEX)
    // ponytail: Vita VRAM is tiny: mip levels beyond SH3_TEXMAX px are not uploaded
    // (256 by default; GL level 0 becomes the first level <= max) and storage is 16-bit instead of RGBA8.
    static uint32_t texmax;
    if (!texmax) texmax = getenv("SH3_TEXMAX") ? (uint32_t)atoi(getenv("SH3_TEXMAX")) : 256;
    int skip = 0;
    while (skip < (int)t->levels - 1 && ((t->w >> skip) > texmax || (t->h >> skip) > texmax)) skip++;
    if (l < skip) return;
    gl_l = l - skip;
#ifdef SH3_PC_VITA_TEX
    if (g_audit_checks && gl_l == 0 && !smem && t->lv[0]->mem)
        rt_log("[audit-format] selected mip has no data: texture=%ux%u fmt=%u level=%d\n", t->w, t->h, t->fmt, l);
#endif
#endif
    {   // first 100 uploads only (SH3_TEXLOG=1: all): hours of play would write tens of MB of log on the memory card
        static int all = -1; static unsigned nlog;
        if (all < 0) all = getenv("SH3_TEXLOG") != NULL;
        if (!t->movie && (all || nlog++ < 100)) rt_log("[tex] upload %ux%u fmt=%u lv=%d\n", w, h, t->fmt, l);
    }
    // PC: hardware DXT (SH3_SWDXT=1 forces the CPU). Vita keeps software
    // decoding by default. SH3_HWDXT=1 is a lab option: the usual vitaGL
    // transfer swizzle corrupts bedroom textures; the isolated block-swizzle
    // backend must be paired and qualified before enabling compressed storage.
    static int hw_dxt = -1;
#ifdef __vita__
    if (hw_dxt < 0) hw_dxt = getenv("SH3_HWDXT") != NULL;
#elif defined(SH3_PC_VITA_TEX)
    // Private PC oracle: retain Vita mip/storage choices while optionally
    // asking desktop GL to sample the original compressed blocks.
    if (hw_dxt < 0) hw_dxt = getenv("SH3_HWDXT") != NULL;
#else
    if (hw_dxt < 0) hw_dxt = getenv("SH3_SWDXT") == NULL;
#endif
#ifdef __vita__
    int hw_texture=hw_dxt && sh3_dxt_hardware_eligible(t->w,t->h,(unsigned)skip);
    // Both the usual transfer and the experimental CPU swizzle address 4x4
    // blocks. Omit sub-block mips; a thin base keeps its whole chain in software.
    if (fmt_is_dxt(t->fmt) && hw_texture && (w < 4 || h < 4) && gl_l > 0) return;
    if (fmt_is_dxt(t->fmt) && hw_texture && w >= 4 && h >= 4) {
#else
    if (fmt_is_dxt(t->fmt) && hw_dxt) {
#endif
        glCompressedTexImage2D(s_up_target, gl_l, dxt_gl(t->fmt), (GLsizei)w, (GLsizei)h, 0,
                               (GLsizei)fmt_level_size(t->fmt, w, h), src);
#ifdef __vita__
        // The next level reallocates (and copies) the texture storage: without this wait, the copy reads the
        // previous levels before the GPU transfer has finished -> corrupted textures.
        sceGxmTransferFinish();
#endif
        return;
    }
#ifdef __vita__
    volatile int *stg_busy = NULL;
    uint8_t *stg = t->movie && t->fmt == FMT_A8R8G8B8 ? movie_565_take((uint32_t)g_movie_w, (uint32_t)g_movie_h, &stg_busy) : NULL;   // 565 frame already converted (movie thread)
    if (!stg && t->movie && t->fmt == FMT_A8R8G8B8 && t->stg_alloc) return;   // nothing new: the GL image stays
    if (stg) {
        uint64_t t0 = SDL_GetPerformanceCounter();
        if (!t->stg_alloc) {   // storage of the whole texture, once; the frame is its top-left part
            // vitaGL clears storage created without pixels; the frames themselves go to the render thread's own textures (qgl.c)
            glTexImage2D(s_up_target, gl_l, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            t->stg_alloc = 1;
        }
        q_tex565(t->gl, (GLsizei)w, (GLsizei)h, (GLsizei)g_movie_w, (GLsizei)g_movie_h, stg, stg_busy);   // render thread: the main thread does not wait
        static unsigned nsub;
        static double sub_ms;
        sub_ms += (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
        if (!(++nsub % 150)) { rt_log("[movie] 565 upload #%u: %.2f ms on the main thread\n", nsub, sub_ms / 150); sub_ms = 0; }
        return;
    }
    if (t->movie && t->fmt == FMT_A8R8G8B8) {   // movie: BGRA -> 565 in one pass (the decoder does not write alpha)
        static unsigned nup;
        static double conv_ms, gl_ms;
        uint16_t *o = malloc((size_t)w * h * 2);
        if (!o) return;
        uint64_t t0 = SDL_GetPerformanceCounter();
        uint32_t i = 0;
        for (; i + 8 <= w * h; i += 8, src += 32) {   // NEON: R<<8, then insert G>>5 and B>>11 -> R5G6B5
            uint8x8x4_t p = vld4_u8(src);
            uint16x8_t v = vsriq_n_u16(vshll_n_u8(p.val[2], 8), vshll_n_u8(p.val[1], 8), 5);
            vst1q_u16(o + i, vsriq_n_u16(v, vshll_n_u8(p.val[0], 8), 11));
        }
        for (; i < w * h; i++, src += 4) o[i] = (uint16_t)((src[2] >> 3) << 11 | (src[1] >> 2) << 5 | src[0] >> 3);
        uint64_t t1 = SDL_GetPerformanceCounter();
        glTexImage2D(s_up_target, gl_l, GL_RGB, (GLsizei)w, (GLsizei)h, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, o);
        free(o);
        double ms = 1000.0 / (double)SDL_GetPerformanceFrequency();
        conv_ms += (double)(t1 - t0) * ms; gl_ms += (double)(SDL_GetPerformanceCounter() - t1) * ms;
        if (!(++nup % 150)) {
            rt_log("[movie] envoi 565 n°%u (%ux%u) : conversion %.1f ms, glTexImage2D %.1f ms\n", nup, w, h, conv_ms / 150, gl_ms / 150);
            conv_ms = gl_ms = 0;
        }
        return;
    }
#endif
    // Diagnostic for deciding whether offline texture preparation is useful.
    // Disabled in normal and tester builds; times cover CPU work and the GL call,
    // not the GPU's asynchronous execution after glTexImage2D returns.
    static int prep_profile = -1;
    if (prep_profile < 0) {
        const char *e = getenv("SH3_TEX_PREP_PROFILE");
        prep_profile = e && !strcmp(e, "1");
    }
    uint64_t prep_t0 = prep_profile && fmt_is_dxt(t->fmt) ? SDL_GetPerformanceCounter() : 0;
#if defined(__vita__) || defined(SH3_PC_VITA_TEX)
    static int direct_dxt1=-1;
    if(direct_dxt1<0) {
        const char *enabled=getenv("SH3_DXT16_NATIVE"),*rgba=getenv("SH3_TEX32");
#ifdef __vita__
        direct_dxt1=(!enabled||!strcmp(enabled,"1"))&&!(rgba&&!strcmp(rgba,"1"));
#else
        direct_dxt1=enabled&&!strcmp(enabled,"1")&&!(rgba&&!strcmp(rgba,"1"));
#endif
    }
    if(direct_dxt1 && t->fmt==FMT_DXT1 && (!t->tfmt || t->tfmt==1)) {
        uint16_t *packed=malloc((size_t)w*h*2);
        if(packed) {
            sh3_dxt1_rgb565(src,packed,w,h);
            uint64_t decoded=prep_t0?SDL_GetPerformanceCounter():0;
            glTexImage2D(s_up_target,gl_l,GL_RGB,(GLsizei)w,(GLsizei)h,0,GL_RGB,GL_UNSIGNED_SHORT_5_6_5,packed);
            if(gl_l==0 || !t->tfmt)t->tfmt=1;
            if(prep_t0) {
                double k=1000.0/SDL_GetPerformanceFrequency();
                rt_log("[tex-prep-direct] frame=%u %ux%u temp_kb=%u decode_pack=%.2f call=%.2f ms\n",g_pframe,w,h,(unsigned)((size_t)w*h*2/1024),(decoded-prep_t0)*k,(SDL_GetPerformanceCounter()-decoded)*k);
            }
            free(packed);return;
        }
        // Retain the original allocation/error path if this smaller allocation fails.
    }
#endif
#ifdef __vita__
    {   // NEON path for 32-bit sources; the laboratory format options keep the generic path. SH3_TEXFAST=0 disables.
        static int fast = -1;
        if (fast < 0) {
            const char *e = getenv("SH3_TEXFAST");
            fast = (!e || atoi(e) != 0) && !getenv("SH3_TEX32") && !getenv("SH3_ALPHA16") && !getenv("SH3_ALPHA32_MAX");
        }
        if (fast && (t->fmt == FMT_A8R8G8B8 || t->fmt == FMT_X8R8G8B8)) {
            size_t n = (size_t)w * h;
            if ((gl_l == 0 && !(t->cube && s_up_face)) || !t->tfmt) {
                int small = w <= 256 && h <= 256;
                // 1: 565, 2: 4444, 3: RGBA8. A big texture with an alpha channel that is all 255 keeps its colour (565)
                // instead of losing it to 4 bits per channel.
                t->tfmt = t->fmt == FMT_X8R8G8B8 ? 1 : small ? 3 : argb_opaque(src, n) ? 1 : 2;
            }
            void *o = malloc(t->tfmt == 3 ? n * 4 : n * 2);
            if (o) {
                if (t->tfmt == 3) { argb_to_rgba(src, o, n); glTexImage2D(s_up_target, gl_l, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, o); }
                else if (t->tfmt == 2) { argb_to_4444(src, o, w, h); glTexImage2D(s_up_target, gl_l, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, o); }
                else { argb_to_565(src, o, w, h); glTexImage2D(s_up_target, gl_l, GL_RGB, (GLsizei)w, (GLsizei)h, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, o); }
                free(o);
                return;
            }
        }
    }
#endif
    uint8_t *tmp = malloc((size_t)w * h * 4);
    if (!tmp) { rt_log("[tex] WARN: malloc %u KB failed for %ux%u, texture skipped\n", (unsigned)((size_t)w * h * 4 / 1024), w, h); return; }
    if (fmt_is_dxt(t->fmt)) dxt_to_rgba(t->fmt, src, tmp, w, h);
    else to_rgba(t->fmt, src, tmp, w * h);
    uint64_t prep_t1 = prep_t0 ? SDL_GetPerformanceCounter() : 0;
    if (g_audit_checks && t->fmt == FMT_DXT1 && gl_l == 0) {
        unsigned transparent = 0;
        for (uint32_t i = 0; i < w * h; i++) transparent += tmp[4 * i + 3] != 255;
        if (transparent) rt_log("[audit-format] DXT1 transparency: texture=%ux%u level=%d pixels=%u\n", t->w, t->h, l, transparent);
    }
#if defined(__vita__) || defined(SH3_PC_VITA_TEX)
    int alpha = fmt_is_dxt(t->fmt) ? t->fmt != FMT_DXT1 : t->fmt == FMT_A8R8G8B8 || t->fmt == FMT_A1R5G5B5 || t->fmt == FMT_A4R4G4B4 || t->fmt == FMT_A8 || t->fmt == FMT_A8L8;
    if (alpha && fmt_is_dxt(t->fmt)) { uint8_t m = 255; for (uint32_t i = 0; i < w * h; i++) m &= tmp[4 * i + 3]; if (m == 255) alpha = 0; }   // alpha format but opaque: 565 keeps more colour
    // Preserve alpha precision separately from opaque texture storage. Laboratory
    // SH3_ALPHA32_MAX=512 raises only this threshold, unlike global TEX32.
    // The normal threshold remains 256; ALPHA16 still takes precedence.
    static int a16 = -1, tex32 = -1;
    static unsigned alpha32_max;
    if (!alpha32_max) {
        const char *value = getenv("SH3_ALPHA32_MAX");
        char *end = NULL;
        unsigned long limit = value ? strtoul(value, &end, 10) : 0;
        alpha32_max = value && end != value && !*end && (limit == 256 || limit == 512 || limit == 1024) ? (unsigned)limit : 256;
    }
    if (a16 < 0) a16 = getenv("SH3_ALPHA16") != NULL;
    if (tex32 < 0) { const char *e = getenv("SH3_TEX32"); tex32 = e && !strcmp(e,"1"); }
    // A cube map keeps one storage format for its 6 faces.
    if ((gl_l == 0 && !(t->cube && s_up_face)) || !t->tfmt) t->tfmt = tex32 ? 3 : !alpha ? 1 : (!a16 && w <= alpha32_max && h <= alpha32_max) ? 3 : 2;
    alpha = t->tfmt != 1;   // every level of a texture shares level 0's format
    if (t->tfmt == 3) {
        glTexImage2D(s_up_target, gl_l, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
        if (prep_t0) {
            uint64_t end = SDL_GetPerformanceCounter();
            double scale = 1000.0 / (double)SDL_GetPerformanceFrequency();
            rt_log("[tex-prep] frame=%u %ux%u fmt=%u level=%d gpu_fmt=rgba8 decode=%.2f call=%.2f ms\n",
                   g_pframe, w, h, t->fmt, gl_l, (prep_t1 - prep_t0) * scale, (end - prep_t1) * scale);
        }
        free(tmp);
        return;
    }
    static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};   // 4x4 ordered dither hides the banding of the 16-bit conversion
    uint16_t *o = (uint16_t *)tmp;   // in-place write: the write index never exceeds the read index
    for (uint32_t i = 0, y = 0; y < h; y++) {
        const uint8_t *bd = bayer + 4 * (y & 3);
        for (uint32_t x = 0; x < w; x++, i++) {
            const uint8_t *p = tmp + 4 * i;
            unsigned d = bd[x & 3], r = p[0], g = p[1], b = p[2], a = p[3];
            if (alpha) {
                r += d; g += d; b += d;
                if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
                o[i] = (uint16_t)((r >> 4) << 12 | (g >> 4) << 8 | (b >> 4) << 4 | a >> 4);
            } else {
                r += d >> 1; g += d >> 2; b += d >> 1;
                if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
                o[i] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
            }
        }
    }
    uint64_t prep_t2 = prep_t0 ? SDL_GetPerformanceCounter() : 0;
    glTexImage2D(s_up_target, gl_l, alpha ? GL_RGBA : GL_RGB, (GLsizei)w, (GLsizei)h, 0, alpha ? GL_RGBA : GL_RGB,
                 alpha ? GL_UNSIGNED_SHORT_4_4_4_4 : GL_UNSIGNED_SHORT_5_6_5, tmp);
    if (prep_t0) {
        uint64_t end = SDL_GetPerformanceCounter();
        double scale = 1000.0 / (double)SDL_GetPerformanceFrequency();
        rt_log("[tex-prep] frame=%u %ux%u fmt=%u level=%d gpu_fmt=%s decode=%.2f pack=%.2f call=%.2f ms\n",
               g_pframe, w, h, t->fmt, gl_l, alpha ? "rgba4444" : "rgb565",
               (prep_t1 - prep_t0) * scale, (prep_t2 - prep_t1) * scale, (end - prep_t2) * scale);
    }
#else
    glTexImage2D(s_up_target, gl_l, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
#endif
    free(tmp);
}

// ---------- vertex / index buffers ----------
M(buf, GetType) { HOSTFN; ComObj *o = com_get(ARG(1)); STDRET(1, o && o->kind == K_IB ? 7 : 6); }
uint32_t g_buf_ver;
// SH3: removido (SH4 native mesh bounds/UV cache, see reference/sh4/d3d8/resource_model_cache.c).

M(buf, Lock) {
    HOSTFN;
    Buffer *b = (Buffer *)com_get(ARG(1));
    prep_wait_use(b->use);
    b->ver = ++g_buf_ver;
    uint32_t off = ARG(2) < b->size ? ARG(2) : b->size, end = ARG(3) && off + ARG(3) < b->size ? off + ARG(3) : b->size;
    if (off < b->dlo || !b->dhi) b->dlo = off;   // ponytail: dirty range = union of the Locks since the last GPU upload
    if (end > b->dhi) b->dhi = end;
    if (b->com.kind == K_IB ? ++b->relocks >= 8 : b->gl && ++b->relocks >= 3) b->dyn = 1;
    W32(ARG(4), b->mem + ARG(2));
    STDRET(5, HR_OK);
}
M(buf, Unlock) {
    HOSTFN;Buffer *b=(Buffer *)com_get(ARG(1));
    b->ver=++g_buf_ver;
    STDRET(1,HR_OK);
}
M(buf, GetDesc) {
    HOSTFN;
    Buffer *b = (Buffer *)com_get(ARG(1));
    uint32_t p = ARG(2);
    int ib = b->com.kind == K_IB;
    W32(p, ib ? b->fmt_or_fvf : 100); W32(p + 4, ib ? 7 : 6); W32(p + 8, b->usage); W32(p + 12, b->pool);
    W32(p + 16, b->size);
    if (!ib) W32(p + 20, b->fmt_or_fvf);
    STDRET(2, HR_OK);
}
const gfn vb_vt[] = {tex_QueryInterface, surf_AddRef, surf_Release, surf_GetDevice, surf_SetPrivateData,
                     surf_GetPrivateData, surf_FreePrivateData, tex_SetPriority, tex_GetPriority, tex_PreLoad,
                     buf_GetType, buf_Lock, buf_Unlock, buf_GetDesc};

Buffer *buf_create(uint32_t size, uint32_t usage, uint32_t fmt, uint32_t pool, int is_index) {
    Buffer *b = calloc(1, sizeof *b);
    b->size = size; b->usage = usage; b->fmt_or_fvf = fmt; b->pool = pool;
    b->dyn = (usage & 0x200) != 0;   // D3DUSAGE_DYNAMIC
    b->ver = ++g_buf_ver;
    b->mem = guest_calloc_reclaim(size ? size : 4);
    if (!b->mem) rt_fatal("D3D buffer: guest memory exhausted (%u bytes)", size);
    g_mem_vb += size;
    com_wrap(&b->com, is_index ? K_IB : K_VB, vb_vt, 14, is_index ? "IDirect3DIndexBuffer8" : "IDirect3DVertexBuffer8");
    return b;
}

// ---------- freeing ----------
// Game thread: the refcount rules stay here; destruction (GL, host struct) is replayed by the consumer (res_free).
void d3d_release(ComObj *o) {
    if (--o->refs > 0) return;
    switch (o->kind) {
    case K_TEXTURE: {
        Texture *t = (Texture *)o;
        if (g_dev)
            for (int s = 0; s < MAX_STAGES; s++)
                if (g_dev->tex[s] == t) return; // still bound (should not happen: SetTexture does AddRef)
        prep_free(K_TEXTURE, t);
        break;
    }
    case K_SURFACE: {
        Surface *s = (Surface *)o;
        if (s->tex) { o->refs = 0; d3d_release(&s->tex->com); return; } // level: texture's counter
        if (s->is_backbuffer || (g_dev && (s == g_dev->depth))) { o->refs = 1; return; }
        if (s->pending) { d3d_release(&s->pending->com); s->pending = NULL; }
        prep_free(K_SURFACE, s);
        break;
    }
    case K_VB: case K_IB:
        prep_free(o->kind, o);
        break;
    default: break;
    }
}

// Consumer: all records issued before the FREE have been replayed. Guest frees are handed back to the game thread.
void res_free(int kind, void *p) {
    extern int g_att_stale;
    switch (kind) {
    case K_TEXTURE: {
        Texture *t = p;
        g_tex_freed++;
        if (t->gl) { glDeleteTextures(1, &t->gl); g_att_stale++; }
        for (uint32_t l = 0; l < tex_surface_count(t); l++) {
            Surface *s = t->lv[l];
            if (s->mem) g_mem_tex -= fmt_level_size(t->fmt, s->w, s->h);
            prep_gfree(s->com.guest, s->mem);
            free(s);
        }
        free(t->lv);
        prep_gfree(t->com.guest, 0);
        free(t);
        break;
    }
    case K_SURFACE: {
        Surface *s = p;
        g_att_stale++;
        if (s->rb) glDeleteRenderbuffers(1, &s->rb);
        if (s->fbo_tex) glDeleteTextures(1, &s->fbo_tex);
        prep_gfree(s->com.guest, s->mem);
        free(s);
        break;
    }
    case K_VB: case K_IB: {
        Buffer *b = p;
        g_mem_vb -= b->size;
        if (b->gl) glDeleteBuffers(1, &b->gl);
        prep_gfree(b->com.guest, b->mem);
        free(b);
        break;
    }
    default: break;
    }
}
