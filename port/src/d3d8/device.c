// IDirect3DDevice8: state, resources and render calls (the GL rendering is in draw.c).
#include "d3d8.h"
#include "prep.h"
#include "audit.h"
#include "plat.h"
#include "texel_align.h"
#include "ws_ui.h"
#include "stateblock.h"
#include <stdlib.h>

Device *g_dev;
void d3d_fill_caps(uint32_t p);
extern const gfn tex_vt[], surf_vt[], vb_vt[], ib_vt[];

#define M(name) static void dev_##name(CPU *c)
#define DEV Device *d = com_get_kind(ARG(1), K_DEVICE); if (!d) rt_fatal("device invalide %08x [%08x %08x %08x %08x] (g_dev=%p guest %08x) kind=%d obj=%p", ARG(1), R32(ARG(1)), R32(ARG(1) + 4), R32(ARG(1) + 8), R32(ARG(1) + 12), (void *)g_dev, g_dev ? g_dev->com.guest : 0, com_get(ARG(1)) ? com_get(ARG(1))->kind : -1, (void *)com_get(ARG(1)))

#define addref d3d_addref
uint64_t g_t_begin, g_t_body; // profiling: start of the current frame, accumulated body

M(QueryInterface) { STDRET(3, 0x80004002u); }
M(AddRef) { HOSTFN; DEV; STDRET(1, ++d->com.refs); }
M(Release) { HOSTFN; DEV; STDRET(1, d->com.refs > 1 ? --d->com.refs : 1); }
M(TestCooperativeLevel) { STDRET(1, HR_OK); }
M(GetAvailableTextureMem) { STDRET(1, 128u << 20); }
M(ResourceManagerDiscardBytes) { STDRET(2, HR_OK); }
M(GetDirect3D) {
    HOSTFN;
    extern void *g_d3d_obj;
    if (!g_d3d_obj) { W32(ARG(2), 0); STDRET(2, HR_FAIL); }
    d3d_addref(g_d3d_obj);
    W32(ARG(2), ((ComObj *)g_d3d_obj)->guest);
    STDRET(2, HR_OK);
}
M(GetDeviceCaps) { HOSTFN; d3d_fill_caps(ARG(2)); STDRET(2, HR_OK); }
M(GetDisplayMode) {
    HOSTFN; DEV;
    uint32_t p = ARG(2);
    W32(p, d->bb_w); W32(p + 4, d->bb_h); W32(p + 8, 60); W32(p + 12, FMT_X8R8G8B8);
    STDRET(2, HR_OK);
}
M(GetCreationParameters) {
    HOSTFN;
    uint32_t p = ARG(2);
    W32(p, 0); W32(p + 4, 1); W32(p + 8, 0x00010010u); W32(p + 12, 0x40);
    STDRET(2, HR_OK);
}
M(SetCursorProperties) { STDRET(4, HR_OK); }
M(SetCursorPosition) { STDRET(4, HR_OK); }
// bShow != 0: on-screen pointer in use -> dinput.c puts every direction on the left stick (g_cursor_seen)
M(ShowCursor) {
    HOSTFN; extern unsigned g_cursor_seen, g_frame;
    static unsigned last = 2;
    unsigned s = ARG(2) & 1;
    if (s) g_cursor_seen = g_frame + 1;
    if (s != last) { last = s; rt_log("[cursor] show=%u frame %u\n", s, g_frame); }
    STDRET(2, 0);
}
M(CreateAdditionalSwapChain) { d3d_log_once("CreateAdditionalSwapChain"); STDRET(3, D3DERR_INVALIDCALL); }
// ponytail: the backbuffer size does not change; rt_apply_res re-imposes the port's resolution (SH3: empty for now).
M(Reset) { HOSTFN; DEV; rt_log("[d3d8] Reset %ux%u\n", R32(ARG(2)), R32(ARG(2) + 4)); rt_apply_res(); prep_gfx_init(); STDRET(2, HR_OK); }
M(Present) {
    HOSTFN; DEV;
    { extern int g_ui_clip_frame; g_ui_clip_frame = 0; }
    if (g_t_begin) g_t_body += SDL_GetPerformanceCounter() - g_t_begin;
    g_t_begin = 0;
    g_frame++;
    rt_keep_res();
    extern void ws_frame_end(void);
    ws_frame_end();   // widescreen.c: per-frame state (cutscene zoom)
    { extern void tex_release_sweep(void); if (!(g_frame % 60)) tex_release_sweep(); }   // resource.c, tex_policy.h
#ifndef __vita__
    {   // SH3_PC_SLOWMS=n: every frame takes n ms more, to reproduce the Vita's frame rate on the PC
        static int slow = -1, from = 0;   // SH3_PC_SLOWFROM=frame: only from that present on (reach gameplay first)
        if (slow < 0) { slow = getenv("SH3_PC_SLOWMS") ? atoi(getenv("SH3_PC_SLOWMS")) : 0; from = getenv("SH3_PC_SLOWFROM") ? atoi(getenv("SH3_PC_SLOWFROM")) : 0; }
        if (slow > 0 && g_frame >= (unsigned)from) SDL_Delay((uint32_t)slow);
    }
#endif
    // SH3: removido (SH4 SH3_PLAY_MOVIE laboratory schedule, see reference/sh4/d3d8/device_movie_test.c).
    extern void rt_controls_present(void), console_ui_finish_frame(void);
    rt_controls_present();
    console_ui_finish_frame();
    prep_present();
    prep_gdrain();
    rt_audit_frame(g_frame);
    (void)d;
    STDRET(5, HR_OK);
}
M(GetBackBuffer) {
    HOSTFN; DEV;
    addref(d->backbuffer);
    W32(ARG(4), d->backbuffer->com.guest);
    STDRET(4, HR_OK);
}
M(GetRasterStatus) { HOSTFN; W32(ARG(2), 1); W32(ARG(2) + 4, 0); STDRET(2, HR_OK); }
M(SetGammaRamp) {
    HOSTFN; DEV;
    // The render consumer owns a separate Device, including the final-blit gamma ramp.
    if (prep_set(d->gamma, mb + ARG(3), sizeof d->gamma) && g_audit_checks)
        rt_log("[audit-gamma] red=%u/%u/%u green=%u/%u/%u blue=%u/%u/%u\n",
               d->gamma[0][0], d->gamma[0][128], d->gamma[0][255],
               d->gamma[1][0], d->gamma[1][128], d->gamma[1][255],
               d->gamma[2][0], d->gamma[2][128], d->gamma[2][255]);
    STDRET(3, 0);
}
M(GetGammaRamp) { HOSTFN; DEV; memcpy(mb + ARG(2), d->gamma, sizeof d->gamma); STDRET(2, 0); }

