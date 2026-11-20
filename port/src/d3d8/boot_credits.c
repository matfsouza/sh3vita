// Boot screen, version label and the Controls screen. From the first frame until the first logo, ui/loading.tga
// (tools/make_prep_screen.ps1 -Loading: artwork, logo, author, repository) covers the black boot screen.
// - ui/version.tga ("v1") sits in the bottom-right corner of the main menu: after the Konami/KCET logos (their last
//   picture is drawn through the 0x5d74fa hook, widescreen.c ws_logo43), until the player is in the game world (the
//   first collision check, perf_natives.c g_world_seen), never over a movie.
// - The black credits screen that followed the logos was removed; the full credits are in the README.
// The images are private host textures in guest memory, like the controls atlas (console_ui.c).
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

// The VPK stores these pictures gzip-compressed (tools/gz_assets.py: fewer bytes to install); zlib's gzread also
// reads a plain file. The PC build reads the plain pictures of port/assets.
#ifdef __vita__
#include <zlib.h>
typedef gzFile TgaFile;
#define TGA_OPEN(p) gzopen((p), "rb")
#define TGA_READ(f, b, n) (gzread((f), (b), (unsigned)(n)) == (int)(n))
#define TGA_CLOSE(f) gzclose(f)
#else
typedef FILE *TgaFile;
#define TGA_OPEN(p) fopen((p), "rb")
#define TGA_READ(f, b, n) (fread((b), 1, (n), (f)) == (size_t)(n))
#define TGA_CLOSE(f) fclose(f)
#endif
// Opens a 32-bit top-left TGA and reads its header: the file is left at the pixels (NULL if missing or another kind).
static TgaFile tga_open(const char *name, uint32_t *w, uint32_t *h) {
#ifdef __vita__
    char path[96]; snprintf(path, sizeof path, "app0:ui/%s", name);
#else
    char path[160]; snprintf(path, sizeof path, "../../port/assets/credits/%s", name);
#endif
    TgaFile f = TGA_OPEN(path);
    unsigned char hd[18];
    if (!f) { rt_log("[credits] cannot open %s\n", path); return NULL; }
    if (!TGA_READ(f, hd, 18) || hd[2] != 2 || hd[16] != 32 || !(hd[17] & 0x20)) {
        TGA_CLOSE(f); rt_log("[credits] %s: not a 32-bit top-left TGA\n", name); return NULL;
    }
    *w = hd[12] | hd[13] << 8; *h = hd[14] | hd[15] << 8;
    if (!*w || !*h || *w > 1024 || *h > 1024) { TGA_CLOSE(f); return NULL; }
    return f;
}
// Wraps pixels already in guest memory (w * h * 4 bytes at mem) as a private texture; NULL frees mem.
static Texture *tex_wrap(uint32_t mem, uint32_t w, uint32_t h) {
    Texture *t = calloc(1, sizeof *t); Surface *s = calloc(1, sizeof *s); Surface **lv = calloc(1, sizeof *lv);
    if (!t || !s || !lv) { free(t); free(s); free(lv); gheap_free(mem); return NULL; }
    t->com.kind = K_TEXTURE; t->com.refs = 1; t->w = w; t->h = h; t->levels = 1; t->fmt = FMT_A8R8G8B8; t->pool = 1;
    t->lv = lv; lv[0] = s; s->tex = t; s->w = w; s->h = h; s->fmt = FMT_A8R8G8B8; s->mem = mem;
    t->dirty = 1;
    g_mem_tex += w * h * 4;
    return t;
}
static Texture *load_tga(const char *name) {
    uint32_t w, h;
    TgaFile f = tga_open(name, &w, &h);
    if (!f) return NULL;
    uint32_t size = w * h * 4, mem = gheap_alloc(size);
    if (!mem) { TGA_CLOSE(f); rt_log("[credits] %s: no memory\n", name); return NULL; }
    int ok = TGA_READ(f, g_mem + mem, size);
    TGA_CLOSE(f);
    if (!ok) { gheap_free(mem); return NULL; }
    return tex_wrap(mem, w, h);
}

