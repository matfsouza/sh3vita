// PC diagnosis (BUG-028): SH3_WATCH=<guest address in hex> logs every write to the 32 bytes at that address, with the
// host call stack (image offsets; symbols: arm-free `addr2line -f -e sh3.exe 0x140000000+offset`). The page is kept
// read-only; each write faults here, is let through with one single step, and the page is protected again.
#include "rt.h"
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>

static uint8_t *s_page;
static uintptr_t s_lo, s_hi;
static unsigned s_hits;

static LONG WINAPI watch_handler(EXCEPTION_POINTERS *e) {
    DWORD code = e->ExceptionRecord->ExceptionCode, old;
    if (code == EXCEPTION_ACCESS_VIOLATION && e->ExceptionRecord->ExceptionInformation[0] == 1) {
        uintptr_t a = e->ExceptionRecord->ExceptionInformation[1];
        if (a < (uintptr_t)s_page || a >= (uintptr_t)s_page + 4096) return EXCEPTION_CONTINUE_SEARCH;
        if (a >= s_lo && a < s_hi && s_hits < 12) {
            void *st[10];
            USHORT n = RtlCaptureStackBackTrace(0, 10, st, NULL);
            uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
            extern unsigned g_frame;
            s_hits++;
            rt_log("[watch] write to %08x frame %u thread %lu:", (unsigned)(a - (uintptr_t)g_mem), g_frame, GetCurrentThreadId());
            for (USHORT i = 0; i < n; i++) rt_log(" %llx", (unsigned long long)((uintptr_t)st[i] - base));
            rt_log("\n");
        }
        VirtualProtect(s_page, 4096, PAGE_READWRITE, &old);
        e->ContextRecord->EFlags |= 0x100;   // trap after this instruction, to protect the page again
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code == EXCEPTION_SINGLE_STEP && s_page) {
        VirtualProtect(s_page, 4096, PAGE_READONLY, &old);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void rt_watch_start(void) {
    const char *e = getenv("SH3_WATCH");
    if (!e || s_page) return;
    uint32_t addr = (uint32_t)strtoul(e, NULL, 16);
    DWORD old;
    s_lo = (uintptr_t)g_mem + addr;
    s_hi = s_lo + 32;
    s_page = (uint8_t *)(s_lo & ~(uintptr_t)4095);
    AddVectoredExceptionHandler(1, watch_handler);
    VirtualProtect(s_page, 4096, PAGE_READONLY, &old);
    rt_log("[watch] guest %08x\n", addr);
}
#else
void rt_watch_start(void) {}
#endif