M(CreateTexture) {
    HOSTFN;
    static int s_first = 1;
    if (s_first) { s_first = 0; rt_log("[d3d8] first CreateTexture %ux%u fmt=%u\n", ARG(2), ARG(3), ARG(6)); }
    Texture *t = tex_create(ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7));
    if (!t) STDRET(8, D3DERR_INVALIDCALL);
    W32(ARG(8), t->com.guest);
    STDRET(8, HR_OK);
}
M(CreateVolumeTexture) { HOSTFN; d3d_log_once("CreateVolumeTexture"); W32(ARG(9), 0); STDRET(9, D3DERR_NOTAVAILABLE); }
M(CreateCubeTexture) {   // (edge, levels, usage, format, pool, IDirect3DCubeTexture8 **)
    HOSTFN;
    Texture *t = cube_create(ARG(2), ARG(3), ARG(4), ARG(5), ARG(6));
    if (!t) { W32(ARG(7), 0); STDRET(7, D3DERR_INVALIDCALL); }
    W32(ARG(7), t->com.guest);
    STDRET(7, HR_OK);
}
M(CreateVertexBuffer) {
    HOSTFN;
    Buffer *b = buf_create(ARG(2), ARG(3), ARG(4), ARG(5), 0);
    W32(ARG(6), b->com.guest);
    STDRET(6, HR_OK);
}
M(CreateIndexBuffer) {
    HOSTFN;
    Buffer *b = buf_create(ARG(2), ARG(3), ARG(4), ARG(5), 1);
    W32(ARG(6), b->com.guest);
    STDRET(6, HR_OK);
}
M(CreateRenderTarget) {
    HOSTFN;
    Surface *s = surf_create(ARG(2), ARG(3), ARG(4), 0);
    s->usage = 1; // D3DUSAGE_RENDERTARGET: stockage GL
    W32(ARG(7), surf_wrap(s));
    STDRET(7, HR_OK);
}
M(CreateDepthStencilSurface) {
    HOSTFN;
    Surface *s = surf_create(ARG(2), ARG(3), ARG(4), 1);
    W32(ARG(6), surf_wrap(s));
    STDRET(6, HR_OK);
}
M(CreateImageSurface) {
    HOSTFN;
    Surface *s = surf_create(ARG(2), ARG(3), ARG(4), 0);
    W32(ARG(5), surf_wrap(s));
    STDRET(5, HR_OK);
}
void surf_copy_rects(Surface *src, uint32_t rects, uint32_t n, Surface *dst, uint32_t points);
M(CopyRects) {
    HOSTFN;
    Surface *src = com_get_kind(ARG(2), K_SURFACE), *dst = com_get_kind(ARG(5), K_SURFACE);
    if (!src || !dst) STDRET(6, D3DERR_INVALIDCALL);
    surf_copy_rects(src, ARG(3), ARG(4), dst, ARG(6));
    STDRET(6, HR_OK);
}
M(UpdateTexture) { d3d_log_once("UpdateTexture"); STDRET(3, HR_OK); }
M(GetFrontBuffer) { d3d_log_once("GetFrontBuffer"); STDRET(2, HR_OK); }
M(SetRenderTarget) {
    HOSTFN; DEV;
    Surface *rt = ARG(2) ? com_get_kind(ARG(2), K_SURFACE) : NULL;
    Surface *ds = ARG(3) ? com_get_kind(ARG(3), K_SURFACE) : NULL;
    if (rt) {
        Surface *o = d->rt; addref(rt); PSET(rt, rt); if (o) d3d_release(&o->com);
        // D3D8: SetRenderTarget resets the viewport to the whole new target (the 4096x4096 pause capture relies on it)
        __typeof__(d->vp) v = {0, 0, rt->w, rt->h, 0.0f, 1.0f};
        prep_set(&d->vp, &v, sizeof v);
    }
    if (ds || ARG(3) == 0) { Surface *o = d->ds; addref(ds); PSET(ds, ds); if (o) d3d_release(&o->com); }
    prep_bind();
    STDRET(3, HR_OK);
}
M(GetRenderTarget) { HOSTFN; DEV; addref(d->rt); W32(ARG(2), d->rt ? d->rt->com.guest : 0); STDRET(2, HR_OK); }
M(GetDepthStencilSurface) {
    HOSTFN; DEV;
    addref(d->ds);
    W32(ARG(2), d->ds ? d->ds->com.guest : 0);
    STDRET(2, d->ds ? HR_OK : 0x8876086cu);
}
M(BeginScene) {
    HOSTFN; DEV;
    if (!d->in_scene) g_t_begin = SDL_GetPerformanceCounter();
    d->in_scene = 1;
    STDRET(1, HR_OK);
}
M(EndScene) { HOSTFN; DEV; d->in_scene = 0; STDRET(1, HR_OK); }
M(Clear) {
    HOSTFN; DEV;
    prep_clear(ARG(4), ARG(5), rt_f32(ARG(6)), ARG(7), (int)ARG(2), ARG(3));
    STDRET(7, HR_OK);
}
static float *xform(Device *d, uint32_t st) {
    if (st == 2) return d->view;
    if (st == 3) return d->proj;
    if (st >= 16 && st < 24) return d->texm[st - 16];
    if (st == 256) return d->world;
    return NULL;
}
M(SetTransform) {
    HOSTFN; DEV;
    { static unsigned n; if (ARG(2) >= 16 && ARG(2) < 24 && n++ < 6) rt_log("[light] SetTransform texture %u (frame %u)\n", ARG(2) - 16, g_frame); }
    float *m = xform(d, ARG(2)); if (m) prep_set(m, mb + ARG(3), 64); STDRET(3, HR_OK);
}
M(GetTransform) { HOSTFN; DEV; float *m = xform(d, ARG(2)); if (m) memcpy(mb + ARG(3), m, 64); STDRET(3, HR_OK); }
M(MultiplyTransform) {
    HOSTFN; DEV;
    float *m = xform(d, ARG(2)), b[16], r[16];
    if (m) {
        memcpy(b, mb + ARG(3), 64);
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                r[i * 4 + j] = b[i * 4 + 0] * m[0 + j] + b[i * 4 + 1] * m[4 + j] + b[i * 4 + 2] * m[8 + j] + b[i * 4 + 3] * m[12 + j];
        prep_set(m, r, 64);
    }
    STDRET(3, HR_OK);
}
M(SetViewport) {
    HOSTFN; DEV;
    uint32_t p = ARG(2);
    __typeof__(d->vp) v = {R32(p), R32(p + 4), R32(p + 8), R32(p + 12), rt_f32(R32(p + 16)), rt_f32(R32(p + 20))};
    prep_set(&d->vp, &v, sizeof v);
    STDRET(2, HR_OK);
}
M(GetViewport) {
    HOSTFN; DEV;
    uint32_t p = ARG(2);
    W32(p, d->vp.x); W32(p + 4, d->vp.y); W32(p + 8, d->vp.w); W32(p + 12, d->vp.h);
    W32(p + 16, rt_u32f(d->vp.minz)); W32(p + 20, rt_u32f(d->vp.maxz));
    STDRET(2, HR_OK);
}
static uint8_t g_material[68], g_lights[8][104];
M(SetMaterial) { HOSTFN; memcpy(g_material, mb + ARG(2), 68); STDRET(2, HR_OK); }
M(GetMaterial) { HOSTFN; memcpy(mb + ARG(2), g_material, 68); STDRET(2, HR_OK); }
M(SetLight) { HOSTFN; { static unsigned n; if (n++ < 4) rt_log("[light] SetLight %u type %u\n", ARG(2), R32(ARG(3))); } if (ARG(2) < 8) memcpy(g_lights[ARG(2)], mb + ARG(3), 104); STDRET(3, HR_OK); }
M(GetLight) { HOSTFN; if (ARG(2) < 8) memcpy(mb + ARG(3), g_lights[ARG(2)], 104); STDRET(3, HR_OK); }
M(LightEnable) { HOSTFN; { static unsigned n; if (n++ < 4) rt_log("[light] LightEnable %u %u\n", ARG(2), ARG(3)); } STDRET(3, HR_OK); }
M(GetLightEnable) { HOSTFN; W32(ARG(3), 0); STDRET(3, HR_OK); }
M(SetClipPlane) { STDRET(3, HR_OK); }
M(GetClipPlane) { STDRET(3, HR_OK); }
static SB *s_rec;   // state block being recorded (see BeginStateBlock): the setters below write into it instead
M(SetRenderState) {
    HOSTFN; DEV;
    if (s_rec) { sb_set_rs(s_rec, ARG(2), ARG(3)); STDRET(3, HR_OK); }
    if (ARG(2) < 256) { uint32_t v = ARG(3); prep_set(&d->rs[ARG(2)], &v, 4); }
    STDRET(3, HR_OK);
}
M(GetRenderState) { HOSTFN; DEV; W32(ARG(3), ARG(2) < 256 ? d->rs[ARG(2)] : 0); STDRET(3, HR_OK); }