// Controls screen pictures (ui/controls_<layout>.tga, 960x544). The open layout is read at once (the screen is black
// meanwhile); the two others are read by a thread into guest buffers the game thread allocated, so changing layout
// never stalls on the memory card.
#define CTL_N 3
#define CTL_W 960
#define CTL_H 544
static Texture *ctl_pic[CTL_N];
static uint32_t ctl_mem[CTL_N];            // guest buffer handed to the reader thread
static SDL_atomic_t ctl_state[CTL_N];      // 0 nothing, 1 reading, 2 read, 3 failed
static SDL_Thread *ctl_thread;
static int ctl_lang;                       // game language of the pictures: 0 Japanese, 1 English, 2 French, 3 German,
                                           // 4 Italian, 5 Spanish, 6 Korean, 7 Chinese
// The game's language: in the options menu the row being edited ([0xbcc1a1], applied when the menu closes), so a
// language picked a moment ago is the one the Controls screen speaks; elsewhere the language in use ([0x70e66b1]).
static int game_language(void) {
    extern int sh3_options_open(void);
    int l = (int8_t)g_mem[sh3_options_open() ? 0xbcc1a1u : 0x70e66b1u];
    return l >= 0 && l <= 7 ? l : 1;
}
static void ctl_name(int i, char *out, size_t n) { snprintf(out, n, "controls_%d_%d.tga", ctl_lang, i); }
static int ctl_reader(void *arg) {
    (void)arg;
    for (int i = 0; i < CTL_N; i++) {
        if (SDL_AtomicGet(&ctl_state[i]) != 1) continue;
        char name[32]; ctl_name(i, name, sizeof name);
        uint32_t w = 0, h = 0;
        TgaFile f = tga_open(name, &w, &h);
        int ok = f && w == CTL_W && h == CTL_H && TGA_READ(f, g_mem + ctl_mem[i], CTL_W * CTL_H * 4);
        if (f) TGA_CLOSE(f);
        SDL_AtomicSet(&ctl_state[i], ok ? 2 : 3);
    }
    return 0;
}
static void ctl_join(void) { if (ctl_thread) { SDL_WaitThread(ctl_thread, NULL); ctl_thread = NULL; } }
// The picture of layout i, NULL if it cannot be read. wait: block until the reader thread is done with it.
static Texture *ctl_get(int i, int wait) {
    if (ctl_pic[i]) return ctl_pic[i];
    int st = SDL_AtomicGet(&ctl_state[i]);
    if (st == 1 && wait) { ctl_join(); st = SDL_AtomicGet(&ctl_state[i]); }
    if (st == 2) ctl_pic[i] = tex_wrap(ctl_mem[i], CTL_W, CTL_H);
    else if (st == 3) gheap_free(ctl_mem[i]);
    if (st == 2 || st == 3) { ctl_mem[i] = 0; SDL_AtomicSet(&ctl_state[i], 0); }
    if (!ctl_pic[i] && st != 1 && wait) {   // never queued, or the thread failed: read it here
        char name[32]; ctl_name(i, name, sizeof name);
        ctl_pic[i] = load_tga(name);
    }
    return ctl_pic[i];
}
static void ctl_open(int current) {
    ctl_lang = game_language();
    if (!ctl_get(current, 1) && ctl_lang != 1) { ctl_lang = 1; ctl_get(current, 1); }   // missing translation: English
    int queued = 0;
    for (int i = 0; i < CTL_N; i++) {
        if (i == current || ctl_pic[i]) continue;
        ctl_mem[i] = gheap_alloc(CTL_W * CTL_H * 4);
        if (ctl_mem[i]) { SDL_AtomicSet(&ctl_state[i], 1); queued = 1; }
    }
    if (queued) {
        ctl_thread = SDL_CreateThread(ctl_reader, "ctl-pics", NULL);
        if (!ctl_thread) ctl_reader(NULL);
    }
}
static void ctl_close(void) {
    ctl_join();
    for (int i = 0; i < CTL_N; i++) {
        ctl_get(i, 0);   // a buffer the thread filled becomes a texture, released just below
        if (ctl_pic[i]) { d3d_release(&ctl_pic[i]->com); ctl_pic[i] = NULL; }
    }
}

