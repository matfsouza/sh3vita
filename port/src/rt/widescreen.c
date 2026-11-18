// "PS2 in 16:9" (M4): the port's own reimplementation of the widescreen patches the PC Fix applies on Windows
//. Installed by recomp/image_patches.py (overrides, wraps, hooks); layout math in ws_layout.h.
#include "rt.h"
#include "ws_layout.h"
#include <stdlib.h>
#include <string.h>

// Game globals.
#define G_DISPLAY_W   0x72c780u   // display mode (back buffer) width / height, and their copies
#define G_DISPLAY_H   0x72c784u
#define G_DISPLAY_W2  0x72c788u
#define G_DISPLAY_H2  0x72c78cu
#define G_DISPLAY_X   0x72c790u   // third argument of 0x416b20
#define G_MODE_DIRTY  0x72c655u   // set when the display mode changes (device reset)

// Port variables in guest memory (PORT_VARS in recomp/image_patches.py): +0 float 0.0, +0x100 side band vertices.
#define WS_BAND_VERTS 0x071F0100u

static int rw, rh;   // 3D render size (SH3_RES, default 790x448)
void ws_render_size(int *w, int *h) {
    if (!rw) {
        const char *res = getenv("SH3_RES");
#ifdef __vita__
        if (!res) res = "640x362";   // the SH4 port's size: the GPU was at 95% at 790x448
#endif
        ws_parse_res(res, &rw, &rh);
    }
    *w = rw;
    *h = rh;
}

// 0x416ae0 (w, h) / 0x416b20 (w, h, x): display-mode setters, called with disp.ini's mode, the 640x480 safe mode
// and the options menu. The port has a single mode, the same size as the 3D render target: the game's screen
// effects (mosaic transitions, frame copies) assume both are equal, and a bigger back buffer made every transition
// drift to the bottom-right. Present scales the back buffer to the Vita screen. cdecl, returns 0.
static void set_display(CPU *c, int third) {
    uint8_t *const mb = g_mem;
    int w, h;
    ws_render_size(&w, &h);
    if (third) W32(G_DISPLAY_X, R32(c->esp + 12));
    if (R32(G_DISPLAY_W) != (uint32_t)w || R32(G_DISPLAY_H) != (uint32_t)h) {
        W32(G_DISPLAY_W, w); W32(G_DISPLAY_W2, w);
        W32(G_DISPLAY_H, h); W32(G_DISPLAY_H2, h);
        W8(G_MODE_DIRTY, 1);
    }
    c->eax = 0;
    c->esp += 4;
}
void ws_set_display(CPU *c) { set_display(c, 0); }
void ws_set_display3(CPU *c) { set_display(c, 1); }

// 0x43ab80 / 0x43abc0: horizontal / vertical scale of the 3D projection, returned in ST(0). Hor+: the horizontal
// scale shrinks for the 16:9 render target, so the scene widens instead of stretching. The two 2D call sites that
// need the game's own scale (0x4ddb93, 0x599c24: ScaleX then ScaleY) set native_scale through a hook first.
static int native_scale;
void ws_native_scale_next(CPU *c) { (void)c; native_scale = 2; }
void F_0043ab80_orig(CPU *c);
void F_0043abc0_orig(CPU *c);
// In-engine cutscene zoom: the frame's bar state is read where the game would draw its cinematic bars (hook at
// 0x420320: word [0x72c8a0] >= 0 means bars this frame) and applied to the next frame's projection. That routine
// does not run outside cutscenes, so the state is renewed at each Present (ws_frame_end).
#define G_CINE_BARS   0x72c8a0u
#define G_BAR_FACTOR  0x68eabcu   // float, height of both bars / 2 (0.2009 x 0.5 x 2 in 0x41baf0)
static int cine, cine_seen;
void ws_cine_bars(CPU *c) {
    (void)c;
    uint8_t *const mb = g_mem;
    if ((int16_t)R16(G_CINE_BARS) >= 0) cine_seen = 1;
}
void ws_frame_end(void) {
    cine = cine_seen;
    cine_seen = 0;
}
static float cine_zoom(void) {
    uint8_t *const mb = g_mem;
    return cine ? ws_cine_zoom(rt_f32(R32(G_BAR_FACTOR))) : 1.f;
}
void ws_scale_x(CPU *c) {
    F_0043ab80_orig(c);
    if (native_scale) { native_scale--; return; }
    int w, h;
    ws_render_size(&w, &h);
    ST(0) *= ws_hor_plus((float)w / (float)h) * cine_zoom();
}
void ws_scale_y(CPU *c) {
    F_0043abc0_orig(c);
    if (native_scale) { native_scale--; return; }
    ST(0) *= cine_zoom();
}

// Calls a recompiled guest function without arguments (cdecl) and returns eax.
static uint32_t guest_call0(CPU *c, void (*f)(CPU *)) {
    uint8_t *const mb = g_mem;
    c->esp -= 4;
    W32(c->esp, 0);   // return address: popped by the callee's ret
    f(c);
    return c->eax;
}

// 0x682ba0 GsX(x): PS2 2D x -> render-target pixels, in ST(0). The 2D (menus, HUD, logos) goes into a centred 4:3
// box; full-screen effects keep the whole width (stretch > 0 while they draw; stretch_next for single call sites).
static int stretch, stretch_next;
void F_00682ba0_orig(CPU *c);
void F_00402ba0(CPU *c);   // current target width
void F_00402bc0(CPU *c);   // current target height
void ws_gsx(CPU *c) {
    uint8_t *const mb = g_mem;
    int x = (int)R32(c->esp + 4);
    F_00682ba0_orig(c);
    int full = stretch > 0 || stretch_next;
    stretch_next = 0;
    if (full) return;
    uint32_t w = guest_call0(c, F_00402ba0), h = guest_call0(c, F_00402bc0);
    ST(0) = ws_gs_x(x, (int)w, (int)h, 0);
}
void ws_stretch_next(CPU *c) { (void)c; stretch_next = 1; }