// State blocks: full capture of the "simple" state (states, transforms, shaders).
// State blocks hold only the states they recorded (stateblock.h, tests/test_stateblock.py). While a block is being
// recorded (BeginStateBlock .. EndStateBlock), the setters it covers write into it and leave the device alone.
_Static_assert(SB_STAGES == MAX_STAGES && SB_VSC == MAX_VSC, "stateblock.h sizes follow the device");
static SB *blocks[256];
static void dev_to_sb(const Device *d, SBState *s) {
    memcpy(s->rs, d->rs, sizeof s->rs);
    memcpy(s->tss, d->tss, sizeof s->tss);
    memcpy(s->vsc, d->vsc, sizeof s->vsc);
    s->vs = d->vs; s->ps = d->ps;
    for (int t = 0; t < MAX_STAGES; t++) s->tex[t] = d->tex[t] ? d->tex[t]->com.guest : 0;
}
static uint32_t sb_store(SB *b) {
    for (int i = 1; i < 256; i++) if (!blocks[i]) { blocks[i] = b; return (uint32_t)i; }
    free(b);
    return 0;
}
M(BeginStateBlock) {
    d3d_log_once("BeginStateBlock");
    if (!s_rec) s_rec = malloc(sizeof *s_rec);
    if (s_rec) sb_clear(s_rec);
    STDRET(1, s_rec ? HR_OK : D3DERR_INVALIDCALL);
}
M(EndStateBlock) {
    HOSTFN;
    uint32_t h = s_rec ? sb_store(s_rec) : 0;
    s_rec = NULL;
    W32(ARG(2), h);
    STDRET(2, h ? HR_OK : D3DERR_INVALIDCALL);
}
// Only the runs of words that changed go to the prepare thread ( ApplyStateBlock 1.4% of the main thread
// in the park; prep_set of a whole 1-1.5 KB array copied and queued all of it as soon as one state differed).
static void prep_set_runs(void *field, const void *src, size_t n) {
    uint32_t *f = field;
    const uint32_t *s = src;
    size_t w = n / 4, i = 0;
    while (i < w) {
        if (f[i] == s[i]) { i++; continue; }
        size_t j = i + 1, last = i;
        while (j < w && j - last <= 4) { if (f[j] != s[j]) last = j; j++; }   // merge runs closer than 4 words
        prep_set(f + i, s + i, 4 * (last + 1 - i));
        i = last + 1;
    }
}
M(ApplyStateBlock) {
    HOSTFN; DEV;
    SB *b = ARG(2) < 256 ? blocks[ARG(2)] : NULL;
    if (b) {
        static SBState s;   // main thread only
        dev_to_sb(d, &s);
        sb_apply(b, &s);
        prep_set_runs(d->rs, s.rs, sizeof d->rs);
        prep_set_runs(d->tss, s.tss, sizeof d->tss);
        prep_set_runs(d->vsc, s.vsc, sizeof d->vsc);
        PSET(vs, s.vs); PSET(ps, s.ps);
        for (int t = 0; t < MAX_STAGES; t++) {
            if (!b->m_tex[t] || s.tex[t] == (d->tex[t] ? d->tex[t]->com.guest : 0)) continue;
            Texture *nt = s.tex[t] ? com_get_kind(s.tex[t], K_TEXTURE) : NULL, *o = d->tex[t];
            addref(nt);
            PSET(tex[t], nt);
            if (o) d3d_release(&o->com);
        }
    }
    STDRET(2, HR_OK);
}
M(CaptureStateBlock) {
    HOSTFN; DEV;
    SB *b = ARG(2) < 256 ? blocks[ARG(2)] : NULL;
    if (b) { static SBState s; dev_to_sb(d, &s); sb_capture(b, &s); }
    STDRET(2, HR_OK);
}
M(DeleteStateBlock) { HOSTFN; if (ARG(2) < 256) { free(blocks[ARG(2)]); blocks[ARG(2)] = NULL; } STDRET(2, HR_OK); }
M(CreateStateBlock) {   // D3DSBT_ALL / PIXELSTATE / VERTEXSTATE: all of the states the port tracks
    HOSTFN; DEV;
    SB *b = malloc(sizeof *b);
    uint32_t h = 0;
    if (b) { static SBState s; sb_clear(b); sb_mark_all(b); dev_to_sb(d, &s); sb_capture(b, &s); h = sb_store(b); }
    W32(ARG(3), h);
    STDRET(3, h ? HR_OK : D3DERR_INVALIDCALL);
}
M(SetClipStatus) { STDRET(2, HR_OK); }
M(GetClipStatus) { HOSTFN; W32(ARG(2), 0); W32(ARG(2) + 4, 0); STDRET(2, HR_OK); }
M(GetTexture) {
    HOSTFN; DEV;
    Texture *t = ARG(2) < MAX_STAGES ? d->tex[ARG(2)] : NULL;
    addref(t);
    W32(ARG(3), t ? t->com.guest : 0);
    STDRET(3, HR_OK);
}
M(SetTexture) {
    HOSTFN; DEV;
    uint32_t s = ARG(2);
    if (s >= MAX_STAGES) STDRET(3, HR_OK);
    if (s_rec) { sb_set_tex(s_rec, s, ARG(3)); STDRET(3, HR_OK); }
    Texture *t = ARG(3) ? com_get_kind(ARG(3), K_TEXTURE) : NULL;
    if (ARG(3) && !t) d3d_log_once("SetTexture: unsupported texture type (volume?)");
    Texture *o = d->tex[s];
    addref(t);
    PSET(tex[s], t);
    if (o) d3d_release(&o->com);
    STDRET(3, HR_OK);
}
M(GetTextureStageState) {
    HOSTFN; DEV;
    W32(ARG(4), ARG(2) < MAX_STAGES && ARG(3) < 32 ? d->tss[ARG(2)][ARG(3)] : 0);
    STDRET(4, HR_OK);
}
M(SetTextureStageState) {
    HOSTFN; DEV;
    if (s_rec) { sb_set_tss(s_rec, ARG(2), ARG(3), ARG(4)); STDRET(4, HR_OK); }
    if (ARG(2) < MAX_STAGES && ARG(3) < 32) { uint32_t v = ARG(4); prep_set(&d->tss[ARG(2)][ARG(3)], &v, 4); }
    {   // lighting diagnosis (flashlight black): texture coordinate generation and projected / transformed coordinates
        static unsigned n;
        if (((ARG(3) == 11 && (ARG(4) >> 16)) || (ARG(3) == 24 && ARG(4))) && n++ < 12)
            rt_log("[light] stage %u state %u = %08x (frame %u)\n", ARG(2), ARG(3), ARG(4), g_frame);
    }
    STDRET(4, HR_OK);
}
M(ValidateDevice) { HOSTFN; W32(ARG(2), 1); STDRET(2, HR_OK); }
M(GetInfo) { STDRET(4, 1); } // S_FALSE
M(SetPaletteEntries) { d3d_log_once("SetPaletteEntries"); STDRET(3, HR_OK); }
M(GetPaletteEntries) { STDRET(3, HR_OK); }
M(SetCurrentTexturePalette) { STDRET(2, HR_OK); }
M(GetCurrentTexturePalette) { HOSTFN; W32(ARG(2), 0); STDRET(2, HR_OK); }

