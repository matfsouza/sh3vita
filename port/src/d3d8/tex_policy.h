// Texture guest copies. Pure: tested by tests/test_tex_policy.py.
// The port keeps, in the game's heap, a copy of every texture level the game locked, even after it reached the GPU.
// A long session (the French one that went black: 58 MB of copies) fragments the heap until a large block (8.7 MB for
// the next room) cannot be found. A level the game filled once and has not touched for a while is released; levels
// the game rewrites (glyph cache, noise), textures made before the player reaches the world (the title menu locks them
// again), render targets and the movie keep theirs. A level locked again after its release keeps its copy from then on.
#pragma once
#include <stdint.h>

#define TEX_RELEASE_IDLE_FRAMES 300u      // 10 s at 30 frames/s
#define TEX_RELEASE_MIN_BYTES   (16u << 10)

typedef struct {
    int has_copy, locks, rect_locked, uploaded, in_flight, made_in_world, render_target, movie, no_release;
    uint32_t frames_idle, pool, bytes;
} TexLevelInfo;

static inline int tex_release_ok(const TexLevelInfo *i) {
    return i->has_copy && i->locks == 1 && !i->rect_locked && i->uploaded && !i->in_flight && i->made_in_world &&
           !i->render_target && !i->movie && !i->no_release && i->pool == 1 &&
           i->frames_idle >= TEX_RELEASE_IDLE_FRAMES && i->bytes >= TEX_RELEASE_MIN_BYTES;
}

// A small rectangle locked in a single-level texture already on the GPU (the glyph cache writes one 20x30 cell per
// new letter in French / Spanish text, into cells no queued draw uses yet) does not wait for the render thread.
static inline int tex_rect_lock_nowait(uint32_t w, uint32_t h, uint32_t levels, int on_gpu, int render_target,
                                       uint32_t rw, uint32_t rh) {
    return levels == 1 && on_gpu && !render_target && rw * rh * 16u <= w * h;
}
