// Boot credits and version label (2026-11-17, release v1). From the first frame until the first logo, ui/loading.tga
// (tools/make_livearea.py) covers the black boot screen.
// - After the Konami/KCET logos (their last picture is drawn through the 0x5d74fa hook, widescreen.c ws_logo43), a
//   black screen shows ui/credits.tga (tools/make_credits.ps1) for 8 seconds, fading in and out; any button skips it
//   after 1.5 s. The game keeps running underneath; its input is held neutral meanwhile (dinput.c g_credits_active).
// - ui/version.tga ("v1") sits in the bottom-right corner of the main menu: after the credits, until the player is in
//   the game world (the first collision check, perf_natives.c g_world_seen), never over a movie.
// Both images are private host textures in guest memory, like the controls atlas (console_ui.c); the credits one
// (4 MB) is released as soon as the screen closes.
#include "console_ui.h"
#include "prep.h"
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>

unsigned g_logo_frame;          // game frame of the last boot logo picture (widescreen.c ws_logo43)
unsigned g_btncfg_frame;        // game frame the Button Config screen was last drawn (hook below)
extern int g_vita_bindings_open;   // console_ui.c: draw into the native 960x544 target, shown instead of the game

// Hook at the entry of 0x5ee080, which draws the options' Button Config page (it reads the table of input names at
// 0x71f848: "anaX", "pov%d"...). Remapping there would break the Vita layout, so that page is covered by the Controls
// picture and only circle (back) reaches the game while it is open (dinput.c).
void fix_btncfg_seen(CPU *c) { (void)c; extern unsigned g_frame; g_btncfg_frame = g_frame ? g_frame : 1; }
int sh3_btncfg_open(void) { extern unsigned g_frame; return g_btncfg_frame && g_frame - g_btncfg_frame <= 2; }
int g_credits_active;           // input held neutral while the credits show (dinput.c)
int g_credits_skip;             // a physical button went down during the credits (dinput.c)
int g_world_seen;               // the player reached the game world (perf_natives.c nat_seg_quad)
extern unsigned g_frame;
extern int g_movie_w;
extern uint64_t g_mem_tex;
void rt_input_neutralize(void);

static Texture *load_tga(const char *name) {
#ifdef __vita__
    char path[96]; snprintf(path, sizeof path, "app0:ui/%s", name);
#else
    char path[160]; snprintf(path, sizeof path, "../../port/assets/credits/%s", name);
#endif
    FILE *f = fopen(path, "rb");
    unsigned char h[18];
    if (!f) { rt_log("[credits] cannot open %s\n", path); return NULL; }
    if (fread(h, 1, 18, f) != 18 || h[2] != 2 || h[16] != 32 || !(h[17] & 0x20)) {
        fclose(f); rt_log("[credits] %s: not a 32-bit top-left TGA\n", name); return NULL;
    }
    uint32_t w = h[12] | h[13] << 8, hh = h[14] | h[15] << 8, size = w * hh * 4;
    uint32_t mem = (w && hh && w <= 1024 && hh <= 1024) ? gheap_alloc(size) : 0;
    if (!mem) { fclose(f); rt_log("[credits] %s: no memory\n", name); return NULL; }
    int ok = fread(g_mem + mem, 1, size, f) == size;
    fclose(f);
    Texture *t = calloc(1, sizeof *t); Surface *s = calloc(1, sizeof *s); Surface **lv = calloc(1, sizeof *lv);
    if (!ok || !t || !s || !lv) { free(t); free(s); free(lv); gheap_free(mem); return NULL; }
    t->com.kind = K_TEXTURE; t->com.refs = 1; t->w = w; t->h = hh; t->levels = 1; t->fmt = FMT_A8R8G8B8; t->pool = 1;
    t->lv = lv; lv[0] = s; s->tex = t; s->w = w; s->h = hh; s->fmt = FMT_A8R8G8B8; s->mem = mem;
    t->dirty = 1;
    g_mem_tex += size;
    return t;
}

