// Handles Win32, attentes et chemins.
#include "win32.h"
#include <ctype.h>

#define MAX_H 1024
static struct { int type; void *obj; } htab[MAX_H];
static SDL_mutex *hlock;
SDL_mutex *g_sync;
SDL_cond *g_sync_cv;

static void lazy_init(void) {
    if (!hlock) {
        hlock = SDL_CreateMutex();
        g_sync = SDL_CreateMutex();
        g_sync_cv = SDL_CreateCond();
    }
}

uint32_t h_new(int type, void *obj) {
    lazy_init();
    SDL_LockMutex(hlock);
    for (int i = 1; i < MAX_H; i++)
        if (htab[i].type == H_FREE) {
            htab[i].type = type;
            htab[i].obj = obj;
            SDL_UnlockMutex(hlock);
            return 0x1000u + 4u * i;
        }
    SDL_UnlockMutex(hlock);
    rt_fatal("handle table full");
}

static int idx(uint32_t h) {
    int i = (int)((h - 0x1000u) / 4u);
    return h >= 0x1000u && (h & 3) == 0 && i < MAX_H ? i : 0;
}

int h_type(uint32_t h) { return htab[idx(h)].type; }

void *h_get(uint32_t h, int type) {
    int i = idx(h);
    return i && htab[i].type == type ? htab[i].obj : NULL;
}

void h_close(uint32_t h) {
    int i = idx(h);
    if (!i) return;
    SDL_LockMutex(hlock);
    htab[i].type = H_FREE;
    htab[i].obj = NULL;
    SDL_UnlockMutex(hlock);
}

int w32_signaled(uint32_t h) {
    switch (h_type(h)) {
    case H_EVENT: return ((Event *)h_get(h, H_EVENT))->state;
    case H_THREAD: return ((Thread *)h_get(h, H_THREAD))->done;
    case H_MUTEX: return 1;
    case H_SEM: return __atomic_load_n(&((Semaphore *)h_get(h, H_SEM))->count, __ATOMIC_SEQ_CST) > 0;
    default: return 1;
    }
}