static uint32_t prim_verts(uint32_t type, uint32_t n) {
    switch (type) {
    case 1: return n;          // POINTLIST
    case 2: return n * 2;      // LINELIST
    case 3: return n + 1;      // LINESTRIP
    case 4: return n * 3;      // TRIANGLELIST
    case 5: case 6: return n + 2; // TRIANGLESTRIP / FAN
    default: return 0;
    }
}
volatile int g_drawdump;
static int ui_box_draw(Device *d, uint32_t prim, uint32_t *verts, uint32_t stride, uint32_t n, int may_clip);
static void draw_dump(Device *d, const char *kind, uint32_t prim, uint32_t n, uint32_t v, uint32_t stride, uint32_t nv, uint32_t caller);
M(DrawPrimitive) {
    HOSTFN; DEV;
    { Buffer *vb0 = d->stream[0].vb; uint32_t st = d->stream[0].stride;
      if (g_drawdump) rt_log("[draw] vb mem %08x start %u stride %u\n", vb0 ? vb0->mem : 0, ARG(3), st);
      draw_dump(d, "VB", ARG(2), ARG(4), vb0 && vb0->mem ? vb0->mem + ARG(3) * st : 0, st, prim_verts(ARG(2), ARG(4)), R32(c->esp)); }
#ifndef __vita__
    {   // SH3_SKIP_CALLER=hex (PC diagnostics): drops the vertex-buffer draws issued from that return address
        static uint32_t skip = 1;
        if (skip == 1) skip = getenv("SH3_SKIP_CALLER") ? (uint32_t)strtoul(getenv("SH3_SKIP_CALLER"), NULL, 16) : 0;
        if (skip && R32(c->esp) == skip) STDRET(4, HR_OK);
        // SH3_SPRDIAG=frame: the 3D sprites of 0x5f5620 (save seal, signs) drawn that frame: vertex and VS constants
        static int sprf = -1;
        if (sprf < 0) sprf = getenv("SH3_SPRDIAG") ? atoi(getenv("SH3_SPRDIAG")) : 0;
        extern unsigned g_frame;
        Buffer *vb0 = d->stream[0].vb;
        if (sprf && g_frame == (unsigned)sprf && R32(c->esp) == 0x5f572eu && vb0 && vb0->mem) {
            uint8_t *const mb = g_mem;
            uint32_t st = d->stream[0].stride, v = vb0->mem + ARG(3) * st;
            rt_log("[spr] vs=%x stride=%u start=%u n=%u tex=%ux%u blend=%u %u/%u v:", d->vs, st, ARG(3), ARG(4),
                   d->tex[0] ? d->tex[0]->w : 0, d->tex[0] ? d->tex[0]->h : 0, d->rs[27], d->rs[19], d->rs[20]);
            for (uint32_t i = 0; i < st / 4 && i < 12; i++) rt_log(" %08x", R32(v + i * 4));
            rt_log("\n      c0 %g %g %g %g | c4 %g %g %g %g | c52 %g %g %g %g | c53 %g %g %g %g | c54 %g %g %g %g\n",
                   d->vsc[0][0], d->vsc[0][1], d->vsc[0][2], d->vsc[0][3], d->vsc[4][0], d->vsc[4][1], d->vsc[4][2], d->vsc[4][3],
                   d->vsc[52][0], d->vsc[52][1], d->vsc[52][2], d->vsc[52][3], d->vsc[53][0], d->vsc[53][1], d->vsc[53][2],
                   d->vsc[53][3], d->vsc[54][0], d->vsc[54][1], d->vsc[54][2], d->vsc[54][3]);
            static int vs_logged;
            if (d->vs == 0x2f && !vs_logged++) {
                for (int i = 0; i < MAX_VSC; i++)
                    if (d->vsc[i][0] || d->vsc[i][1] || d->vsc[i][2] || d->vsc[i][3])
                        rt_log("      c%d = %g %g %g %g\n", i, d->vsc[i][0], d->vsc[i][1], d->vsc[i][2], d->vsc[i][3]);
                PShader *psh = d->ps ? ps_get(d->ps) : NULL;
                rt_log("      ps=%x fog=%u fogcolor=%08x tfactor=%08x\n", d->ps, d->rs[28], d->rs[34], d->rs[60]);
                if (psh && psh->glsl_body) rt_log("--- PS ---\n%s--- end ---\n", psh->glsl_body);
                for (int i = 0; i < 8; i++)
                    if (d->psc[i][0] || d->psc[i][1] || d->psc[i][2] || d->psc[i][3])
                        rt_log("      pc%d = %g %g %g %g\n", i, d->psc[i][0], d->psc[i][1], d->psc[i][2], d->psc[i][3]);
                rt_log("      tss0 cop=%u a1=%u a2=%u aop=%u aa1=%u aa2=%u; atest=%u ref=%u func=%u; z=%u zw=%u\n",
                       d->tss[0][1], d->tss[0][2], d->tss[0][3], d->tss[0][4], d->tss[0][5], d->tss[0][6],
                       d->rs[15], d->rs[24], d->rs[25], d->rs[7], d->rs[14]);
            }
        }
    }
#endif
    {   // pre-transformed 2D from a vertex buffer (the map's marks): same box rules as the UP draws
        Buffer *vb0 = d->stream[0].vb; uint32_t st = d->stream[0].stride, n = prim_verts(ARG(2), ARG(4));
        uint32_t v = vb0 && vb0->mem ? vb0->mem + ARG(3) * st : 0, v0 = v;
        if (v) {
            if (!ui_box_draw(d, ARG(2), &v, st, n, 1)) STDRET(4, HR_OK);
            if (v != v0) { prep_draw(d, ARG(2), 0, n, ARG(4), NULL, 0, 0, mb + v, st); STDRET(4, HR_OK); }
        } else { extern int g_ws_ui_boxed; g_ws_ui_boxed = 0; }
    }
    {   // SH3_SEAL_LOG=1: every self-lit 3D sprite (0x5f5620 list, vertex shader 0x2f) with
        // its texture and first vertex, to check which decals use that shader
        static int sl = -1;
        if (sl < 0) sl = getenv("SH3_SEAL_LOG") != NULL;
        Buffer *vb0 = d->stream[0].vb;
        if (sl && R32(c->esp) == 0x5f572eu && d->vs == 0x2f && vb0 && vb0->mem) {
            uint8_t *const mb = g_mem; extern unsigned g_frame;
            uint32_t v = vb0->mem + ARG(3) * d->stream[0].stride;
            static unsigned last; if (g_frame != last) { last = g_frame; rt_log("[seal] frame %u\n", g_frame); }
            rt_log("[seal] tex=%p %ux%u uv=%.3f,%.3f pos=%.0f,%.0f,%.0f ps=%x\n", (void *)d->tex[0], d->tex[0] ? d->tex[0]->w : 0,
                   d->tex[0] ? d->tex[0]->h : 0, (double)rt_f32(R32(v + 24)), (double)rt_f32(R32(v + 28)),
                   (double)rt_f32(R32(v)), (double)rt_f32(R32(v + 4)), (double)rt_f32(R32(v + 8)), d->ps);
        }
    }
    prep_draw(d, ARG(2), ARG(3), prim_verts(ARG(2), ARG(4)), ARG(4), NULL, 0, 0, NULL, 0);
    STDRET(4, HR_OK);
}
M(DrawIndexedPrimitive) {
    HOSTFN; DEV;
    draw_dump(d, "IVB", ARG(2), ARG(6), 0, 0, 0, R32(c->esp));
    { extern int g_ws_ui_boxed; g_ws_ui_boxed = 0; }
    uint32_t type = ARG(2), minidx = ARG(3), start = ARG(5), n = ARG(6);
    if (!d->ib) STDRET(6, D3DERR_INVALIDCALL);
    int i32 = d->ib->fmt_or_fvf == FMT_INDEX32;
    const uint8_t *idx = mb + d->ib->mem + start * (i32 ? 4 : 2);
    prep_draw(d, type, d->base_vertex, prim_verts(type, n), n, idx, i32, minidx, NULL, 0);
    STDRET(6, HR_OK);
}
// Scratch vertices in the port's guest variables (PORT_VARS + 0x200, recomp/image_patches.py): prep_draw only takes
// user vertices that lie in guest memory.
#define TEXEL_ALIGN_VERTS 0x071F0200u
// PC diagnostics: F9 (pc/main.c) logs every draw of the next two frames with its state, to read a screen's 2D.
static void draw_dump(Device *d, const char *kind, uint32_t prim, uint32_t n, uint32_t v, uint32_t stride, uint32_t nv, uint32_t caller) {
    if (!g_drawdump) return;
    uint8_t *const mb = g_mem;
    extern int g_ws_ui_boxed; extern unsigned g_frame;
    Texture *t0 = d->tex[0];
    rt_log("[draw] f%u from %08x %s prim=%u n=%u fvf=%x boxed=%d tex=%ux%u fmt=%x blend=%u %u/%u atest=%u ref=%u z=%u cull=%u cop=%u aop=%u rt=%ux%u",
           g_frame, caller, kind, prim, n, d->vs, g_ws_ui_boxed, t0 ? t0->w : 0, t0 ? t0->h : 0, t0 ? t0->fmt : 0,
           d->rs[27], d->rs[19], d->rs[20], d->rs[15], d->rs[24], d->rs[7], d->rs[22], d->tss[0][1], d->tss[0][4],
           d->rt ? d->rt->w : 0, d->rt ? d->rt->h : 0);
    rt_log(" zfunc=%u afunc=%u cwrite=%x", d->rs[23], d->rs[25], d->rs[168]);
    if (d->rs[52])   // stencil: enable, func, ref, mask, write mask, fail / zfail / pass ops
        rt_log(" st=func%u ref=%x mask=%x wmask=%x ops=%u/%u/%u", d->rs[56], d->rs[57], d->rs[58], d->rs[59],
               d->rs[53], d->rs[54], d->rs[55]);
    if (v && stride >= 16 && nv && nv <= 256 && !(d->vs & 1) && d->vs < 0x10000) {
        float lo = 1e9f, hi = -1e9f, ylo = 1e9f, yhi = -1e9f;
        for (uint32_t i = 0; i < nv; i++) {
            float x = rt_f32(R32(v + i * stride)), y = rt_f32(R32(v + i * stride + 4));
            if (x < lo) lo = x; if (x > hi) hi = x; if (y < ylo) ylo = y; if (y > yhi) yhi = y;
        }
        rt_log(" x=%.1f..%.1f y=%.1f..%.1f z0=%.3f", (double)lo, (double)hi, (double)ylo, (double)yhi, (double)rt_f32(R32(v + 8)));
        if (d->vs & 0x40) rt_log(" col0=%08x", R32(v + ((d->vs & 0xe) == 4 ? 16 : 12)));
        if (nv <= 6 && t0 && t0->w == 128)
            for (uint32_t i = 0; i < nv; i++) {
                uint32_t q = v + i * stride;
                rt_log("\n   v%u %.2f %.2f z=%.4f rhw=%.4f uv=%.4f %.4f", i, (double)rt_f32(R32(q)), (double)rt_f32(R32(q + 4)),
                       (double)rt_f32(R32(q + 8)), (double)rt_f32(R32(q + 12)), (double)rt_f32(R32(q + stride - 8)),
                       (double)rt_f32(R32(q + stride - 4)));
            }
    }
    rt_log("\n");
}
#define UI_CLIP_VERTS 0x071F0300u    // PORT_VARS + 0x300: one clipped quad (4 x 64 bytes at most)
#define UI_BAND_RECTS 0x071F0400u    // PORT_VARS + 0x400: the two side-band rectangles to clear