static void image(Texture *t, float x, float y, float w, float h, float alpha) {
    if (alpha <= 0.f) return;
    uint32_t a = (uint32_t)(alpha * 255.f + 0.5f);
    uint32_t col = (a > 255 ? 255u : a) << 24 | 0xffffffu;
    const float pos[4] = {x, y, x + w, y + h}, uv[4] = {0, 0, w, h};
    console_ui_glyph(t, pos, uv, col, col);
}

void boot_credits_present(void) {
    static int state;           // 0 waiting for the logos, 1 credits on screen, 2 done
    static Texture *credits, *version;
    static uint32_t t0;
    static Texture *loading;
    static int loading_tried;
    if (state == 0 && !g_logo_frame) {   // boot, before the first logo: the loading picture (ui/loading.tga)
        if (!loading_tried) { loading_tried = 1; loading = load_tga("loading.tga"); }
        if (loading && console_ui_begin()) {
            console_ui_fill(0, 0, 960, 544, 0xff000000u);
            image(loading, 0, 0, 960, 544, 1.f);
            console_ui_end();
        }
        return;
    }
    if (loading) { d3d_release(&loading->com); loading = NULL; }
    if (state == 0) {
        if (g_frame < g_logo_frame + 3) return;   // logos still on screen
        credits = load_tga("credits.tga");
        if (!credits) { state = 2; return; }
        state = 1; t0 = SDL_GetTicks(); g_credits_skip = 0; g_credits_active = 1;
        rt_log("[credits] shown at frame %u\n", g_frame);
    }
    if (state == 1) {
        uint32_t ms = SDL_GetTicks() - t0;
        if (ms >= 8000 || (g_credits_skip && ms >= 1500)) {
            state = 2; g_credits_active = 0;
            rt_input_neutralize();   // a button held to skip must not reach the menu
            d3d_release(&credits->com); credits = NULL;
            return;
        }
        float a = ms < 600 ? ms / 600.f : ms > 7400 ? (8000 - ms) / 600.f : 1.f;
        g_vita_bindings_open = 1;   // native 960x544: the small lines stay sharp (the 3D target is 640x362)
        if (console_ui_begin()) {
            console_ui_fill(0, 0, 960, 544, 0xff000000u);
            image(credits, 0, 0, 960, 544, a);
            console_ui_end();
        }
        g_vita_bindings_open = 0;
        return;
    }
    {   // Button Config open: the Controls picture instead (ui/controls_help.tga, tools/make_controls_help.ps1).
        // Fades in when the page opens and out when it closes (0.3 s), over the game's own picture like the menu's
        // transitions; fully shown, it is drawn at the native 960x544 so the text stays sharp.
        static Texture *help;
        static int help_tried;
        static float alpha;
        static uint32_t last;
        uint32_t now = SDL_GetTicks();
        float dt = last ? (now - last) / 300.f : 0.f;
        last = now;
        int open = sh3_btncfg_open();
        if (open) {
            if (!help_tried) { help_tried = 1; help = load_tga("controls_help.tga"); }
            alpha = alpha + dt > 1.f ? 1.f : alpha + dt;
        } else alpha = alpha - dt < 0.f ? 0.f : alpha - dt;
        if (help && alpha > 0.f) {
            g_vita_bindings_open = alpha >= 1.f;
            if (console_ui_begin()) {
                // opening: opaque black at once (the game's own Button Config page never shows through), the picture
                // fades in over it; closing: everything fades out onto the options menu the player goes back to
                uint32_t a = open ? 255u : (uint32_t)(alpha * 255.f);
                console_ui_fill(0, 0, 960, 544, a << 24);
                image(help, 0, 0, 960, 544, alpha);
                console_ui_end();
            }
            g_vita_bindings_open = 0;
            return;
        }
    }
    if (g_world_seen || g_movie_w) return;
    if (!version) { version = load_tga("version.tga"); if (!version) { g_world_seen = 1; return; } }
    if (!console_ui_begin()) return;
    image(version, 960 - 128 - 14, 544 - 32 - 10, 128, 32, 1.f);
    console_ui_end();
}
