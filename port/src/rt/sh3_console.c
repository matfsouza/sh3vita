// Console-layer state shared by the generic runtime (input, device, console UI).
// In sh4vita these lived in the SH4 options/controls modules (reference/sh4/rt/); the SH3 console options,
// controls screen and camera sensitivity come in a later plan. Until then: neutral values.
#include "rt.h"

unsigned g_cursor_seen = 0;   // set by dev_ShowCursor (device.c)
int g_options_active = 0;     // options menu open: sticks act as a d-pad (dinput.c)
int g_vita_bindings_open = 0; // controls screen open (console_ui.c)

float rt_camera_scale(void) { return 1.0f; }   // camera sensitivity multiplier
void rt_controls_present(void) {               // per-frame hook before Present: boot credits and version label
    void boot_credits_present(void);
    boot_credits_present();
}