// Pre-transformed 2D whose x GsX just put in the centred 4:3 box (widescreen.c g_ws_ui_boxed, rt/ws_ui.h). The
// PS2 screen ended at the picture's edges: what lies wholly in a side band is not drawn (map marks once the map is
// panned or zoomed), a quad crossing an edge is cut at it, and an opaque picture covering the whole box (inventory,
// map) gets black side bands instead of what the 3D scene left there. Returns 0 to skip the draw; *verts may move
// to a clipped copy.
int g_ui_clip_frame;   // set by a full-box 2D picture, cleared at Present
static int ui_box_draw(Device *d, uint32_t prim, uint32_t *verts, uint32_t stride, uint32_t n, int may_clip) {
    extern int g_ws_ui_boxed;
    uint8_t *const mb = g_mem;
    int boxed = g_ws_ui_boxed;
    g_ws_ui_boxed = 0;
    uint32_t fvf = d->vs, v = *verts;
    // map: Heather's arrow at 60% of the game's size, around its centre (ws_map_arrow_uv, user closer to
    // the PS2); a copy of the quad is drawn, the game's vertices are untouched
    if (!(fvf & 1) && (fvf & 0xe) == 0x4 && ((fvf >> 8) & 0xf) >= 1 && n == 4 && stride >= 24 && stride <= 64 &&
        d->tex[0] && d->tex[0]->w == 128 && d->tex[0]->h == 128) {
        uint32_t uvo = 16 + ((fvf & 0x40) ? 4 : 0) + ((fvf & 0x80) ? 4 : 0);
        float x[4], y[4], u[4], w[4];
        for (int i = 0; i < 4; i++) {
            x[i] = rt_f32(R32(v + i * stride)); y[i] = rt_f32(R32(v + i * stride + 4));
            u[i] = rt_f32(R32(v + i * stride + uvo)); w[i] = rt_f32(R32(v + i * stride + uvo + 4));
        }
        if (ws_map_arrow_uv(u, w, 4)) {
            ws_shrink_quad(x, y, 4, 0.6f);
            if (v != UI_CLIP_VERTS) memcpy(mb + UI_CLIP_VERTS, mb + v, 4 * stride);
            for (int i = 0; i < 4; i++) { W32(UI_CLIP_VERTS + i * stride, rt_u32f(x[i])); W32(UI_CLIP_VERTS + i * stride + 4, rt_u32f(y[i])); }
            *verts = v = UI_CLIP_VERTS;
        }
    }
    if ((!boxed && !g_ui_clip_frame) || (fvf & 1) || (fvf & 0xe) != 0x4 || !d->rt || n == 0 || n > 64 || stride < 16 || stride > 64) return 1;
    float W = (float)d->rt->w, H = (float)d->rt->h, x0, x1;
    if (!ws_ui_box(W, H, &x0, &x1)) return 1;
    x0 -= 0.5f; x1 -= 0.5f;   // D3D pixel centres
    float xs[64], ys[64];
    for (uint32_t i = 0; i < n; i++) { xs[i] = rt_f32(R32(v + i * stride)); ys[i] = rt_f32(R32(v + i * stride + 4)); }
    int kind = ws_ui_classify(xs, ys, (int)n, H, x0, x1);
    if (kind == WS_UI_SKIP) return 0;
    if (kind == WS_UI_COVER && boxed) {
        // a full-box picture (map paper, inventory): the rest of this frame's 2D belongs to the box. The map draws
        // its marks from a vertex buffer filled earlier, so GsX's flag no longer says which draw is boxed.
        g_ui_clip_frame = 1;
        int opaque = !d->rs[27];   // D3DRS_ALPHABLENDENABLE off
        if (!opaque && (fvf & 0x40)) {
            opaque = 1;
            for (uint32_t i = 0; i < n; i++) if ((R32(v + i * stride + 16) >> 24) != 0xff) opaque = 0;
        }
        if (opaque) {
            int32_t l = (int32_t)(x0 + 0.5f), r = (int32_t)(x1 + 1.f), w = (int32_t)W, h = (int32_t)H;
            int32_t rects[8] = {0, 0, l, h, r, 0, w, h};
            for (int i = 0; i < 8; i++) W32(UI_BAND_RECTS + 4 * i, (uint32_t)rects[i]);
            prep_clear(1, 0xff000000u, 1.f, 0, 2, UI_BAND_RECTS);
        }
    }
    if (may_clip && n == 4 && (prim == 5 || prim == 6) && ((fvf >> 8) & 0xf) >= 1) {
        uint32_t uv = 16 + ((fvf & 0x40) ? 4 : 0) + ((fvf & 0x80) ? 4 : 0);
        float x[4], u[4];
        for (int i = 0; i < 4; i++) { x[i] = xs[i]; u[i] = rt_f32(R32(v + i * stride + uv)); }
        if (ws_ui_clip_quad(x, u, x0, x1)) {
            memcpy(mb + UI_CLIP_VERTS, mb + v, 4 * stride);
            for (int i = 0; i < 4; i++) {
                W32(UI_CLIP_VERTS + i * stride, rt_u32f(x[i]));
                W32(UI_CLIP_VERTS + i * stride + uv, rt_u32f(u[i]));
            }
            *verts = UI_CLIP_VERTS;
        }
    }
    return 1;
}
M(DrawPrimitiveUP) {
    HOSTFN; DEV;
    HOST_TIMED(g_t_up, g_n_up);
    uint32_t verts = ARG(4), stride = ARG(5), n = prim_verts(ARG(2), ARG(3));
    draw_dump(d, "UP", ARG(2), ARG(3), verts, stride, n, R32(c->esp));
    if (!ui_box_draw(d, ARG(2), &verts, stride, n, 1)) STDRET(5, HR_OK);
    // Frame feedback quads mapped 1:1 with point sampling: D3D pixel centres.
    uint32_t fvf = d->vs;
    if (n == 4 && stride <= 64 && !(fvf & 1) && (fvf & 0xe) == 0x4 && ((fvf >> 8) & 0xf) >= 1 && d->tex[0] &&
        d->tss[0][16] < 2) {
        memcpy(mb + TEXEL_ALIGN_VERTS, mb + verts, 4 * stride);
        uint32_t uv = 16 + ((fvf & 0x40) ? 4 : 0) + ((fvf & 0x80) ? 4 : 0);
        if (texel_align_quad(mb + TEXEL_ALIGN_VERTS, stride, 4, uv, (float)d->tex[0]->w, (float)d->tex[0]->h))
            verts = TEXEL_ALIGN_VERTS;
    }
    prep_draw(d, ARG(2), 0, n, ARG(3), NULL, 0, 0, mb + verts, stride);
    STDRET(5, HR_OK);
}
M(DrawIndexedPrimitiveUP) {
    HOSTFN; DEV;
    uint32_t type = ARG(2), minidx = ARG(3), n = ARG(5);
    draw_dump(d, "IUP", type, n, ARG(8) + minidx * ARG(9), ARG(9), ARG(4), R32(c->esp));
    { uint32_t first = ARG(8) + minidx * ARG(9); if (!ui_box_draw(d, type, &first, ARG(9), ARG(4), 0)) STDRET(9, HR_OK); }
    prep_draw(d, type, 0, prim_verts(type, n), n, mb + ARG(6), ARG(7) == FMT_INDEX32, minidx, mb + ARG(8), ARG(9));
    STDRET(9, HR_OK);
}
M(ProcessVertices) { d3d_log_once("ProcessVertices"); STDRET(6, D3DERR_INVALIDCALL); }
M(CreateVertexShader) {
    HOSTFN;
    static unsigned s_n;
    uint32_t h = vs_create(ARG(2), ARG(3));
    if (++s_n <= 3 || s_n % 50 == 0) rt_log("[vs] CreateVertexShader #%u -> 0x%x\n", s_n, h);
    W32(ARG(4), h);
    STDRET(5, h ? HR_OK : D3DERR_INVALIDCALL);
}
M(SetVertexShader) { HOSTFN; DEV; if (s_rec) sb_set_vs(s_rec, ARG(2)); else PSET(vs, ARG(2)); STDRET(2, HR_OK); }
M(GetVertexShader) { HOSTFN; DEV; W32(ARG(2), d->vs); STDRET(2, HR_OK); }
M(DeleteVertexShader) { HOSTFN; prep_vsdel(ARG(2)); STDRET(2, HR_OK); }
M(SetVertexShaderConstant) {
    HOSTFN; DEV;
    uint32_t r = ARG(2), n = ARG(4);
    if (r + n > MAX_VSC) n = r < MAX_VSC ? MAX_VSC - r : 0;
    if (s_rec) { float tmp[MAX_VSC][4]; memcpy(tmp, mb + ARG(3), 16u * n); sb_set_vsc(s_rec, r, tmp[0], n); STDRET(4, HR_OK); }
    if (n) prep_set(d->vsc[r], mb + ARG(3), 16u * n);
    STDRET(4, HR_OK);
}

