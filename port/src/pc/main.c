// PC entry point: SDL2, loading the game exe, starting the recompiled code.
#include "rt.h"
#include "plat.h"
#include "cp1252.h"
#include <SDL_opengl.h>
#include <stdlib.h>

static SDL_Window *g_win;

SDL_Window *plat_sdl_window(void) { return g_win; }
int plat_width(void) { return 640; }
int plat_height(void) { return 480; }
void plat_show_window(void) { SDL_ShowWindow(g_win); }

int plat_message_box(const char *title, const char *text, unsigned type) {
    // Both GOG call sites are MB_OK startup errors. Do not fabricate acceptance
    // of an unsupported confirmation or of an invisible headless dialogue.
    if ((type & 15u) || getenv("SH3_HEADLESS")) return 0;
    unsigned icon = (type & 0xf0u) == 0x10u ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION;
    char title_utf8[768], text_utf8[6144];
    rt_cp1252_utf8(title_utf8, sizeof title_utf8, title);
    rt_cp1252_utf8(text_utf8, sizeof text_utf8, text);
    return SDL_ShowSimpleMessageBox(icon, title_utf8, text_utf8, g_win) == 0 ? 1 : 0;
}

int plat_pump_events(void) {
    SDL_Event e;
    int quit = 0;
    while (SDL_PollEvent(&e))
        if (e.type == SDL_QUIT) quit = 1;
    return quit;
}

static SDL_GLContext g_gl;
void plat_gl_init(void) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    g_gl = SDL_GL_CreateContext(g_win);
    if (!g_gl) rt_fatal("contexte OpenGL : %s", SDL_GetError());
    SDL_GL_SetSwapInterval(getenv("SH3_NOVSYNC") ? 0 : 1);
}
// Writes the current back buffer as a 24-bit TGA (bottom-up, like glReadPixels).
void plat_shot_raw(const char *path) {
    int w, h;
    SDL_GL_GetDrawableSize(g_win, &w, &h);
    unsigned char *px = malloc((size_t)w * h * 3);
    FILE *f = px ? fopen(path, "wb") : NULL;
    if (!f) { free(px); rt_log("[shot] cannot write %s\n", path); return; }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, px);
    unsigned char hdr[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                             (unsigned char)w, (unsigned char)(w >> 8), (unsigned char)h, (unsigned char)(h >> 8), 24, 0};
    fwrite(hdr, 1, sizeof hdr, f);
    fwrite(px, 1, (size_t)w * h * 3, f);
    fclose(f);
    free(px);
    rt_log("[shot] %s (%dx%d)\n", path, w, h);
}
// SH3_SHOT=N[,M...]: before present N, writes shot_N.tga in the working folder (PC counterpart of the Vita option).
void plat_gl_swap(void) {
    static unsigned n;
    static const char *shots;
    if (!shots) shots = getenv("SH3_SHOT") ? getenv("SH3_SHOT") : "";
    n++;
    for (const char *s = shots; *s;) {
        char *end;
        unsigned want = (unsigned)strtoul(s, &end, 10);
        if (end == s) break;
        if (want == n) { char path[64]; snprintf(path, sizeof path, "shot_%u.tga", n); plat_shot_raw(path); }
        s = *end == ',' ? end + 1 : end;
        if (end == s) break;
    }
    SDL_GL_SwapWindow(g_win);
}
void plat_drawable_size(int *w, int *h) { SDL_GL_GetDrawableSize(g_win, w, h); }

void rt_platform_fatal(const char *msg) {
    if (getenv("SH3_HEADLESS")) return;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "sh3vita", msg, g_win);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : getenv("SH3_GAME_DIR") ? getenv("SH3_GAME_DIR") : "../game";
    SDL_strlcpy(g_game_dir, dir, sizeof g_game_dir);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    int ww = 960, wh = 544;
    if (getenv("SH3_WIN")) sscanf(getenv("SH3_WIN"), "%dx%d", &ww, &wh);   // video capture window
    g_win = SDL_CreateWindow("Silent Hill 3 (sh3vita)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | (getenv("SH3_WIN") ? SDL_WINDOW_BORDERLESS : SDL_WINDOW_RESIZABLE));
    /* Optional PC-only diagnostic: reproduce the guest-memory ceiling of a Vita. */
    unsigned guest_mb = 512;
    const char *guest_limit = getenv("SH3_PC_GUEST_MB");
    if (guest_limit) {
        char *end = NULL;
        unsigned long parsed = strtoul(guest_limit, &end, 10);
        if (end != guest_limit && !*end && parsed >= 64 && parsed <= 512)
            guest_mb = (unsigned)parsed;
    }
    if (rt_init(guest_mb << 20)) rt_fatal("guest memory: allocation failed");
    rt_log("[mem] PC guest memory: %u MB\n", guest_mb);
    char exe[600];
    SDL_snprintf(exe, sizeof exe, "%s/sh3.exe", g_game_dir);
    int r = rt_load_exe(exe);
    if (r) rt_fatal("cannot load %s (%d)", exe, r);
    rt_fill_iat();
    rt_log("game loaded from %s\n", g_game_dir);
    { extern void prof_start(void); prof_start(); }
    rt_run_entry();
    SDL_Quit();
    return 0;
}
