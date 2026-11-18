// Emulated USER32: a single window (SDL), minimal message queue.
#include "win32.h"
#include "plat.h"
#include <SDL.h>

#define U(name) void imp_USER32_##name(CPU *c)
#define HWND_MAIN    0x00010010u
#define HWND_DESKTOP 0x00010014u

static uint32_t g_wndproc;
static int g_quit_posted;

// Messages posted by the host for the window procedure (DirectShow SetNotifyWindow): PeekMessageA returns them and
// DispatchMessageA calls the procedure, as on Windows. Any thread may post (movie decoding threads).
static struct { uint32_t hwnd, msg, wp, lp; } g_posted[32];
static int g_nposted;
static SDL_mutex *g_post_lock;
void w32_post_message(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp) {
    if (!g_post_lock) g_post_lock = SDL_CreateMutex();
    SDL_LockMutex(g_post_lock);
    if (g_nposted < 32) { g_posted[g_nposted].hwnd = hwnd; g_posted[g_nposted].msg = msg; g_posted[g_nposted].wp = wp; g_posted[g_nposted].lp = lp; g_nposted++; }
    SDL_UnlockMutex(g_post_lock);
}

U(RegisterClassExA) { HOSTFN; g_wndproc = R32(ARG(1) + 8); STDRET(1, 0xc001); }
U(UnregisterClassA) { STDRET(2, 1); }
U(CreateWindowExA) {
    HOSTFN;
    rt_log("CreateWindowExA(\"%s\", %ux%u)\n", GSTR(ARG(3)), ARG(7), ARG(8));
    rt_apply_res();
    STDRET(12, HWND_MAIN);
}
U(DestroyWindow) { STDRET(1, 1); }
U(ShowWindow) { plat_show_window(); STDRET(2, 1); }
U(UpdateWindow) { STDRET(1, 1); }
U(GetWindowPlacement) {
    HOSTFN;
    uint32_t p = ARG(2);
    memset(mb + p, 0, 44);
    W32(p, 44);
    W32(p + 8, 1); // SW_SHOWNORMAL
    STDRET(2, 1);
}
U(GetForegroundWindow) { STDRET(0, HWND_MAIN); }
U(GetDesktopWindow) { STDRET(0, HWND_DESKTOP); }
U(LoadIconA) { STDRET(2, 1); }
U(RegisterWindowMessageA) { static uint32_t n = 0xc100; STDRET(1, n++); }
U(SystemParametersInfoA) {
    HOSTFN;
    if (ARG(3) && ARG(1) == 0x10) W32(ARG(3), 0); // SPI_GETSCREENSAVEACTIVE
    STDRET(4, 1);
}
U(DefWindowProcA) { STDRET(4, 0); }
U(MessageBoxA) {
    HOSTFN;
    rt_log("MessageBoxA(\"%s\", \"%s\")\n", GSTR(ARG(3)) ? GSTR(ARG(3)) : "", GSTR(ARG(2)) ? GSTR(ARG(2)) : "");
    int button = plat_message_box(GSTR(ARG(3)), GSTR(ARG(2)), ARG(4));
    if (!button) w32_set_last_error(c, 31); // ERROR_GEN_FAILURE; no implicit answer
    STDRET(4, button);
}
U(PeekMessageA) {
    HOSTFN;
    uint32_t msg = ARG(1);
    if (plat_pump_events() || g_quit_posted) {
        memset(mb + msg, 0, 28);
        W32(msg + 4, 0x12); // WM_QUIT
        STDRET(5, 1);
    }
    int got = 0;
    if (g_post_lock && g_nposted) {
        SDL_LockMutex(g_post_lock);
        if (g_nposted) {
            memset(mb + msg, 0, 28);
            W32(msg, g_posted[0].hwnd); W32(msg + 4, g_posted[0].msg); W32(msg + 8, g_posted[0].wp); W32(msg + 12, g_posted[0].lp);
            if (ARG(5) & 1) memmove(&g_posted[0], &g_posted[1], (size_t)(--g_nposted) * sizeof g_posted[0]);   // PM_REMOVE
            got = 1;
        }
        SDL_UnlockMutex(g_post_lock);
    }
    STDRET(5, got);
}
U(TranslateMessage) { STDRET(1, 0); }
U(DispatchMessageA) {
    HOSTFN;
    uint32_t m = ARG(1), r = 0;
    if (g_wndproc && R32(m + 4) != 0x12) r = rt_guest_call(c, g_wndproc, 4, R32(m), R32(m + 4), R32(m + 8), R32(m + 12));
    STDRET(1, r);
}
U(PostQuitMessage) { g_quit_posted = 1; STDRET(1, 0); }
U(PostMessageA) {
    HOSTFN;
    if (ARG(2) == 0x10 || ARG(2) == 0x12) g_quit_posted = 1; // WM_CLOSE / WM_QUIT
    STDRET(4, 1);
}
U(PostThreadMessageA) { STDRET(4, 1); }
U(GetQueueStatus) { STDRET(1, 0); }
U(MsgWaitForMultipleObjects) {
    HOSTFN;
    uint32_t n = ARG(1), hs[64];
    for (uint32_t i = 0; i < n && i < 64; i++) hs[i] = R32(ARG(2) + 4 * i);
    extern uint64_t g_t_wait;
    uint64_t t0 = SDL_GetPerformanceCounter();
    uint32_t r = w32_wait(n, hs, ARG(3) != 0, ARG(4));
    if (c == g_main_cpu) g_t_wait += SDL_GetPerformanceCounter() - t0;
    STDRET(5, r);
}
U(GetCursorPos) { HOSTFN; W32(ARG(1), 0); W32(ARG(1) + 4, 0); STDRET(1, 1); }
U(SetCursorPos) { STDRET(2, 1); }
U(ClientToScreen) { STDRET(2, 1); }
U(GetClientRect) {
    HOSTFN;
    uint32_t r = ARG(2);
    W32(r, 0); W32(r + 4, 0); W32(r + 8, plat_width()); W32(r + 12, plat_height());
    STDRET(2, 1);
}