// SH3: removido (SH4 native matrix-palette upload, see reference/sh4/d3d8/device_model_palette.c).
M(GetVertexShaderConstant) {
    HOSTFN; DEV;
    uint32_t r = ARG(2), n = ARG(4);
    if (r + n > MAX_VSC) n = r < MAX_VSC ? MAX_VSC - r : 0;
    memcpy(mb + ARG(3), d->vsc[r], 16u * n);
    STDRET(4, HR_OK);
}
M(GetVertexShaderDeclaration) { d3d_log_once("GetVertexShaderDeclaration"); STDRET(4, D3DERR_INVALIDCALL); }
M(GetVertexShaderFunction) { d3d_log_once("GetVertexShaderFunction"); STDRET(4, D3DERR_INVALIDCALL); }
M(SetStreamSource) {
    HOSTFN; DEV;
    uint32_t n = ARG(2);
    if (n >= 16) STDRET(4, D3DERR_INVALIDCALL);
    Buffer *b = ARG(3) ? com_get_kind(ARG(3), K_VB) : NULL;
    Buffer *o = d->stream[n].vb;
    addref(b);
    PSET(stream[n].vb, b);
    PSET(stream[n].stride, ARG(4));
    if (o) d3d_release(&o->com);
    STDRET(4, HR_OK);
}
M(GetStreamSource) {
    HOSTFN; DEV;
    uint32_t n = ARG(2) & 15;
    addref(d->stream[n].vb);
    W32(ARG(3), d->stream[n].vb ? d->stream[n].vb->com.guest : 0);
    W32(ARG(4), d->stream[n].stride);
    STDRET(4, HR_OK);
}
M(SetIndices) {
    HOSTFN; DEV;
    Buffer *b = ARG(2) ? com_get_kind(ARG(2), K_IB) : NULL;
    Buffer *o = d->ib;
    addref(b);
    PSET(ib, b);
    PSET(base_vertex, ARG(3));
    if (o) d3d_release(&o->com);
    STDRET(3, HR_OK);
}
M(GetIndices) {
    HOSTFN; DEV;
    addref(d->ib);
    W32(ARG(2), d->ib ? d->ib->com.guest : 0);
    W32(ARG(3), d->base_vertex);
    STDRET(3, HR_OK);
}
M(CreatePixelShader) {
    HOSTFN;
    uint32_t h = ps_create(ARG(2));
    W32(ARG(3), h);
    STDRET(3, h ? HR_OK : D3DERR_INVALIDCALL);
}
M(SetPixelShader) { HOSTFN; DEV; if (s_rec) sb_set_ps(s_rec, ARG(2)); else PSET(ps, ARG(2)); STDRET(2, HR_OK); }
M(GetPixelShader) { HOSTFN; DEV; W32(ARG(2), d->ps); STDRET(2, HR_OK); }
M(DeletePixelShader) { HOSTFN; prep_psdel(ARG(2)); STDRET(2, HR_OK); }
M(SetPixelShaderConstant) {
    HOSTFN; DEV;
    uint32_t r = ARG(2), n = ARG(4);
    if (r + n > 8) n = r < 8 ? 8 - r : 0;
    if (n) prep_set(d->psc[r], mb + ARG(3), 16u * n);
    STDRET(4, HR_OK);
}
M(GetPixelShaderConstant) {
    HOSTFN; DEV;
    uint32_t r = ARG(2), n = ARG(4);
    if (r + n > 8) n = r < 8 ? 8 - r : 0;
    memcpy(mb + ARG(3), d->psc[r], 16u * n);
    STDRET(4, HR_OK);
}
M(GetPixelShaderFunction) { STDRET(4, D3DERR_INVALIDCALL); }
M(DrawRectPatch) { d3d_log_once("DrawRectPatch"); STDRET(4, HR_OK); }
M(DrawTriPatch) { d3d_log_once("DrawTriPatch"); STDRET(4, HR_OK); }
M(DeletePatch) { STDRET(2, HR_OK); }