// Full-screen effects drawn through GsX: lens flares, light haze, fog layer, flare occlusion.
#define STRETCH_WRAP(name, va) \
    void F_##va##_orig(CPU *c); \
    void name(CPU *c) { stretch++; F_##va##_orig(c); stretch--; }
STRETCH_WRAP(ws_fx_lensflare, 00468b40)
STRETCH_WRAP(ws_fx_lensflare_rot, 00468df0)
STRETCH_WRAP(ws_fx_haze, 00479e30)
STRETCH_WRAP(ws_fx_fog, 004794b0)
STRETCH_WRAP(ws_fx_occlusion, 0049fac0)

// Hook before DrawPrimitiveUP(TRIANGLESTRIP, 2, verts, 0x1c) of the full-screen 4:3 pictures: Konami/KCET logos
// (0x5d74fa) and credits (0x59bef9). The quad spans the whole target; it moves into the centred 4:3 box so the
// pictures keep their shape (PC Fix LogosIn43 / CreditIn43). Vertex: x, y, z, rhw, colour, u, v.
void ws_quad43(CPU *c);
// Boot logos (0x5d74fa only): the frame of the last logo picture starts the credits screen once the logos end
// (d3d8/boot_credits.c), then the picture goes into its 4:3 box like the others.
void ws_logo43(CPU *c) {
    extern unsigned g_logo_frame, g_frame;
    g_logo_frame = g_frame;
    ws_quad43(c);
}
void ws_quad43(CPU *c) {
    uint8_t *const mb = g_mem;
    if (R32(c->esp) != 5 || R32(c->esp + 4) != 2 || R32(c->esp + 12) != 0x1c) return;
    uint32_t v = R32(c->esp + 8);
    float w = 0.f, h = 0.f;
    for (int i = 0; i < 4; i++) {
        float x = rt_f32(R32(v + 0x1c * i)), y = rt_f32(R32(v + 0x1c * i + 4));
        if (x + 0.5f > w) w = x + 0.5f;
        if (y + 0.5f > h) h = y + 0.5f;
    }
    if (w <= 0.f || h <= 0.f) return;
    for (int i = 0; i < 4; i++)
        W32(v + 0x1c * i, rt_u32f(ws_quad_x43(rt_f32(R32(v + 0x1c * i)), w, h)));
    if (w * 3.f <= h * 4.f) return;
    // Side bands in the picture's own edge colour (user decision white around the Konami logo instead
    // of black bars), drawn first with the same DrawPrimitiveUP; the device is in eax at both call sites.
    uint32_t dev = c->eax, fn = dev ? R32(R32(dev) + 0x120) : 0;
    if (!fn) return;
    CPU saved = *c;
    float x[4], u[4], ox[4], ou[4];
    for (int i = 0; i < 4; i++) { x[i] = rt_f32(R32(v + 0x1c * i)); u[i] = rt_f32(R32(v + 0x1c * i + 20)); }
    for (int side = 0; side < 2; side++) {
        ws_side_band(x, u, w, side, ox, ou);
        for (int i = 0; i < 4; i++) {
            memcpy(mb + WS_BAND_VERTS + 0x1c * i, mb + v + 0x1c * i, 0x1c);
            W32(WS_BAND_VERTS + 0x1c * i, rt_u32f(ox[i]));
            W32(WS_BAND_VERTS + 0x1c * i + 20, rt_u32f(ou[i]));
        }
        rt_guest_call(c, fn, 5, dev, 5u, 2u, WS_BAND_VERTS, 0x1cu);
    }
    *c = saved;
}

// 0x402e00 (w, h): sets the 3D render-target size (from disp.ini and the options menu). Always the port's size.
void F_00402e00_orig(CPU *c);
void ws_set_render_size(CPU *c) {
    uint8_t *const mb = g_mem;
    int w, h;
    ws_render_size(&w, &h);
    W32(c->esp + 4, w);
    W32(c->esp + 8, h);
    F_00402e00_orig(c);
}

// 0x403e70 (x0, y0, x1, y1): sets the movie rectangle ([0x72c070..0x72c07c], floats). The game letterboxes every
// movie to 6/7 of the height; ws_movie_rect lets the 16:9 movies of the HD collection fill the screen and keeps
// 4:3 movies unstretched. ws_movie_rect_update re-applies it when a movie is opened (its size is known then).
#define G_MOVIE_RECT 0x72c070u
extern int g_movie_w, g_movie_h;
static float movie_game_rect[4];
static int movie_rect_set;
void ws_movie_rect_update(void) {
    uint8_t *const mb = g_mem;
    if (!movie_rect_set) return;
    float r[4];
    ws_movie_rect(movie_game_rect, g_movie_w, g_movie_h, r);
    for (int i = 0; i < 4; i++) W32(G_MOVIE_RECT + 4 * i, rt_u32f(r[i]));
}
void ws_set_movie_rect(CPU *c) {
    uint8_t *const mb = g_mem;
    for (int i = 0; i < 4; i++) movie_game_rect[i] = rt_f32(R32(c->esp + 4 + 4 * i));
    movie_rect_set = 1;
    ws_movie_rect_update();
    c->esp += 4;
}