// ---------- imported by SH3 only ----------
#include "guest_fmt.h"
#define ONCE(name) do { static int said; if (!said++) rt_log("[user32] %s (stub)\n", name); } while (0)
U(GetWindowRect) {
    HOSTFN;
    uint32_t r = ARG(2);
    W32(r, 0); W32(r + 4, 0); W32(r + 8, plat_width()); W32(r + 12, plat_height());
    STDRET(2, 1);
}
U(GetMessageA) {   // blocking PeekMessage: WM_QUIT -> 0, otherwise a WM_NULL every ~10 ms
    HOSTFN;
    uint32_t msg = ARG(1);
    memset(mb + msg, 0, 28);
    if (plat_pump_events() || g_quit_posted) { W32(msg + 4, 0x12); STDRET(4, 0); }
    SDL_Delay(10);
    STDRET(4, 1);
}
U(GetSystemMetrics) {   // SM_CXSCREEN / SM_CYSCREEN; everything else 0
    HOSTFN;
    STDRET(1, ARG(1) == 0 ? (uint32_t)plat_width() : ARG(1) == 1 ? (uint32_t)plat_height() : 0);
}
U(EnumDisplaySettingsA) {   // (device, mode index or -1 current, DEVMODEA *)
    HOSTFN;
    static const uint16_t modes[][2] = {{640, 480}, {800, 600}, {960, 544}, {1024, 768}, {1280, 720}};
    uint32_t i = ARG(2), dm = ARG(3), w, h;
    if (i == 0xffffffffu || i == 0xfffffffeu) { w = (uint32_t)plat_width(); h = (uint32_t)plat_height(); }
    else if (i < sizeof modes / sizeof modes[0]) { w = modes[i][0]; h = modes[i][1]; }
    else STDRET(3, 0);
    W32(dm + 40, 0x001c0000u);              // dmFields: BITSPERPEL | PELSWIDTH | PELSHEIGHT
    W32(dm + 104, 32); W32(dm + 108, w); W32(dm + 112, h); W32(dm + 120, 60);
    STDRET(3, 1);
}
U(ChangeDisplaySettingsA) { HOSTFN; rt_log("ChangeDisplaySettingsA(%08x)\n", ARG(1)); STDRET(2, 0); }   // DISP_CHANGE_SUCCESSFUL
U(GetWindowLongA) { HOSTFN; STDRET(2, (int32_t)ARG(2) == -4 ? g_wndproc : 0); }   // GWL_WNDPROC
U(SetWindowLongA) {
    HOSTFN;
    uint32_t prev = 0;
    if ((int32_t)ARG(2) == -4) { prev = g_wndproc; g_wndproc = ARG(3); }
    STDRET(3, prev);
}
U(CallWindowProcA) {   // (prev proc, hwnd, msg, wparam, lparam): forward to the guest procedure
    HOSTFN;
    uint32_t r = ARG(1) ? rt_guest_call(c, ARG(1), 4, ARG(2), ARG(3), ARG(4), ARG(5)) : 0;
    STDRET(5, r);
}
U(wsprintfA) {   // cdecl (buf, fmt, ...): the caller pops the arguments
    HOSTFN;
    char tmp[1024];
    int n = guest_vformat(mb, tmp, sizeof tmp, ARG(2), c->esp + 12);
    memcpy(mb + ARG(1), tmp, (size_t)n + 1);
    STDRET(0, n);
}
U(wvsprintfA) {
    HOSTFN;
    char tmp[1024];
    int n = guest_vformat(mb, tmp, sizeof tmp, ARG(2), ARG(3));
    memcpy(mb + ARG(1), tmp, (size_t)n + 1);
    STDRET(3, n);
}
U(GetWindowThreadProcessId) { HOSTFN; if (ARG(2)) W32(ARG(2), 0x10); STDRET(2, 0x100); }
U(GetKeyState) { STDRET(1, 0); }
U(FindWindowA) { STDRET(2, 0); }   // no other running instance
U(IsIconic) { STDRET(1, 0); }
U(SetFocus) { HOSTFN; STDRET(1, ARG(1)); }
U(SetForegroundWindow) { STDRET(1, 1); }
U(SetWindowPos) { STDRET(7, 1); }
U(AdjustWindowRectEx) { STDRET(4, 1); }
U(AttachThreadInput) { STDRET(3, 1); }
U(ClipCursor) { STDRET(1, 1); }
U(SetCursor) { STDRET(1, 0); }
U(ScreenToClient) { STDRET(2, 1); }
U(InvalidateRect) { STDRET(3, 1); }
U(RedrawWindow) { STDRET(4, 1); }
U(KillTimer) { STDRET(2, 1); }
U(GetClassLongA) { STDRET(2, 0); }
U(BeginPaint) { HOSTFN; memset(mb + ARG(2), 0, 64); W32(ARG(2), 0x0001d000u); STDRET(2, 0x0001d000u); }   // fake HDC
U(EndPaint) { STDRET(2, 1); }
U(RegisterDeviceNotificationA) { STDRET(3, 0x0001e000u); }
U(UnregisterDeviceNotification) { STDRET(1, 1); }
U(LoadStringA) {   // (hInstance, id, buffer, buffer size): from the image's string table
    HOSTFN;
    int w32_load_string(uint32_t id, char *out, size_t cap);
    char s[1024];
    int n = w32_load_string(ARG(2), s, sizeof s);
    rt_log("LoadStringA(%u) -> \"%s\"\n", ARG(2), s);
    if (!ARG(3) || !ARG(4)) STDRET(4, 0);
    if ((uint32_t)n >= ARG(4)) n = (int)ARG(4) - 1;
    memcpy(mb + ARG(3), s, (size_t)n);
    W8(ARG(3) + (uint32_t)n, 0);
    STDRET(4, (uint32_t)n);
}
U(DrawTextA) { ONCE("DrawTextA"); STDRET(5, 16); }
U(DrawTextW) { ONCE("DrawTextW"); STDRET(5, 16); }
