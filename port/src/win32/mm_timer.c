// WINMM multimedia timers (timeSetEvent / timeKillEvent) on SDL timers.
// SDL runs every timer callback on its single timer thread, so all guest callbacks share one CPU context.
// A one-shot timer frees itself after firing, as on Windows: SH3 re-arms a one-shot timer every frame, and a
// context per timer exhausted the guest heap after ~1500 frames (docs/BUGS.md BUG-009).
// A callback that kills its own timer and arms a new one gets the old memory back from malloc: the timer being
// fired is therefore tracked by its `firing`/`killed` flags, never by comparing pointers (BUG-011).
// Handle values are reused too: the SDL parameter is a ticket (handle slot + timer serial), so a late tick of a
// killed timer never fires the new timer that got the same handle (BUG-017).
#include "win32.h"
#include <stdlib.h>

#define W(name) void imp_WINMM_##name(CPU *c)
#define TIME_PERIODIC 1u

typedef struct { SDL_TimerID id; uint32_t cb, user, periodic, handle, serial; int firing, killed; } MMTimer;
#define SERIAL_BITS 22u   // handles are 0x1000 + 4 * slot with slot < 1024 (handles.c): 10 bits of slot, 22 of serial
#define SERIAL_MASK ((1u << SERIAL_BITS) - 1u)
static uint32_t s_serial;
static uint32_t ticket_of(const MMTimer *t) { return ((t->handle - 0x1000u) / 4u) << SERIAL_BITS | (t->serial & SERIAL_MASK); }
static uint32_t ticket_handle(uint32_t ticket) { return 0x1000u + 4u * (ticket >> SERIAL_BITS); }
static SDL_mutex *s_lock;   // recursive: a callback may call timeSetEvent/timeKillEvent
static CPU *s_cpu;          // shared guest context of the SDL timer thread

static void mm_init(void) {
    static SDL_SpinLock once;
    SDL_AtomicLock(&once);
    if (!s_lock) s_lock = SDL_CreateMutex();
    SDL_AtomicUnlock(&once);
}

Uint32 w32_mm_tick(Uint32 interval, void *param) {
    uint32_t ticket = (uint32_t)(uintptr_t)param, handle = ticket_handle(ticket);
    SDL_LockMutex(s_lock);
    MMTimer *t = h_get(handle, H_TIMER);
    if (!t || (t->serial & SERIAL_MASK) != (ticket & SERIAL_MASK)) { SDL_UnlockMutex(s_lock); return 0; }   // killed meanwhile
    if (!s_cpu) s_cpu = rt_new_cpu(0x40000);
    uint32_t periodic = t->periodic;
    w32_trace_sync("timer fire", handle, t->cb);
    t->firing = 1;
    rt_guest_call(s_cpu, t->cb, 5, handle, 0u, t->user, 0u, 0u);
    t->firing = 0;
    if (t->killed) { free(t); periodic = 0; }          // killed by its own callback: handle already closed
    else if (!periodic) { h_close(handle); free(t); }  // one-shot: frees itself after firing
    SDL_UnlockMutex(s_lock);
    return periodic ? interval : 0;
}

void *w32_mm_param(uint32_t handle) {   // SDL parameter of a live timer (tests)
    MMTimer *t = h_get(handle, H_TIMER);
    return t ? (void *)(uintptr_t)ticket_of(t) : NULL;
}

W(timeSetEvent) {   // (delay ms, resolution, callback, user, flags)
    HOSTFN;
    mm_init();
    MMTimer *t = calloc(1, sizeof *t);
    if (!t) STDRET(5, 0);
    t->cb = ARG(3);
    t->user = ARG(4);
    t->periodic = ARG(5) & TIME_PERIODIC;
    SDL_LockMutex(s_lock);
    t->handle = h_new(H_TIMER, t);
    t->serial = ++s_serial;
    t->id = SDL_AddTimer(ARG(1) ? ARG(1) : 1, w32_mm_tick, (void *)(uintptr_t)ticket_of(t));
    uint32_t h = t->handle;
    SDL_UnlockMutex(s_lock);
    w32_trace_sync("timer set", h, ARG(1));
    static unsigned logged;
    if (logged++ < 4) rt_log("timeSetEvent(%u ms, cb=%08x, periodic=%u) -> %x\n", ARG(1), ARG(3), ARG(5) & TIME_PERIODIC, h);
    STDRET(5, h);
}

W(timeKillEvent) {
    HOSTFN;
    mm_init();
    SDL_LockMutex(s_lock);   // waits for a running callback: none fires after timeKillEvent returns
    MMTimer *t = h_get(ARG(1), H_TIMER);
    w32_trace_sync("timer kill", ARG(1), t != NULL);
    if (t) {
        SDL_RemoveTimer(t->id);
        h_close(ARG(1));
        if (t->firing) t->killed = 1;   // freed by mm_tick when the callback returns
        else free(t);
    }
    SDL_UnlockMutex(s_lock);
    STDRET(1, t ? 0 : 97);   // TIMERR_NOERROR / MMSYSERR_INVALPARAM
}
