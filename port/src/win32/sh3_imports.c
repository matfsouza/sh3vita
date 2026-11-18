// Win32 imports that SH3 uses and SH4 did not: KERNEL32 (sync, files, locale, time), GDI32, IMM32, OLEAUT32.
// Functions marked "(stub)" log their first call: if one shows up in a log, it is a lead.
#include "win32.h"
#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include "w32_time.h"

#define K(name) void imp_KERNEL32_##name(CPU *c)
#define G(name) void imp_GDI32_##name(CPU *c)
#define ONCE(name) do { static int said; if (!said++) rt_log("[win32] %s (stub)\n", name); } while (0)

// ---------- synchronisation ----------
K(WaitForSingleObject) {
    HOSTFN;
    uint32_t h = ARG(1);
    uint64_t t0 = SDL_GetPerformanceCounter();
    uint32_t r = w32_wait(1, &h, 0, ARG(2));
    if (c == g_main_cpu) { extern uint64_t g_t_wait; g_t_wait += SDL_GetPerformanceCounter() - t0; }
    STDRET(2, r);
}
K(CreateSemaphoreA) {   // (attrs, initial, max, name)
    HOSTFN;
    Semaphore *s = calloc(1, sizeof *s);
    if (!s) { w32_set_last_error(c, 8); STDRET(4, 0); }
    s->count = (int32_t)ARG(2);
    s->max = (int32_t)ARG(3);
    w32_set_last_error(c, 0);
    STDRET(4, h_new(H_SEM, s));
}
K(ReleaseSemaphore) {   // (h, release count, previous count *)
    HOSTFN;
    Semaphore *s = h_get(ARG(1), H_SEM);
    w32_trace_sync("sem release", ARG(1), ARG(2));
    if (!s) { w32_set_last_error(c, 6); STDRET(3, 0); }
    // lock-free add (handles.c w32_sem_try); g_sync only to wake a waiting thread, under the lock so that a waiter
    // that registered and found no count is already in its condition wait (no lost wake-up)
    int32_t add = (int32_t)ARG(2), prev = __atomic_load_n(&s->count, __ATOMIC_SEQ_CST);
    int ok = 0;
    while (add > 0 && prev + add <= s->max)
        if (__atomic_compare_exchange_n(&s->count, &prev, prev + add, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) { ok = 1; break; }
    if (ok && w32_has_waiters(ARG(1))) { SDL_LockMutex(g_sync); SDL_CondBroadcast(g_sync_cv); SDL_UnlockMutex(g_sync); }
    if (ok && ARG(3)) W32(ARG(3), (uint32_t)prev);
    if (!ok) w32_set_last_error(c, 298);   // ERROR_TOO_MANY_POSTS
    STDRET(3, ok);
}
K(ReleaseMutex) { STDRET(1, 1); }   // mutexes are always signalled in this runtime (handles.c)
K(PulseEvent) { HOSTFN; w32_event_pulse(ARG(1)); STDRET(1, 1); }
K(InterlockedExchange) { HOSTFN; STDRET(2, __atomic_exchange_n((uint32_t *)(mb + ARG(1)), ARG(2), __ATOMIC_SEQ_CST)); }
K(InterlockedExchangeAdd) { HOSTFN; STDRET(2, __atomic_fetch_add((uint32_t *)(mb + ARG(1)), ARG(2), __ATOMIC_SEQ_CST)); }
K(InterlockedCompareExchange) {   // (dest, exchange, comparand) -> initial value
    HOSTFN;
    uint32_t expected = ARG(3);
    __atomic_compare_exchange_n((uint32_t *)(mb + ARG(1)), &expected, ARG(2), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    STDRET(3, expected);
}
K(DuplicateHandle) {   // (src process, src handle, dst process, dst handle *, access, inherit, options): same handle value
    HOSTFN;
    if (ARG(4)) W32(ARG(4), ARG(2));
    STDRET(7, 1);
}
K(SetThreadAffinityMask) { STDRET(2, 1); }
K(SetThreadPriorityBoost) { STDRET(2, 1); }

// ---------- process / environment ----------
K(FatalAppExitA) { HOSTFN; rt_fatal("FatalAppExitA: %s", GSTR(ARG(2)) ? GSTR(ARG(2)) : ""); }
K(OutputDebugStringA) { HOSTFN; rt_log("[debug] %s", GSTR(ARG(1)) ? GSTR(ARG(1)) : ""); STDRET(1, 0); }
K(SetConsoleCtrlHandler) { STDRET(2, 1); }
K(SetErrorMode) { STDRET(1, 0); }
K(SetEnvironmentVariableA) { STDRET(2, 1); }
K(GetLogicalDrives) { STDRET(0, 1u << 2); }   // C:
K(GetProcessHeap) { STDRET(0, 0x00a00000u); }   // same pseudo handle as HeapCreate (kernel32.c)
K(lstrlenA) { HOSTFN; STDRET(1, ARG(1) ? (uint32_t)strlen(GSTR(ARG(1))) : 0); }
K(SetCurrentDirectoryA) { HOSTFN; rt_log("SetCurrentDirectoryA(\"%s\")\n", GSTR(ARG(1)) ? GSTR(ARG(1)) : ""); STDRET(1, 1); }
K(GetCurrentDirectoryA) {   // the game folder is always the guest root
    HOSTFN;
    const char *d = "C:\\SH3";
    uint32_t len = (uint32_t)strlen(d);
    if (ARG(1) <= len) STDRET(2, len + 1);
    memcpy(mb + ARG(2), d, len + 1);
    STDRET(2, len);
}
K(GetFullPathNameA) {   // (path, buffer length, buffer, file part *)
    HOSTFN;
    const char *p = GSTR(ARG(1)) ? GSTR(ARG(1)) : "";
    char full[600];
    if (p[0] && p[1] == ':') SDL_snprintf(full, sizeof full, "%s", p);
    else SDL_snprintf(full, sizeof full, "%s%s", GUEST_ROOT, p[0] == '\\' || p[0] == '/' ? p + 1 : p);
    uint32_t len = (uint32_t)strlen(full);
    if (ARG(2) <= len) STDRET(4, len + 1);
    memcpy(mb + ARG(3), full, len + 1);
    if (ARG(4)) {
        const char *slash = strrchr(full, '\\');
        W32(ARG(4), ARG(3) + (uint32_t)(slash ? slash + 1 - full : 0));
    }
    STDRET(4, len);
}

// ---------- files ----------
K(DeleteFileA) {
    HOSTFN;
    char hp[600];
    w32_host_path(GSTR(ARG(1)) ? GSTR(ARG(1)) : "", hp, sizeof hp);
    int ok = remove(hp) == 0;
    rt_log("DeleteFileA(\"%s\") -> %d\n", GSTR(ARG(1)), ok);
    if (!ok) w32_set_last_error(c, 2);
    STDRET(1, ok);
}

typedef struct { DIR *d; char dir[600], pattern[260]; } Find;
static int wildmatch(const char *p, const char *s) {   // * and ?, case-insensitive (Windows names)
    for (; *p; p++, s++) {
        if (*p == '*') { for (;; s++) { if (wildmatch(p + 1, s)) return 1; if (!*s) return 0; } }
        if (!*s || (*p != '?' && tolower((unsigned char)*p) != tolower((unsigned char)*s))) return 0;
    }
    return !*s;
}
static int find_next(Find *f, uint8_t *mb, uint32_t data) {   // fills WIN32_FIND_DATAA
    struct dirent *e;
    while ((e = readdir(f->d))) {
        if (!wildmatch(f->pattern, e->d_name)) continue;
        char path[900];
        struct stat st;
        SDL_snprintf(path, sizeof path, "%s/%s", f->dir, e->d_name);
        int ok = stat(path, &st) == 0;
        memset(mb + data, 0, 320);
        W32(data, ok && S_ISDIR(st.st_mode) ? 0x10u : 0x80u);   // DIRECTORY / NORMAL
        W32(data + 32, ok ? (uint32_t)st.st_size : 0);           // nFileSizeLow
        uint64_t ft = ok ? ((uint64_t)st.st_mtime + 11644473600ull) * 10000000ull : 0;   // Unix -> FILETIME
        W64(data + 4, ft); W64(data + 12, ft); W64(data + 20, ft);  // creation, last access, last write
        SDL_strlcpy((char *)mb + data + 44, e->d_name, 260);
        return 1;
    }
    return 0;
}
K(FindFirstFileA) {
    HOSTFN;
    char hp[600];
    w32_host_path(GSTR(ARG(1)) ? GSTR(ARG(1)) : "", hp, sizeof hp);
    Find *f = calloc(1, sizeof *f);
    char *slash = strrchr(hp, '/');
    if (f && slash) { *slash = 0; SDL_strlcpy(f->dir, hp, sizeof f->dir); SDL_strlcpy(f->pattern, slash + 1, sizeof f->pattern); }
    else if (f) { SDL_strlcpy(f->dir, ".", sizeof f->dir); SDL_strlcpy(f->pattern, hp, sizeof f->pattern); }
    if (f) f->d = opendir(f->dir);
    if (!f || !f->d || !find_next(f, mb, ARG(2))) {
        if (f && f->d) closedir(f->d);
        free(f);
        rt_log("FindFirstFileA(\"%s\") -> none\n", GSTR(ARG(1)));
        w32_set_last_error(c, 2);
        STDRET(2, 0xffffffffu);
    }
    STDRET(2, h_new(H_FIND, f));
}
K(FindNextFileA) {
    HOSTFN;
    Find *f = h_get(ARG(1), H_FIND);
    if (f && find_next(f, mb, ARG(2))) STDRET(2, 1);
    w32_set_last_error(c, 18);   // ERROR_NO_MORE_FILES
    STDRET(2, 0);
}
K(FindClose) {
    HOSTFN;
    Find *f = h_get(ARG(1), H_FIND);
    if (!f) STDRET(1, 0);
    closedir(f->d);
    free(f);
    h_close(ARG(1));
    STDRET(1, 1);
}

// File mapping: a read-only copy of the file in guest memory (the game maps data files, it does not write through views).
FILE *w32_file_stream(uint32_t h, const char **path);
typedef struct { uint32_t file; } Mapping;
K(CreateFileMappingA) {   // (file, attrs, protect, size high, size low, name)
    HOSTFN;
    if (!w32_file_stream(ARG(1), NULL)) { w32_set_last_error(c, 6); STDRET(6, 0); }
    Mapping *m = calloc(1, sizeof *m);
    if (!m) { w32_set_last_error(c, 8); STDRET(6, 0); }
    m->file = ARG(1);
    STDRET(6, h_new(H_MAPPING, m));
}
K(MapViewOfFile) {   // (mapping, access, offset high, offset low, bytes)
    HOSTFN;
    Mapping *m = h_get(ARG(1), H_MAPPING);
    const char *path = NULL;
    FILE *f = m ? w32_file_stream(m->file, &path) : NULL;
    if (!f) { w32_set_last_error(c, 6); STDRET(5, 0); }
    long keep = ftell(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    uint32_t off = ARG(4), n = ARG(5) ? ARG(5) : (uint32_t)(size - (long)off);
    uint32_t view = gheap_alloc(n ? n : 1);
    if (!view) { fseek(f, keep, SEEK_SET); w32_set_last_error(c, 8); STDRET(5, 0); }
    fseek(f, (long)off, SEEK_SET);
    size_t got = fread(mb + view, 1, n, f);
    fseek(f, keep, SEEK_SET);
    rt_log("MapViewOfFile(\"%s\", +%u, %u) -> %08x (%u read)\n", path, off, n, view, (unsigned)got);
    STDRET(5, view);
}
K(UnmapViewOfFile) { HOSTFN; gheap_free(ARG(1)); STDRET(1, 1); }

// ---------- locale ----------
K(GetThreadLocale) { STDRET(0, 0x409); }
K(GetUserDefaultLCID) { STDRET(0, 0x409); }
K(GetUserDefaultLangID) { STDRET(0, 0x409); }
K(IsValidCodePage) { STDRET(1, 1); }
K(IsValidLocale) { STDRET(2, 1); }
K(EnumSystemLocalesA) { STDRET(2, 1); }   // no callback: only the default locale exists
K(GetLocaleInfoW) { ONCE("GetLocaleInfoW"); STDRET(4, 0); }
static uint32_t compare_result(int r) { return r < 0 ? 1 : r > 0 ? 3 : 2; }   // CSTR_LESS_THAN / EQUAL / GREATER_THAN
K(CompareStringA) {   // (lcid, flags, s1, n1, s2, n2); flag 1 = NORM_IGNORECASE
    HOSTFN;
    const char *a = GSTR(ARG(3)), *b = GSTR(ARG(5));
    size_t na = (int32_t)ARG(4) < 0 ? strlen(a) : ARG(4), nb = (int32_t)ARG(6) < 0 ? strlen(b) : ARG(6);
    size_t n = na < nb ? na : nb;
    int r = (ARG(2) & 1) ? SDL_strncasecmp(a, b, n) : strncmp(a, b, n);
    if (!r) r = na < nb ? -1 : na > nb;
    STDRET(6, compare_result(r));
}
K(CompareStringW) {
    HOSTFN;
    uint32_t a = ARG(3), b = ARG(5), na = ARG(4), nb = ARG(6);
    if ((int32_t)na < 0) for (na = 0; R16(a + 2 * na); na++) {}
    if ((int32_t)nb < 0) for (nb = 0; R16(b + 2 * nb); nb++) {}
    int r = 0;
    for (uint32_t i = 0; !r && i < na && i < nb; i++) {
        uint16_t x = R16(a + 2 * i), y = R16(b + 2 * i);
        if (ARG(2) & 1) { if (x < 0x80) x = (uint16_t)tolower(x); if (y < 0x80) y = (uint16_t)tolower(y); }
        r = (int)x - (int)y;
    }
    if (!r) r = na < nb ? -1 : na > nb;
    STDRET(6, compare_result(r));
}

// ---------- time ----------
static void put_systemtime(uint8_t *mb, uint32_t p, const struct tm *t, unsigned ms) {
    uint16_t v[8] = {(uint16_t)(t->tm_year + 1900), (uint16_t)(t->tm_mon + 1), (uint16_t)t->tm_wday, (uint16_t)t->tm_mday,
                     (uint16_t)t->tm_hour, (uint16_t)t->tm_min, (uint16_t)t->tm_sec, (uint16_t)ms};
    memcpy(mb + p, v, sizeof v);
}
K(GetLocalTime) {
    HOSTFN;
    time_t now = time(NULL);
    struct tm t = *localtime(&now);
    put_systemtime(mb, ARG(1), &t, 0);
    STDRET(1, 0);
}
K(FileTimeToLocalFileTime) { HOSTFN; W64(ARG(2), R64(ARG(1))); STDRET(2, 1); }   // UTC kept as local
K(FileTimeToSystemTime) {   // FILETIME (100 ns since 1601) -> SYSTEMTIME; gmtime crashed before 1970
    HOSTFN;
    uint16_t st[8];
    filetime_to_systemtime(R64(ARG(1)), st);
    memcpy(mb + ARG(2), st, sizeof st);
    STDRET(2, 1);
}
K(GetTimeZoneInformation) { HOSTFN; memset(mb + ARG(1), 0, 172); STDRET(1, 0); }   // TIME_ZONE_ID_UNKNOWN, UTC
static uint32_t put_text(CPU *c, uint32_t buf, uint32_t cap, const char *s) {
    HOSTFN;
    uint32_t len = (uint32_t)strlen(s) + 1;
    if (!cap) return len;
    if (cap < len) return 0;
    memcpy(mb + buf, s, len);
    return len;
}
K(GetDateFormatA) {   // (lcid, flags, SYSTEMTIME * or NULL, format, buffer, size): always MM/DD/YY
    HOSTFN;
    char s[32];
    time_t now = time(NULL);
    struct tm t = *localtime(&now);
    int y = t.tm_year + 1900, m = t.tm_mon + 1, d = t.tm_mday;
    if (ARG(3)) { y = R16(ARG(3)); m = R16(ARG(3) + 2); d = R16(ARG(3) + 6); }
    SDL_snprintf(s, sizeof s, "%02d/%02d/%02d", m, d, y % 100);
    STDRET(6, put_text(c, ARG(5), ARG(6), s));
}
K(GetTimeFormatA) {   // always HH:MM:SS
    HOSTFN;
    char s[32];
    time_t now = time(NULL);
    struct tm t = *localtime(&now);
    int h = t.tm_hour, mi = t.tm_min, se = t.tm_sec;
    if (ARG(3)) { h = R16(ARG(3) + 8); mi = R16(ARG(3) + 10); se = R16(ARG(3) + 12); }
    SDL_snprintf(s, sizeof s, "%02d:%02d:%02d", h, mi, se);
    STDRET(6, put_text(c, ARG(5), ARG(6), s));
}

// ---------- OLEAUT32 (BSTR) ----------
void imp_OLEAUT32_ord2(CPU *c) {   // SysAllocString(wide string): length prefix + characters + NUL
    HOSTFN;
    uint32_t s = ARG(1), n = 0;
    if (!s) STDRET(1, 0);
    while (R16(s + 2 * n)) n++;
    uint32_t b = gheap_alloc(4 + 2 * n + 2);
    if (!b) STDRET(1, 0);
    W32(b, 2 * n);
    memcpy(mb + b + 4, mb + s, 2 * n + 2);
    STDRET(1, b + 4);
}
void imp_OLEAUT32_ord6(CPU *c) { HOSTFN; if (ARG(1)) gheap_free(ARG(1) - 4); STDRET(1, 0); }   // SysFreeString

// ---------- IMM32: no input method editor ----------
void imp_IMM32_ImmGetContext(CPU *c) { STDRET(1, 0); }
void imp_IMM32_ImmAssociateContext(CPU *c) { STDRET(2, 0); }

// ---------- GDI32: device contexts and DIB sections, no text rendering yet ----------
#define FAKE_GDI 0x0001f000u
G(CreateCompatibleDC) { ONCE("CreateCompatibleDC"); STDRET(1, FAKE_GDI + 0x10); }
G(DeleteDC) { STDRET(1, 1); }
G(CreateDIBSection) {   // (hdc, BITMAPINFO *, usage, bits **, section, offset): real pixel memory, nothing draws into it yet
    HOSTFN;
    ONCE("CreateDIBSection");
    uint32_t bmi = ARG(2);
    int32_t w = (int32_t)R32(bmi + 4), h = (int32_t)R32(bmi + 8);
    uint32_t bpp = R16(bmi + 14), bytes = (uint32_t)((w * (int32_t)bpp + 31) / 32 * 4) * (uint32_t)(h < 0 ? -h : h);
    uint32_t bits = gheap_calloc(bytes ? bytes : 4);
    if (ARG(4)) W32(ARG(4), bits);
    rt_log("CreateDIBSection %dx%d %u bpp -> %08x\n", w, h, bpp, bits);
    STDRET(6, bits ? FAKE_GDI + 0x20 : 0);
}
G(CreateFontIndirectA) { ONCE("CreateFontIndirectA"); STDRET(1, FAKE_GDI + 0x30); }
G(GetStockObject) { STDRET(1, FAKE_GDI + 0x40); }
G(SelectObject) { STDRET(2, FAKE_GDI + 0x40); }
G(GetObjectA) { ONCE("GetObjectA"); STDRET(3, 0); }
G(SetBkColor) { STDRET(2, 0); }
G(SetTextColor) { STDRET(2, 0); }
G(SetBkMode) { STDRET(2, 1); }
