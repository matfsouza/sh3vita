#pragma once
#include <SDL_gamecontroller.h>

// Vita buttons -> the DirectInput buttons SH3 reads, laid out like the PS2 (docs/mapa/Entrada e controles.md, BUG-039).
// Meaning of each DI button in SH3's default pad configuration, measured on the PC build with a virtual pad: the map
// opened on 0, the pause menu confirmed on 1 and closed on 2, the flashlight switched on 3, the camera went behind on
// 4, the posture changed on 5 (ready weapon), the inventory opened on 8 and the pause menu on 9. 10 and 11 show
// nothing while standing still: both ride along with circle so that one of them runs when moving. 6 and 7 (side steps,
// as in SH4) are left out: the Vita has no buttons for them and the PS2 layout does not need them.
enum {
    SH3_DI_MAP = 0, SH3_DI_ACTION = 1, SH3_DI_CANCEL = 2, SH3_DI_LIGHT = 3, SH3_DI_CAMERA = 4, SH3_DI_AIM = 5,
    SH3_DI_INVENTORY = 8, SH3_DI_PAUSE = 9, SH3_DI_RUN = 10, SH3_DI_RUN2 = 11
};

// raw: bit SDL_CONTROLLER_BUTTON_x per pressed button. Returns bit i per DI button i (0..11). The d-pad is not a DI
// button: it stays the POV hat (menus, item selection).
static inline unsigned sh3_pad_buttons(unsigned raw) {
#define SH3_PB(b) ((raw >> SDL_CONTROLLER_BUTTON_##b) & 1u)
    unsigned out = 0;
    if (SH3_PB(A)) out |= 1u << SH3_DI_ACTION;                                          // cross: action / confirm / attack
    if (SH3_PB(B)) out |= (1u << SH3_DI_CANCEL) | (1u << SH3_DI_RUN) | (1u << SH3_DI_RUN2);   // circle: back / run
    if (SH3_PB(Y)) out |= 1u << SH3_DI_INVENTORY;                                       // triangle: inventory
    if (SH3_PB(X)) out |= 1u << SH3_DI_LIGHT;                                           // square: flashlight
    if (SH3_PB(RIGHTSHOULDER)) out |= 1u << SH3_DI_AIM;                                 // R: ready weapon
    if (SH3_PB(LEFTSHOULDER)) out |= 1u << SH3_DI_CAMERA;                               // L: camera behind
    if (SH3_PB(START)) out |= 1u << SH3_DI_PAUSE;                                       // start: pause menu
    if (SH3_PB(BACK)) out |= 1u << SH3_DI_MAP;                                          // select: map
#undef SH3_PB
    return out;
}
