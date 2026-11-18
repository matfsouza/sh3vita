// Sampling profile of the main game thread with no instrumentation: the normal build measures
// itself while the user plays. The Vita cannot read another thread's program counter from user code, but the guest
// stack tells where the game is: every recompiled call writes its x86 return address at [esp] and stores esp in the
// CPU struct, so the first return address found from c->esp upward names the innermost guest function that is running
// (the target of that `call rel32`; for an indirect call or an import, the call site). While the main thread is inside
// a host function reached by an indirect call (Direct3D, DirectSound... through COM), rt_call records it and the sample
// goes to that host function instead.
//
// A thread samples every millisecond and logs the 40 busiest entries of each 10 s window:
//   [sprof] frames A-B: N samples
//   [sprof] 12.3% 1230 F 0048e140 guest function (entry address)
//   [sprof] 3.1% 310 S 005ceb55 call site whose target is not known (indirect call or import): return address
//   [sprof] 2.0% 200 H +0001a2c4 host function, offset from rt_call (resolve with nm on the build's ELF)
// Enabled with SH3_STATS (the test configuration) unless SH3_SPROF=0; SH3_SPROF=1 enables it alone.
#include "rt.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/cpu.h>
#endif

void *rt_main_host(void);   // x86rt.c: host function the main thread is in (NULL: guest code)

#define SP_N 8192           // hash slots (power of 2)
#define SP_WINDOW 10000     // samples per report
typedef struct { uint32_t key; uint32_t n; uint8_t kind; } Slot;   // kind: 0 empty, 'F', 'S', 'H'
static Slot s_tab[SP_N];
static unsigned s_frames;   // game frames (rt_sprof_frame), read by the sampler

static void add(uint8_t kind, uint32_t key) {
    for (uint32_t i = (key * 2654435761u) >> 19;; i = (i + 1) & (SP_N - 1)) {
        Slot *s = &s_tab[i & (SP_N - 1)];
        if (!s->kind) { s->kind = kind; s->key = key; s->n = 1; return; }
        if (s->kind == kind && s->key == key) { s->n++; return; }
    }
}
static int by_count(const void *a, const void *b) {
    const Slot *x = a, *y = b;
    return x->n < y->n ? 1 : x->n > y->n ? -1 : 0;
}
static void report(unsigned f0, unsigned f1, unsigned total) {
    static Slot top[SP_N];
    unsigned used = 0;
    for (unsigned i = 0; i < SP_N; i++) if (s_tab[i].kind) top[used++] = s_tab[i];
    qsort(top, used, sizeof *top, by_count);
    rt_log("[sprof] frames %u-%u: %u samples, %u entries\n", f0, f1, total, used);
    for (unsigned i = 0; i < used && i < 40; i++) {
        if (top[i].kind == 'H')
            rt_log("[sprof] %5.1f%% %6u H %+d\n", 100.0 * top[i].n / total, top[i].n, (int)(int32_t)top[i].key);
        else
            rt_log("[sprof] %5.1f%% %6u %c %08x\n", 100.0 * top[i].n / total, top[i].n, top[i].kind, top[i].key);
    }
    memset(s_tab, 0, sizeof s_tab);
}
static int sampler(void *arg) {
    (void)arg;
    rt_cpu_profile_thread("sprof");
#ifdef __vita__
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_2);   // never on the game's core
#endif
    unsigned total = 0, f0 = s_frames;
    for (;;) {
        SDL_Delay(1);
        CPU *c = g_main_cpu;
        uint8_t *mb = g_mem;
        if (!c || !mb) continue;
        void *host = rt_main_host();
        if (host) add('H', (uint32_t)((intptr_t)host - (intptr_t)(void *)rt_call));
        else {
            uint32_t esp = c->esp, hit = 0;
            uint8_t kind = 'S';
            for (int i = 0; i < 96 && !hit; i++) {
                uint32_t a = esp + 4u * (uint32_t)i;
                if (a < 0x10000 || a + 4 > g_mem_size) break;
                uint32_t v = R32(a);
                if (v < g_text_lo + 6 || v >= g_text_hi) continue;
                if (R8(v - 5) == 0xe8) {   // call rel32: the function that was called
                    uint32_t t = v + R32(v - 4);
                    if (t >= g_text_lo && t < g_text_hi) { hit = t; kind = 'F'; }
                } else if (R8(v - 2) == 0xff || R8(v - 3) == 0xff || R8(v - 6) == 0xff) hit = v;   // call r/m: the site
            }
            add(kind, hit);
        }
        if (++total >= SP_WINDOW) {
            unsigned f1 = s_frames;
            report(f0, f1, total);
            total = 0; f0 = f1;
        }
    }
    return 0;
}

// Called once per game frame by the main thread (frame limiter wrap); starts the sampler the first time.
void rt_sprof_frame(void) {
    static int state;   // 0: not decided, 1: running, -1: off
    s_frames++;
    if (state > 0 && !(s_frames % 300)) { void nat_prof_report(unsigned frames); nat_prof_report(300); }   // perf_natives.c
    if (state) return;
    const char *e = getenv("SH3_SPROF");
    state = (e ? atoi(e) != 0 : getenv("SH3_STATS") != NULL) ? 1 : -1;
    if (state < 0) return;
    rt_log("[sprof] sampling the main thread every ms (host offsets are relative to rt_call)\n");
    if (!SDL_CreateThread(sampler, "sh3sprof", NULL)) { rt_log("[sprof] thread: %s\n", SDL_GetError()); state = -1; }
}
