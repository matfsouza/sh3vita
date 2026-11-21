// One lock around every call that takes a FILE from newlib's pool or gives it back (tests/test_stdio_lock.py).
// The Vita's newlib is built without stdio locking (_RETARGETABLE_LOCKING off, __sfp_lock_acquire is empty): two
// threads in fopen / fclose at once could be handed the same FILE, and when one closed it the other's FILE had
// fileno -1, so its next fseek crashed in __vita_fd_grab (the game's loader thread in ReadFile, while another thread
// opened or closed a file: opening Options from the inventory, the subway gate). The linker sends every call to
// these functions here (port/CMakeLists.txt, -Wl,--wrap), the static libraries' calls included. Reads and seeks on
// different FILEs need no lock; calls on one FILE are serialized by its owner (kernel32.c File.mu).
#include <stdio.h>
#include <psp2/kernel/threadmgr.h>

FILE *__real_fopen(const char *path, const char *mode);
int __real_fclose(FILE *f);
FILE *__real_freopen(const char *path, const char *mode, FILE *f);
FILE *__real_fdopen(int fd, const char *mode);

static SceKernelLwMutexWork s_work __attribute__((aligned(8)));
static volatile int s_state;   // 0 not created, 1 being created, 2 ready

// Recursive (a wrapped call may reach another one), created on first use: a file can be opened before main.
static void stdio_lock(void) {
    if (__atomic_load_n(&s_state, __ATOMIC_ACQUIRE) != 2) {
        int expected = 0;
        if (__atomic_compare_exchange_n(&s_state, &expected, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            sceKernelCreateLwMutex(&s_work, "sh3stdio", SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 0, NULL);
            __atomic_store_n(&s_state, 2, __ATOMIC_RELEASE);
        } else {
            while (__atomic_load_n(&s_state, __ATOMIC_ACQUIRE) != 2) sceKernelDelayThread(100);
        }
    }
    sceKernelLockLwMutex(&s_work, 1, NULL);
}
static void stdio_unlock(void) { sceKernelUnlockLwMutex(&s_work, 1); }

FILE *__wrap_fopen(const char *path, const char *mode) {
    stdio_lock();
    FILE *f = __real_fopen(path, mode);
    stdio_unlock();
    return f;
}
int __wrap_fclose(FILE *f) {
    stdio_lock();
    int r = __real_fclose(f);
    stdio_unlock();
    return r;
}
FILE *__wrap_freopen(const char *path, const char *mode, FILE *f) {
    stdio_lock();
    FILE *r = __real_freopen(path, mode, f);
    stdio_unlock();
    return r;
}
FILE *__wrap_fdopen(int fd, const char *mode) {
    stdio_lock();
    FILE *f = __real_fdopen(fd, mode);
    stdio_unlock();
    return f;
}
