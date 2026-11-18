// Tests for port/src/rt/ws_layout.h: the 16:9 screen layout of the port (M4).
#include <math.h>
#include <stdio.h>
#include "ws_layout.h"

static int fails;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

int main(void) {
    int w, h;
    // The Vita screen is the display; the 3D renders at a lower size with the same shape (no stretching).
    CHECK(WS_DISPLAY_W == 960 && WS_DISPLAY_H == 544);
    ws_parse_res(NULL, &w, &h);
    CHECK(w == 790 && h == 448);
    CHECK(fabs((double)w / h - 960.0 / 544.0) < 0.002);
    ws_parse_res("640x362", &w, &h);
    CHECK(w == 640 && h == 362);
    ws_parse_res("bad", &w, &h);
    CHECK(w == 790 && h == 448);
    ws_parse_res("4000x3000", &w, &h);   // never more than the display
    CHECK(w == 960 && h == 544);
    ws_parse_res("100x50", &w, &h);      // never below a usable size
    CHECK(w == 320 && h == 180);
    // Hor+: the horizontal projection scale shrinks by (4:3)/(render aspect), so 16:9 shows more on the sides;
    // the vertical framing stays the PS2's.
    CHECK(fabs(ws_hor_plus(16.0f / 9.0f) - 0.75f) < 1e-6);
    CHECK(fabs(ws_hor_plus(4.0f / 3.0f) - 1.0f) < 1e-6);
    CHECK(fabs(ws_hor_plus(790.0f / 448.0f) - (4.0f / 3.0f) / (790.0f / 448.0f)) < 1e-6);
    CHECK(ws_hor_plus(0.f) == 1.0f);   // degenerate input keeps the game's scale
    // 2D: PS2 GS x (12.4 fixed point, 512 wide, centred on 2048) -> render-target pixels. In 4:3 mode the
    // 512-wide space fills a 4:3 box centred in the 16:9 target; stretch mode (full-screen effects) fills it all.
    int L = (2048 - 256) * 16, C = 2048 * 16, R = (2048 + 256) * 16;
    float w43 = 448.f * 4.f / 3.f;
    CHECK(fabs(ws_gs_x(C, 790, 448, 0) - 395.f) < 1e-3);
    CHECK(fabs(ws_gs_x(C, 790, 448, 1) - 395.f) < 1e-3);
    CHECK(fabs(ws_gs_x(L, 790, 448, 0) - (790.f - w43) / 2.f) < 1e-3);
    CHECK(fabs(ws_gs_x(R, 790, 448, 0) - (790.f + w43) / 2.f) < 1e-3);
    CHECK(fabs(ws_gs_x(L, 790, 448, 1) - 0.f) < 1e-3);
    CHECK(fabs(ws_gs_x(R, 790, 448, 1) - 790.f) < 1e-3);
    CHECK(fabs(ws_gs_x(L, 640, 480, 0) - 0.f) < 1e-3);     // 4:3 target: the game's own mapping
    CHECK(fabs(ws_gs_x(R, 640, 480, 0) - 640.f) < 1e-3);
    // Full-screen 4:3 pictures (logos, credits) drawn as a -0.5..w-0.5 quad: moved into the centred 4:3 box.
    CHECK(fabs(ws_quad_x43(-0.5f, 790.f, 448.f) - ((790.f - w43) / 2.f - 0.5f)) < 1e-3);
    CHECK(fabs(ws_quad_x43(789.5f, 790.f, 448.f) - ((790.f + w43) / 2.f - 0.5f)) < 1e-3);
    CHECK(fabs(ws_quad_x43(-0.5f, 640.f, 480.f) - -0.5f) < 1e-3);   // 4:3 target unchanged
    // In-engine cutscenes: the PS2 bars hid bar_total (0.2009) of the height; without them the picture is enlarged
    // so the screen shows exactly the rows between the bars. Degenerate factors keep the game's view.
    CHECK(fabs(ws_cine_zoom(0.20089285f) - 1.0f / (1.0f - 0.20089285f)) < 1e-5);
    CHECK(fabs(448.f / ws_cine_zoom(0.20089285f) - 358.f) < 0.1f);   // 45 rows per bar hidden on the PS2
    CHECK(ws_cine_zoom(0.f) == 1.f && ws_cine_zoom(-1.f) == 1.f && ws_cine_zoom(0.9f) == 1.f);
    // Side bands of a 4:3 picture in a wide screen (logos): each band runs from the screen edge to the picture
    // and samples only the picture's edge column, so the picture's own background colour reaches the edges.
    {
        float x[4] = {116.5f, 672.5f, 116.5f, 672.5f}, u[4] = {0.f, 1.f, 0.f, 1.f}, ox[4], ou[4];
        ws_side_band(x, u, 790.f, 0, ox, ou);
        CHECK(ox[0] == -0.5f && ox[2] == -0.5f && ox[1] == 116.5f && ox[3] == 116.5f);
        CHECK(ou[0] == ou[1] && ou[1] == ou[2] && ou[2] == ou[3] && ou[0] > 0.f && ou[0] < 0.01f);
        ws_side_band(x, u, 790.f, 1, ox, ou);
        CHECK(ox[0] == 672.5f && ox[2] == 672.5f && ox[1] == 789.5f && ox[3] == 789.5f);
        CHECK(ou[0] == ou[3] && ou[0] < 1.f && ou[0] > 0.99f);
    }
    // Movie rectangle (x0, y0, x1, y1): the game letterboxes every movie to 6/7 of the height (0x59c850).
    // A wide movie (HD collection, 16:9) fills the screen with its own shape; a 4:3 movie keeps the game's
    // letterbox inside a centred 4:3 box; before the movie size is known the game's rectangle stays.
    float game[4] = {0.f, 32.f, 790.f, 416.f}, r[4];
    ws_movie_rect(game, 1280, 720, r);
    CHECK(fabs(r[0] - 0.f) < 0.5f && fabs(r[2] - 790.f) < 0.5f);
    CHECK(fabs((r[2] - r[0]) / (r[3] - r[1]) - 1280.f / 720.f) < 0.01f);   // its own shape
    CHECK(fabs((r[1] + r[3]) / 2.f - 224.f) < 0.5f);                        // centred
    ws_movie_rect(game, 512, 384, r);
    CHECK(fabs(r[0] - (790.f - w43) / 2.f) < 0.5f && fabs(r[2] - (790.f + w43) / 2.f) < 0.5f);
    CHECK(r[1] == 32.f && r[3] == 416.f);
    ws_movie_rect(game, 0, 0, r);
    CHECK(r[0] == 0.f && r[1] == 32.f && r[2] == 790.f && r[3] == 416.f);
    float tall[4] = {0.f, 34.f, 640.f, 446.f};   // 4:3 screen: a 16:9 movie fits the width
    ws_movie_rect(tall, 1280, 720, r);
    CHECK(fabs(r[0]) < 0.5f && fabs(r[2] - 640.f) < 0.5f && fabs((r[3] - r[1]) - 360.f) < 0.5f);
    if (fails) return 1;
    printf("ok\n");
    return 0;
}
