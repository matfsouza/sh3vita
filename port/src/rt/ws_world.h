// Effects projected from the 3D world keep the whole 16:9 width. Pure: tested by tests/test_ws_world.py.
// 0x59ef90 projects the effect system's points (blood, footprints, other floor and air effects) with the 3D projection,
// which is Hor+ in 16:9 (widescreen.c ws_scale_x); the GS packet translator (0x5ebdd0, 0x5eca10) then turns them into
// pixels through GsX, which puts 2D into the centred 4:3 box, so these effects slid away from the floor. The integer
// GS x values (12.4 fixed point, low 16 bits) 0x59ef90 wrote this frame are remembered; GsX called from the packet
// translator with one of them gives the full width.
#pragma once
#include <stdint.h>
#include <string.h>

#define WS_WORLD_SLOTS 16384u
typedef struct { uint32_t frame[WS_WORLD_SLOTS]; uint16_t x[WS_WORLD_SLOTS]; } WsWorld;

static inline void ws_world_reset(WsWorld *w) { memset(w, 0, sizeof *w); }
static inline unsigned ws_world_slot(uint32_t x) { return ((x & 0xffffu) * 2654435761u >> 18) & (WS_WORLD_SLOTS - 1u); }
// frame + 1 is stored, so frame 0 is a real frame
static inline void ws_world_add(WsWorld *w, uint32_t frame, uint32_t x) {
    unsigned h = ws_world_slot(x);
    for (unsigned i = 0; i < 64; i++, h = (h + 1) & (WS_WORLD_SLOTS - 1u)) {
        if (w->frame[h] != frame + 1u) { w->frame[h] = frame + 1u; w->x[h] = (uint16_t)x; return; }
        if (w->x[h] == (uint16_t)x) return;
    }
}
static inline int ws_world_has(const WsWorld *w, uint32_t frame, uint32_t x) {
    unsigned h = ws_world_slot(x);
    for (unsigned i = 0; i < 64; i++, h = (h + 1) & (WS_WORLD_SLOTS - 1u)) {
        if (w->frame[h] != frame + 1u) return 0;
        if (w->x[h] == (uint16_t)x) return 1;
    }
    return 0;
}
// return addresses of the GsX calls inside the GS packet translator (0x5ebdd0..0x5ec420, 0x5eca10..0x5ecb00)
static inline int ws_world_caller(uint32_t ret) {
    return (ret >= 0x5ebdd0u && ret < 0x5ec420u) || (ret >= 0x5eca10u && ret < 0x5ecb00u);
}
