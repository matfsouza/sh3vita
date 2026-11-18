// Direct3D 8 state blocks: a block holds only the states it recorded. Pure: tested by tests/test_stateblock.py.
// BeginStateBlock / Set* / EndStateBlock records the states set in between (the device is not changed meanwhile);
// ApplyStateBlock sets those states again, CaptureStateBlock refreshes them from the device; CreateStateBlock records
// everything. The port used to snapshot and restore the whole device: applying the game's start-up render presets
// (0x413xxx) brought back the start-up vertex-shader constants (all zero) and the 3D sprites drawn after it collapsed.
#pragma once
#include <stdint.h>
#include <string.h>

#define SB_RS 256
#define SB_STAGES 8
#define SB_TSS 32
#define SB_VSC 96

typedef struct {
    uint32_t rs[SB_RS];
    uint32_t tss[SB_STAGES][SB_TSS];
    float vsc[SB_VSC][4];
    uint32_t vs, ps;
    uint32_t tex[SB_STAGES];   // the textures' guest handles (0: none)
} SBState;

typedef struct {
    SBState v;
    uint8_t m_rs[SB_RS], m_tss[SB_STAGES][SB_TSS], m_vsc[SB_VSC], m_tex[SB_STAGES], m_vs, m_ps;
} SB;

static inline void sb_clear(SB *b) { memset(b, 0, sizeof *b); }
static inline void sb_mark_all(SB *b) {
    memset(b->m_rs, 1, sizeof b->m_rs); memset(b->m_tss, 1, sizeof b->m_tss); memset(b->m_vsc, 1, sizeof b->m_vsc);
    memset(b->m_tex, 1, sizeof b->m_tex); b->m_vs = b->m_ps = 1;
}
static inline void sb_set_rs(SB *b, uint32_t i, uint32_t v) { if (i < SB_RS) { b->v.rs[i] = v; b->m_rs[i] = 1; } }
static inline void sb_set_tss(SB *b, uint32_t s, uint32_t i, uint32_t v) {
    if (s < SB_STAGES && i < SB_TSS) { b->v.tss[s][i] = v; b->m_tss[s][i] = 1; }
}
static inline void sb_set_vsc(SB *b, uint32_t r, const float *src, uint32_t n) {
    for (uint32_t k = 0; k < n && r + k < SB_VSC; k++) { memcpy(b->v.vsc[r + k], src + 4 * k, 16); b->m_vsc[r + k] = 1; }
}
static inline void sb_set_vs(SB *b, uint32_t h) { b->v.vs = h; b->m_vs = 1; }
static inline void sb_set_ps(SB *b, uint32_t h) { b->v.ps = h; b->m_ps = 1; }
static inline void sb_set_tex(SB *b, uint32_t s, uint32_t h) { if (s < SB_STAGES) { b->v.tex[s] = h; b->m_tex[s] = 1; } }

// from = 1: block -> state (apply); from = 0: state -> block (capture). Only the recorded entries move.
static inline void sb_move(SB *b, SBState *s, int apply) {
#define SB_MOVE(dst, src, n) do { if (apply) memcpy(&(dst), &(src), n); else memcpy(&(src), &(dst), n); } while (0)
    for (int i = 0; i < SB_RS; i++) if (b->m_rs[i]) SB_MOVE(s->rs[i], b->v.rs[i], 4);
    for (int t = 0; t < SB_STAGES; t++)
        for (int i = 0; i < SB_TSS; i++) if (b->m_tss[t][i]) SB_MOVE(s->tss[t][i], b->v.tss[t][i], 4);
    for (int i = 0; i < SB_VSC; i++) if (b->m_vsc[i]) SB_MOVE(s->vsc[i], b->v.vsc[i], 16);
    for (int t = 0; t < SB_STAGES; t++) if (b->m_tex[t]) SB_MOVE(s->tex[t], b->v.tex[t], 4);
    if (b->m_vs) SB_MOVE(s->vs, b->v.vs, 4);
    if (b->m_ps) SB_MOVE(s->ps, b->v.ps, 4);
#undef SB_MOVE
}
static inline void sb_apply(SB *b, SBState *s) { sb_move(b, s, 1); }
static inline void sb_capture(SB *b, const SBState *s) { sb_move(b, (SBState *)s, 0); }