static const gfn dev_vt[] = {
    dev_QueryInterface, dev_AddRef, dev_Release, dev_TestCooperativeLevel, dev_GetAvailableTextureMem,
    dev_ResourceManagerDiscardBytes, dev_GetDirect3D, dev_GetDeviceCaps, dev_GetDisplayMode,
    dev_GetCreationParameters, dev_SetCursorProperties, dev_SetCursorPosition, dev_ShowCursor,
    dev_CreateAdditionalSwapChain, dev_Reset, dev_Present, dev_GetBackBuffer, dev_GetRasterStatus,
    dev_SetGammaRamp, dev_GetGammaRamp, dev_CreateTexture, dev_CreateVolumeTexture, dev_CreateCubeTexture,
    dev_CreateVertexBuffer, dev_CreateIndexBuffer, dev_CreateRenderTarget, dev_CreateDepthStencilSurface,
    dev_CreateImageSurface, dev_CopyRects, dev_UpdateTexture, dev_GetFrontBuffer, dev_SetRenderTarget,
    dev_GetRenderTarget, dev_GetDepthStencilSurface, dev_BeginScene, dev_EndScene, dev_Clear, dev_SetTransform,
    dev_GetTransform, dev_MultiplyTransform, dev_SetViewport, dev_GetViewport, dev_SetMaterial, dev_GetMaterial,
    dev_SetLight, dev_GetLight, dev_LightEnable, dev_GetLightEnable, dev_SetClipPlane, dev_GetClipPlane,
    dev_SetRenderState, dev_GetRenderState, dev_BeginStateBlock, dev_EndStateBlock, dev_ApplyStateBlock,
    dev_CaptureStateBlock, dev_DeleteStateBlock, dev_CreateStateBlock, dev_SetClipStatus, dev_GetClipStatus,
    dev_GetTexture, dev_SetTexture, dev_GetTextureStageState, dev_SetTextureStageState, dev_ValidateDevice,
    dev_GetInfo, dev_SetPaletteEntries, dev_GetPaletteEntries, dev_SetCurrentTexturePalette,
    dev_GetCurrentTexturePalette, dev_DrawPrimitive, dev_DrawIndexedPrimitive, dev_DrawPrimitiveUP,
    dev_DrawIndexedPrimitiveUP, dev_ProcessVertices, dev_CreateVertexShader, dev_SetVertexShader,
    dev_GetVertexShader, dev_DeleteVertexShader, dev_SetVertexShaderConstant, dev_GetVertexShaderConstant,
    dev_GetVertexShaderDeclaration, dev_GetVertexShaderFunction, dev_SetStreamSource, dev_GetStreamSource,
    dev_SetIndices, dev_GetIndices, dev_CreatePixelShader, dev_SetPixelShader, dev_GetPixelShader,
    dev_DeletePixelShader, dev_SetPixelShaderConstant, dev_GetPixelShaderConstant, dev_GetPixelShaderFunction,
    dev_DrawRectPatch, dev_DrawTriPatch, dev_DeletePatch};