// Semaphore counts are atomic (BUG-029, run20): the game's try-lock 0x6099c0 (WaitForSingleObject with timeout 0) and
// its unlock 0x609a60 (ReleaseSemaphore) run many times per frame on the main thread; through the global g_sync mutex
// they waited on the guest threads that hold it (3.1% + 3.7% of the main thread in the park). Taking or adding a count
// is a compare-and-swap; g_sync is only needed to wake a thread that waits.
int w32_sem_try(uint32_t h) {
    Semaphore *s = h_get(h, H_SEM);
    if (!s) return 0;
    int32_t v = __atomic_load_n(&s->count, __ATOMIC_SEQ_CST);
    while (v > 0)
        if (__atomic_compare_exchange_n(&s->count, &v, v - 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return 1;
    return 0;
}
int w32_consume(uint32_t h) {
    Event *e = h_get(h, H_EVENT);
    if (e && !e->manual) { if (e->tokens) e->tokens--; else e->state = 0; }
    if (h_type(h) == H_SEM) return w32_sem_try(h);
    return 1;
}

// PulseEvent: releases the threads waiting right now (all of them for a manual-reset event, one for an
// auto-reset event) and leaves the event unsignalled. Setting then resetting the state lost the wake-up whenever a
// waiter re-checked after the reset (docs/BUGS.md BUG-010). Waiters register in w32_wait; a manual-reset pulse
// bumps the epoch they recorded, an auto-reset pulse leaves one token for the first waiter to consume.
void w32_event_pulse(uint32_t h) {
    lazy_init();
    w32_trace_sync("event pulse", h, 0);
    SDL_LockMutex(g_sync);
    Event *e = h_get(h, H_EVENT);
    if (e && e->waiters) {
        if (e->manual) e->epoch++;
        else if (!e->state) e->tokens = 1;
        SDL_CondBroadcast(g_sync_cv);
    }
    SDL_UnlockMutex(g_sync);
}

// Signalled for this waiter: as w32_signaled, plus a pulse it was registered for (epoch0: its epoch at wait start).
static int signaled_for(uint32_t h, uint32_t epoch0) {
    Event *e = h_get(h, H_EVENT);
    if (e) return e->state || e->tokens || (e->manual && e->epoch != epoch0);
    return w32_signaled(h);
}
// Threads in w32_wait right now, per handle (under g_sync). A semaphore release or event set with nobody waiting on
// that handle does not wake every waiting thread (BUG-029, run20: ReleaseSemaphore called by the game's sound code
// took 3.7% of the main thread in the park, each call waking all the guest threads that share core 1 with rendering).
static uint16_t s_hwait[MAX_H];
int w32_has_waiters(uint32_t h) { return __atomic_load_n(&s_hwait[idx(h)], __ATOMIC_SEQ_CST) != 0; }
static void register_waiter(uint32_t n, const uint32_t *hs, uint32_t *epoch0, int delta) {   // under g_sync
    for (uint32_t i = 0; i < n; i++) {
        __atomic_add_fetch(&s_hwait[idx(hs[i])], (uint16_t)delta, __ATOMIC_SEQ_CST);
        Event *e = h_get(hs[i], H_EVENT);
        if (!e) continue;
        e->waiters += delta;
        if (delta > 0) epoch0[i] = e->epoch;
    }
}

uint32_t w32_wait(uint32_t n, const uint32_t *hs, int all, uint32_t timeout_ms) {
    lazy_init();
    if (n == 1 && h_type(hs[0]) == H_SEM) {   // semaphore: lock-free when a count is there (or for a try with no wait)
        if (w32_sem_try(hs[0])) return 0;
        if (timeout_ms == 0) return WAIT_TIMEOUT_;
    }
    static unsigned s_wait_n;
    unsigned wait_id = ++s_wait_n;
    if (wait_id <= 5)   // first ones only: one line every 200 waits filled the log over hours of play
        rt_log("w32_wait #%u n=%u all=%d to=%u h0=%x\n", wait_id, n, all, timeout_ms, n > 0 ? hs[0] : 0);
    for (uint32_t i = 0; i < n && i < 4; i++) w32_trace_sync("wait", hs[i], timeout_ms);
    if (n > 64) n = 64;
    uint32_t epoch0[64] = {0};
    uint32_t start = SDL_GetTicks();
    SDL_LockMutex(g_sync);
    register_waiter(n, hs, epoch0, 1);
    for (;;) {
        uint32_t ok = 0, first = 0xffffffffu;
        for (uint32_t i = 0; i < n; i++)
            if (signaled_for(hs[i], epoch0[i])) { ok++; if (first == 0xffffffffu) first = i; }
        // a semaphore count can be taken by a lock-free try between the check and here: then wait again
        if (all ? ok == n : ok > 0 && w32_consume(hs[first])) {
            if (all) for (uint32_t i = 0; i < n; i++) w32_consume(hs[i]);
            register_waiter(n, hs, epoch0, -1);
            SDL_UnlockMutex(g_sync);
            if (wait_id <= 5) rt_log("w32_wait #%u -> %u\n", wait_id, all ? 0 : first);
            return all ? 0 : first;
        }
        uint32_t el = SDL_GetTicks() - start;
        if (timeout_ms != 0xffffffffu && el >= timeout_ms) break;
        SDL_CondWaitTimeout(g_sync_cv, g_sync, timeout_ms == 0xffffffffu ? 50 : (timeout_ms - el < 50 ? timeout_ms - el : 50));
    }
    register_waiter(n, hs, epoch0, -1);
    SDL_UnlockMutex(g_sync);
    if (wait_id <= 5) rt_log("w32_wait #%u -> TIMEOUT\n", wait_id);
    return WAIT_TIMEOUT_;
}

void w32_trace_sync(const char *what, uint32_t h, uint32_t extra) {
    static int on = -1;
    static SDL_atomic_t n;
    if (on < 0) on = getenv("SH3_TRACE_SYNC") != NULL;
    if (!on || SDL_AtomicAdd(&n, 1) >= 600) return;
    static const char *names[] = {"free", "file", "event", "mutex", "thread", "regkey", "timer", "sem", "mmio", "find", "mapping"};
    int ty = h_type(h);
    rt_log("[sync] %-14s h=%x (%s) %x host=%lu\n", what, h, ty >= 0 && ty < 11 ? names[ty] : "?", extra, SDL_ThreadID());
}

void w32_set_last_error(CPU *c, uint32_t e) {
    HOSTFN;
    W32(c->fs_base + 0x34, e);
}

// SH3_VTIME=1 (PC profiling): virtual 30 Hz clock + 10 us per read, so the game never sleeps.
extern unsigned g_frame;
static int vtime(void) { static int v = -1; if (v < 0) v = getenv("SH3_VTIME") != NULL; return v; }
unsigned w32_hz(void) {   // no game logic (30 originally); SH3_HZ=60 to test a 60 Hz loop
    static unsigned h;
    if (!h) { const char *e = getenv("SH3_HZ"); h = e && atoi(e) >= 15 && atoi(e) <= 120 ? (unsigned)atoi(e) : 30; }
    return h;
}
uint64_t w32_qpc(void) {
    static uint64_t calls;
    if (!vtime()) return SDL_GetPerformanceCounter();
    return ((uint64_t)g_frame * 1000000 / w32_hz() + ++calls * 10) * SDL_GetPerformanceFrequency() / 1000000;
}
// Sleeps until QPC instant t to within ~1 ms (the caller finishes with a busy wait); nothing with the virtual clock.
void w32_sleep_until(uint64_t t) {
    if (vtime()) return;
    uint64_t now = SDL_GetPerformanceCounter(), f = SDL_GetPerformanceFrequency();
    if (t <= now || t - now >= f) return;
    uint32_t ms = (uint32_t)((t - now) * 1000 / f);
    if (ms >= 2) SDL_Delay(ms - 1);
}
uint32_t w32_tick_ms(void) { return vtime() ? (uint32_t)(w32_qpc() * 1000 / SDL_GetPerformanceFrequency()) : SDL_GetTicks(); }

void w32_host_path(const char *g, char *out, size_t n) {
    // NULL guest path (BUG-005): an empty host path, so the file call fails as on Windows instead of crashing.
    if (!g) { if (n) out[0] = 0; return; }
    char tmp[512];
    size_t k = 0;
    for (; *g && k < sizeof tmp - 1; g++) tmp[k++] = *g == '\\' ? '/' : *g;
    tmp[k] = 0;
    const char *p = tmp;
    if (!SDL_strncasecmp(p, "C:/SH3/", 7)) p += 7;
    while (p[0] == '.' && p[1] == '/') p += 2;
    if (p[0] && p[1] == ':') { SDL_snprintf(out, n, "%s", p); return; }
    SDL_snprintf(out, n, "%s/%s", g_game_dir, p);
}
