// Guest threads (KERNEL32 CreateThread, ExitThread, SuspendThread, ResumeThread) on SDL threads.
// Suspension is cooperative but exact for the cases SH3 uses (docs/BUGS.md BUG-010): a thread created with
// CREATE_SUSPENDED does not start before ResumeThread, and a thread that suspends itself blocks until resumed.
// Suspending another running thread only raises its count (it cannot be stopped mid-instruction here).
#include "win32.h"
#include <stdlib.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/cpu.h>
#endif

#define K(name) void imp_KERNEL32_##name(CPU *c)
#define CREATE_SUSPENDED 4u
#define PSEUDO_CURRENT_THREAD 0xfffffffeu

static uint32_t next_tid = 0x100;
// The guest Thread run by the calling host thread, in SDL thread-local storage (NULL on the main thread).
// A table indexed by tid % 256 let two live threads 256 tids apart share an entry (BUG-018).
static SDL_TLSID s_self;

static Thread *current_thread(void) { return s_self ? SDL_TLSGet(s_self) : NULL; }
static Thread *thread_of(uint32_t h) { return h == PSEUDO_CURRENT_THREAD ? current_thread() : h_get(h, H_THREAD); }

// Blocks the calling host thread while t is suspended. Caller holds g_sync.
static void wait_resumed(Thread *t) { while (t->suspend > 0) SDL_CondWait(g_sync_cv, g_sync); }

K(ExitThread) {
    HOSTFN;
    Thread *t = current_thread();
    if (!t || !t->exit_jb) { rt_log("ExitThread(%u) on the main thread -> exit\n", ARG(1)); exit((int)ARG(1)); }
    longjmp(*(jmp_buf *)t->exit_jb, 1);
}

static int thread_main(void *p) {
    Thread *t = p;
    { char name[24]; snprintf(name, sizeof name, "guest-%06x", (unsigned)t->start); rt_cpu_profile_thread(name); }
#ifdef __vita__
    // The game's helper threads (sound streaming, loader: ~10 ms of CPU per frame together, run15) stay off core 0,
    // which the main game thread needs whole (39 ms of CPU per frame in the park). SH3_GUEST_ANYCORE=1: any core.
    if (!getenv("SH3_GUEST_ANYCORE"))
        sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2);
#endif
    HOSTFN;
    W32(t->c->fs_base + 0x24, t->tid);
    SDL_TLSSet(s_self, t, NULL);
    SDL_LockMutex(g_sync);
    wait_resumed(t);   // CREATE_SUSPENDED
    SDL_UnlockMutex(g_sync);
    jmp_buf exit_jb;   // ExitThread unwinds the guest call back to here
    t->exit_jb = &exit_jb;
    if (!setjmp(exit_jb)) rt_guest_call(t->c, t->start, 1, t->param);
    else rt_log("ExitThread tid=%u\n", t->tid);
    t->exit_jb = NULL;
    SDL_TLSSet(s_self, NULL, NULL);
    SDL_LockMutex(g_sync);
    t->done = 1;
    SDL_CondBroadcast(g_sync_cv);
    SDL_UnlockMutex(g_sync);
    return 0;
}

K(CreateThread) {   // (attrs, stack size, start, param, flags, tid *)
    HOSTFN;
    static SDL_SpinLock once;
    SDL_AtomicLock(&once);
    if (!s_self) s_self = SDL_TLSCreate();
    SDL_AtomicUnlock(&once);
    Thread *t = calloc(1, sizeof *t);
    if (!t) { w32_set_last_error(c, 8); STDRET(6, 0); }
    uint32_t stack = ARG(2) ? ARG(2) : (1u << 20);
    t->c = rt_new_cpu(stack < 0x40000 ? 0x40000 : stack);
    t->start = ARG(3);
    t->param = ARG(4);
    t->tid = next_tid++;
    t->suspend = (ARG(5) & CREATE_SUSPENDED) ? 1 : 0;
    if (ARG(6)) W32(ARG(6), t->tid);
    uint32_t h = h_new(H_THREAD, t);
    rt_log("CreateThread start=%08x param=%08x tid=%u%s\n", t->start, t->param, t->tid, t->suspend ? " (suspended)" : "");
    t->t = SDL_CreateThread(thread_main, "guest", t);
    STDRET(6, h);
}

K(ResumeThread) {   // -> previous suspend count, or -1
    HOSTFN;
    w32_trace_sync("resume", ARG(1), R32(c->esp));
    SDL_LockMutex(g_sync);
    Thread *t = thread_of(ARG(1));
    uint32_t prev = 0xffffffffu;
    if (t) {
        prev = (uint32_t)t->suspend;
        if (t->suspend > 0) t->suspend--;
        SDL_CondBroadcast(g_sync_cv);
    }
    SDL_UnlockMutex(g_sync);
    if (!t) w32_set_last_error(c, 6);
    STDRET(1, prev);
}

K(SuspendThread) {   // -> previous suspend count, or -1
    HOSTFN;
    w32_trace_sync("suspend", ARG(1), R32(c->esp));
    SDL_LockMutex(g_sync);
    Thread *t = thread_of(ARG(1));
    uint32_t prev = 0xffffffffu;
    if (t) {
        prev = (uint32_t)t->suspend++;
        if (t == current_thread()) wait_resumed(t);   // self-suspension: block until ResumeThread
        else {
            static int said;
            if (!said++) rt_log("[thread] SuspendThread of another running thread tid=%u: count raised only\n", t->tid);
        }
    }
    SDL_UnlockMutex(g_sync);
    if (!t) w32_set_last_error(c, 6);
    STDRET(1, prev);
}
