// WINMM timeSetEvent/timeKillEvent with real SDL timers. Built and run by tests/test_mm_timer.py.
// The runtime pieces the timer code needs (CPU contexts, guest calls) are counted by stubs.
#define SDL_MAIN_HANDLED
#include "win32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

uint8_t *g_mem;
uint32_t g_mem_size = 1u << 20;
char g_game_dir[512] = ".";
CPU *g_main_cpu;
uint64_t g_t_wait;
unsigned g_frame;
void rt_log(const char *fmt, ...) { (void)fmt; }
_Noreturn void rt_fatal(const char *fmt, ...) { printf("FATAL %s\n", fmt); exit(2); }

static SDL_atomic_t cpus_alive, cpus_made, calls, chain;
CPU *rt_new_cpu(uint32_t stack) {
    (void)stack; SDL_AtomicIncRef(&cpus_alive); SDL_AtomicIncRef(&cpus_made);
    CPU *c = calloc(1, sizeof(CPU));
    c->esp = 0xa000;   // the timer thread's own stack area
    return c;
}
void rt_free_cpu(CPU *c) { SDL_AtomicDecRef(&cpus_alive); free(c); }

static void put32(uint32_t a, uint32_t v) { memcpy(g_mem + a, &v, 4); }
void imp_WINMM_timeSetEvent(CPU *c);
void imp_WINMM_timeKillEvent(CPU *c);
static uint32_t set_event_cb(CPU *c, uint32_t sp, uint32_t ms, uint32_t cb, uint32_t flags) {   // (delay, resolution, cb, user, flags)
    put32(sp + 4, ms); put32(sp + 8, 1); put32(sp + 12, cb); put32(sp + 16, 0); put32(sp + 20, flags);
    c->esp = sp;
    imp_WINMM_timeSetEvent(c);
    return c->eax;
}
static uint32_t set_event(CPU *c, uint32_t ms, uint32_t flags) { return set_event_cb(c, 0x8000, ms, 0x612bf0, flags); }
static void kill_event(CPU *c, uint32_t sp, uint32_t h) { put32(sp + 4, h); c->esp = sp; imp_WINMM_timeKillEvent(c); }

// Guest callbacks: 0x612bf0 only counts; 0x3000 is SH3's pattern (0x612bf0 in the game): kill its own one-shot timer,
// then arm the next one, which may reuse both the handle number and the freed memory of the old timer.
uint32_t rt_guest_call(CPU *c, uint32_t fn, int nargs, ...) {
    va_list ap;
    va_start(ap, nargs);
    uint32_t handle = va_arg(ap, uint32_t);
    va_end(ap);
    SDL_AtomicIncRef(&calls);
    if (fn == 0x3000 && SDL_AtomicIncRef(&chain) < 199) {
        uint32_t sp = c->esp - 0x40;
        kill_event(c, sp, handle);
        set_event_cb(c, sp, 1, 0x3000, 0);
        c->esp = sp + 0x40;
    }
    return 0;
}

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_TIMER)) { printf("SDL_Init failed\n"); return 2; }
    g_mem = calloc(1, g_mem_size);
    CPU c = {0};
    // 300 one-shot timers re-armed one after the other, as SH3 does every frame.
    for (int i = 0; i < 300; i++) {
        int before = SDL_AtomicGet(&calls);
        uint32_t h = set_event(&c, 1, 0);   // TIME_ONESHOT
        CHECK(h != 0, "timeSetEvent returned 0");
        for (int w = 0; w < 500 && SDL_AtomicGet(&calls) == before; w++) SDL_Delay(1);
    }
    SDL_Delay(20);
    CHECK(SDL_AtomicGet(&calls) == 300, "callbacks fired: %d", SDL_AtomicGet(&calls));
    CHECK(SDL_AtomicGet(&cpus_made) <= 1, "CPU contexts created: %d (one shared context expected)", SDL_AtomicGet(&cpus_made));

    // SH3's self-killing, self-re-arming one-shot chain must never break (it stopped at random before the fix).
    set_event_cb(&c, 0x8000, 1, 0x3000, 0);
    for (int w = 0; w < 3000 && SDL_AtomicGet(&chain) < 200; w++) SDL_Delay(1);
    CHECK(SDL_AtomicGet(&chain) == 200, "self re-arming chain stopped after %d timers", SDL_AtomicGet(&chain));

    // A stale tick of a killed timer (SDL already past its cancel check) must not fire the new timer that reused
    // the same handle value.
    {
        extern void *w32_mm_param(uint32_t handle);
        extern Uint32 w32_mm_tick(Uint32 interval, void *param);
        uint32_t a = set_event(&c, 100000, 0);
        void *stale = w32_mm_param(a);
        put32(0x8000 + 4, a); c.esp = 0x8000;
        imp_WINMM_timeKillEvent(&c);
        uint32_t b = set_event(&c, 100000, 0);
        CHECK(b == a, "test setup: the new timer should reuse the handle (%x vs %x)", b, a);
        int before_stale = SDL_AtomicGet(&calls);
        Uint32 next = w32_mm_tick(10, stale);
        CHECK(SDL_AtomicGet(&calls) == before_stale && next == 0, "stale tick fired the timer that reused the handle");
        CHECK(h_type(b) == H_TIMER, "stale tick released the new timer");
        put32(0x8000 + 4, b); c.esp = 0x8000;
        imp_WINMM_timeKillEvent(&c);
    }

    // A periodic timer keeps firing until timeKillEvent, which may also be called on an already-fired one-shot.
    int before = SDL_AtomicGet(&calls);
    uint32_t p = set_event(&c, 2, 1);   // TIME_PERIODIC
    SDL_Delay(60);
    put32(0x8000 + 4, p); c.esp = 0x8000;
    imp_WINMM_timeKillEvent(&c);
    int after_kill = SDL_AtomicGet(&calls);
    CHECK(after_kill - before >= 5, "periodic fired %d times", after_kill - before);
    SDL_Delay(30);
    CHECK(SDL_AtomicGet(&calls) == after_kill, "periodic fired after timeKillEvent");
    CHECK(h_type(p) == H_FREE, "periodic handle not released");

    printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
    return fails != 0;
}