static void image(Texture *t, float x, float y, float w, float h, float alpha) {
    if (alpha <= 0.f) return;
    uint32_t a = (uint32_t)(alpha * 255.f + 0.5f);
    uint32_t col = (a > 255 ? 255u : a) << 24 | 0xffffffu;
    const float pos[4] = {x, y, x + w, y + h}, uv[4] = {0, 0, w, h};
    console_ui_glyph(t, pos, uv, col, col);
}

void boot_credits_present(void) {
    static int state;           // 0 boot and logos, 2 after them (1 was the credits screen, removed)
    static Texture *version;
    static Texture *loading;
    static int loading_tried;
    if (state == 0 && !g_logo_frame) {   // boot, before the first logo: the loading picture (ui/loading.tga)
        if (!loading_tried) { loading_tried = 1; loading = load_tga("loading.tga"); }
        g_vita_bindings_open = 1;   // native 960x544, as the credits: the 3D target would blur its small text
        if (loading && console_ui_begin()) {
            console_ui_fill(0, 0, 960, 544, 0xff000000u);
            image(loading, 0, 0, 960, 544, 1.f);
            console_ui_end();
        }
        g_vita_bindings_open = 0;
        return;
    }
    if (loading) { d3d_release(&loading->com); loading = NULL; }
    // The black credits screen after the logos was removed: the author, the repository
    // and the unofficial-port notice are on the loading screen and the LiveArea; the full credits are in the README.
    if (state == 0) {
        if (g_frame < g_logo_frame + 3) return;   // logos still on screen: no version label over them
        state = 2;
    }
    {   // Button Config open: the Controls screen instead, the picture of the chosen control layout
        // (ui/controls_<layout>.tga, tools/make_controls_help.ps1; left / right change it, dinput.c).
        // Opening: opaque black at once (the game's own Button Config page never shows through), then the picture
        // fades in over 0.25 s, all at the native 960x544 so the text stays sharp from the first frame. Changing layout:
        // 0.15 s cross-fade. Closing: everything fades out (0.2 s) onto the options menu the player goes back to.
        // Steps use the real time but never more than 50 ms per frame, so a slow frame cannot skip a fade.
        static int shown = -1, prev = -1, was_open;
        static float alpha, cross = 1.f;
        static uint32_t last;
        extern int sh3_control_scheme(void);
        uint32_t now = SDL_GetTicks();
        float ms = last ? (float)(now - last) : 0.f;
        last = now;
        if (ms > 50.f) ms = 50.f;
        int open = sh3_btncfg_open();
        if (open) {
            int scheme = sh3_control_scheme();
            if (scheme < 0 || scheme >= CTL_N) scheme = 0;
            if (!was_open) { ctl_open(scheme); shown = scheme; prev = -1; cross = 1.f; ms = 0.f; }
            else if (scheme != shown && ctl_get(scheme, 1)) { prev = shown; shown = scheme; cross = 0.f; }
            for (int i = 0; i < CTL_N; i++) ctl_get(i, 0);   // pick up what the reader thread finished
            alpha = alpha + ms / 250.f > 1.f ? 1.f : alpha + ms / 250.f;
            cross = cross + ms / 150.f > 1.f ? 1.f : cross + ms / 150.f;
        } else alpha = alpha - ms / 200.f < 0.f ? 0.f : alpha - ms / 200.f;
        was_open = open;
        if (!open && alpha <= 0.f && shown >= 0) { ctl_close(); shown = prev = -1; }
        Texture *cur = shown >= 0 ? ctl_pic[shown] : NULL, *old = prev >= 0 && cross < 1.f ? ctl_pic[prev] : NULL;
        if (cur && (open || alpha > 0.f)) {   // open: the black is drawn even before the picture starts to show
            g_vita_bindings_open = open;
            if (console_ui_begin()) {
                uint32_t a = open ? 255u : (uint32_t)(alpha * 255.f);
                console_ui_fill(0, 0, 960, 544, a << 24);
                if (old) image(old, 0, 0, 960, 544, alpha);
                image(cur, 0, 0, 960, 544, old ? alpha * cross : alpha);
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