static void default_states(Device *d, int autodepth) {
    static const uint32_t rs_def[][2] = {
        {8, 3}, {9, 2}, {14, 1}, {16, 1}, {19, 2}, {20, 1}, {22, 3}, {23, 4}, {25, 8}, {37, 0x3f800000},
        {38, 0x3f800000}, {53, 1}, {54, 1}, {55, 1}, {56, 8}, {58, 0xffffffff}, {59, 0xffffffff},
        {60, 0xffffffff}, {136, 1}, {137, 1}, {141, 1}, {142, 1}, {145, 1}, {146, 2}, {154, 0x3f800000},
        {161, 1}, {162, 0xffffffff}, {166, 0x42800000}, {168, 0xf}, {171, 1}};
    memset(d->rs, 0, sizeof d->rs);
    for (unsigned i = 0; i < sizeof rs_def / sizeof rs_def[0]; i++) d->rs[rs_def[i][0]] = rs_def[i][1];
    d->rs[7] = autodepth ? 1 : 0;
    for (int s = 0; s < MAX_STAGES; s++) {
        uint32_t *t = d->tss[s];
        memset(t, 0, 32 * 4);
        t[1] = s ? 1 : 4; t[2] = 2; t[3] = 1; t[4] = s ? 1 : 2; t[5] = 2; t[6] = 1;
        t[11] = (uint32_t)s; t[13] = 1; t[14] = 1; t[16] = 1; t[17] = 1; t[18] = 0; t[21] = 1;
        t[25] = 1; t[26] = 1; t[27] = 1; t[28] = 1;
    }
    for (int i = 0; i < 16; i++) d->world[i] = d->view[i] = d->proj[i] = (i % 5 == 0);
    for (int s = 0; s < MAX_STAGES; s++)
        for (int i = 0; i < 16; i++) d->texm[s][i] = (i % 5 == 0);
    for (int c = 0; c < 3; c++)
        for (int i = 0; i < 256; i++) d->gamma[c][i] = (uint16_t)(i * 257);
}

uint32_t device_create(uint32_t pp) {
    HOSTFN;
    Device *d = calloc(1, sizeof *d);
    d->bb_w = R32(pp) ? R32(pp) : 640;
    d->bb_h = R32(pp + 4) ? R32(pp + 4) : 480;
    default_states(d, R32(pp + 32));
    d->vp.w = d->bb_w; d->vp.h = d->bb_h; d->vp.maxz = 1.0f;
    d->backbuffer = surf_create(d->bb_w, d->bb_h, FMT_X8R8G8B8, 0);
    d->backbuffer->is_backbuffer = 1;
    surf_wrap(d->backbuffer);
    if (R32(pp + 32)) {
        d->depth = surf_create(d->bb_w, d->bb_h, R32(pp + 36), 1);
        surf_wrap(d->depth);
    }
    d->rt = d->backbuffer; d->rt->com.refs++;
    d->ds = d->depth; if (d->ds) d->ds->com.refs++;
    g_dev = d;
    prep_init(d);
    prep_gfx_init();
    return com_wrap(&d->com, K_DEVICE, dev_vt, 97, "IDirect3DDevice8");
}
