// PulseEvent semantics with real SDL threads (BUG-010). Built and run by tests/test_sync.py.
// A pulse releases the threads already waiting (all of them for a manual-reset event, one for an auto-reset event)
// and never leaves the event signalled.
#define SDL_MAIN_HANDLED
#include "win32.h"
#include <stdio.h>
#include <stdlib.h>

uint8_t *g_mem;
uint32_t g_mem_size = 1u << 16;
char g_game_dir[512] = ".";
CPU *g_main_cpu;
unsigned g_frame;
void rt_log(const char *fmt, ...) { (void)fmt; }
_Noreturn void rt_fatal(const char *fmt, ...) { printf("FATAL %s\n", fmt); exit(2); }

static SDL_atomic_t woke, started;
typedef struct { uint32_t h, timeout; } WaitArg;
static int waiter(void *p) {
    WaitArg *a = p;
    SDL_AtomicIncRef(&started);
    if (w32_wait(1, &a->h, 0, a->timeout) == 0) SDL_AtomicIncRef(&woke);
    return 0;
}
static uint32_t new_event(int manual) {
    Event *e = calloc(1, sizeof *e);
    e->manual = manual;
    return h_new(H_EVENT, e);
}
static void start_waiters(WaitArg *a, int n, SDL_Thread **th) {
    SDL_AtomicSet(&woke, 0);
    SDL_AtomicSet(&started, 0);
    for (int i = 0; i < n; i++) th[i] = SDL_CreateThread(waiter, "waiter", &a[i]);
    while (SDL_AtomicGet(&started) < n) SDL_Delay(1);
    SDL_Delay(30);   // let them block inside w32_wait
}

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_TIMER)) return 2;
    g_mem = calloc(1, g_mem_size);
    SDL_Thread *th[2];

    for (int round = 0; round < 20; round++) {   // repeated: the old set+reset pulse lost wake-ups only sometimes
        uint32_t m = new_event(1);
        WaitArg a[2] = {{m, 400}, {m, 400}};
        start_waiters(a, 2, th);
        w32_event_pulse(m);
        for (int i = 0; i < 2; i++) SDL_WaitThread(th[i], NULL);
        CHECK(SDL_AtomicGet(&woke) == 2, "round %d: manual-reset pulse woke %d of 2 waiters", round, SDL_AtomicGet(&woke));

        uint32_t au = new_event(0);
        WaitArg b[2] = {{au, 200}, {au, 200}};
        start_waiters(b, 2, th);
        w32_event_pulse(au);
        for (int i = 0; i < 2; i++) SDL_WaitThread(th[i], NULL);
        CHECK(SDL_AtomicGet(&woke) == 1, "round %d: auto-reset pulse woke %d waiters (1 expected)", round, SDL_AtomicGet(&woke));
    }

    uint32_t idle = new_event(1);
    w32_event_pulse(idle);   // nobody waiting: the pulse is lost and the event stays unsignalled
    CHECK(w32_wait(1, &idle, 0, 20) == WAIT_TIMEOUT_, "pulse without waiters left the event signalled");

    printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
    return fails != 0;
}
