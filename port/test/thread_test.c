// Guest threads: CREATE_SUSPENDED, SuspendThread on itself, ResumeThread (BUG-010). Built and run by tests/test_thread.py.
// Guest code is simulated by the rt_guest_call stub: entry 0x1000 just runs, entry 0x2000 suspends itself in the middle.
#define SDL_MAIN_HANDLED
#include "win32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *g_mem;
uint32_t g_mem_size = 24u << 20;   // 64 KiB per fake thread context, ~260 threads in the tid-collision case
char g_game_dir[512] = ".";
CPU *g_main_cpu;
unsigned g_frame;
uint64_t g_t_wait;
void rt_log(const char *fmt, ...) { (void)fmt; }
_Noreturn void rt_fatal(const char *fmt, ...) { printf("FATAL %s\n", fmt); exit(2); }
int rt_cpu_profile_thread(const char *label) { (void)label; return 0; }

static void put32(uint32_t a, uint32_t v) { memcpy(g_mem + a, &v, 4); }
static SDL_atomic_t next_area, ran, stage1, stage2, go1, t1_after, release2, quick;
CPU *rt_new_cpu(uint32_t stack) {   // each context: a TIB and a small stack in guest memory
    (void)stack;
    uint32_t area = 0x10000u + 0x10000u * (uint32_t)SDL_AtomicAdd(&next_area, 1);
    CPU *c = calloc(1, sizeof *c);
    c->fs_base = area;
    c->esp = area + 0xf000;
    return c;
}
void rt_free_cpu(CPU *c) { free(c); }

void imp_KERNEL32_CreateThread(CPU *c);
void imp_KERNEL32_ResumeThread(CPU *c);
void imp_KERNEL32_SuspendThread(CPU *c);

static uint32_t call1(void (*fn)(CPU *), CPU *c, uint32_t arg) {   // stdcall with one argument
    c->esp -= 8; put32(c->esp + 4, arg);
    fn(c);
    return c->eax;
}
uint32_t rt_guest_call(CPU *c, uint32_t fn, int nargs, ...) {
    (void)nargs;
    if (fn == 0x1000) SDL_AtomicIncRef(&ran);
    if (fn == 0x1001) SDL_AtomicIncRef(&quick);
    if (fn == 0x4000) {   // waits for the test, then suspends itself
        while (!SDL_AtomicGet(&go1)) SDL_Delay(1);
        call1(imp_KERNEL32_SuspendThread, c, 0xfffffffeu);
        SDL_AtomicIncRef(&t1_after);
    }
    if (fn == 0x5000) while (!SDL_AtomicGet(&release2)) SDL_Delay(1);   // stays alive until released
    if (fn == 0x2000) {
        SDL_AtomicIncRef(&stage1);
        call1(imp_KERNEL32_SuspendThread, c, 0xfffffffeu);   // GetCurrentThread() pseudo handle
        SDL_AtomicIncRef(&stage2);
    }
    return 0;
}

static uint32_t create(CPU *c, uint32_t entry, uint32_t flags) {   // (attrs, stack, start, param, flags, tid *)
    c->esp = 0x8000;
    put32(c->esp + 4, 0); put32(c->esp + 8, 0); put32(c->esp + 12, entry); put32(c->esp + 16, 0);
    put32(c->esp + 20, flags); put32(c->esp + 24, 0);
    imp_KERNEL32_CreateThread(c);
    return c->eax;
}

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void wait_for(SDL_atomic_t *a, int value) { for (int i = 0; i < 1000 && SDL_AtomicGet(a) != value; i++) SDL_Delay(1); }

int main(void) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_TIMER)) return 2;
    g_mem = calloc(1, g_mem_size);
    CPU main_cpu = {0};
    main_cpu.fs_base = 0x1000;

    uint32_t h = create(&main_cpu, 0x1000, 4);   // CREATE_SUSPENDED
    CHECK(h != 0, "CreateThread failed");
    SDL_Delay(50);
    CHECK(SDL_AtomicGet(&ran) == 0, "CREATE_SUSPENDED thread ran before ResumeThread");
    main_cpu.esp = 0x8000;
    CHECK(call1(imp_KERNEL32_ResumeThread, &main_cpu, h) == 1, "ResumeThread must return the previous count 1");
    wait_for(&ran, 1);
    CHECK(SDL_AtomicGet(&ran) == 1, "thread did not run after ResumeThread");

    uint32_t h2 = create(&main_cpu, 0x2000, 0);   // runs, then suspends itself
    wait_for(&stage1, 1);
    SDL_Delay(50);
    CHECK(SDL_AtomicGet(&stage1) == 1 && SDL_AtomicGet(&stage2) == 0, "self-suspended thread kept running");
    main_cpu.esp = 0x8000;
    CHECK(call1(imp_KERNEL32_ResumeThread, &main_cpu, h2) == 1, "ResumeThread of a self-suspended thread must return 1");
    wait_for(&stage2, 1);
    CHECK(SDL_AtomicGet(&stage2) == 1, "self-suspended thread not resumed");
    main_cpu.esp = 0x8000;
    CHECK(call1(imp_KERNEL32_ResumeThread, &main_cpu, h2) == 0, "ResumeThread of a running thread must return 0");

    // Two live threads 256 tids apart (they shared a slot of the old tid % 256 table, final review / BUG-018):
    // the older one suspending itself must still block, and resuming it by handle must release it.
    uint32_t h1 = create(&main_cpu, 0x4000, 0);
    for (int k = 0; k < 255; k++) create(&main_cpu, 0x1001, 0);   // short-lived threads that only advance the tid
    wait_for(&quick, 255);
    create(&main_cpu, 0x5000, 0);                                  // tid = t1 + 256, alive
    SDL_Delay(20);
    SDL_AtomicSet(&go1, 1);
    SDL_Delay(80);
    CHECK(SDL_AtomicGet(&t1_after) == 0, "self-suspend resolved to another live thread (tid modulo collision)");
    main_cpu.esp = 0x8000;
    call1(imp_KERNEL32_ResumeThread, &main_cpu, h1);
    wait_for(&t1_after, 1);
    CHECK(SDL_AtomicGet(&t1_after) == 1, "ResumeThread did not release the self-suspended thread");
    SDL_AtomicSet(&release2, 1);

    printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
    return fails != 0;
}
