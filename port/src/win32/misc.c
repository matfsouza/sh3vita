// ADVAPI32 (emulated registry), GDI32, IMM32, WINMM, OLE32.
#include "win32.h"
#include <stdlib.h>

// ---------- registry: small in-memory table ----------
typedef struct { char key[128], name[64]; uint32_t type, size; uint8_t data[260]; } RegVal;
#define MAX_REG 64
static RegVal reg[MAX_REG];
static int reg_n;
// SH3 reads INSTALLDIR and one base folder per data class under
// HKLM\SOFTWARE\KONAMI\SILENT HILL 3; the key path is not checked: key "*" matches any.
#include "registry_defaults.h"

static RegVal *reg_find(const char *key, const char *name) {
    for (int i = 0; i < reg_n; i++)
        if ((!strcmp(reg[i].key, "*") || !SDL_strcasecmp(reg[i].key, key)) && !SDL_strcasecmp(reg[i].name, name)) return &reg[i];
    return NULL;
}
static void reg_put(const char *key, const char *name, uint32_t type, const void *data, uint32_t size) {
    RegVal *v = reg_find(key, name);
    if (!v) {
        if (reg_n == MAX_REG) return;
        v = &reg[reg_n++];
        SDL_strlcpy(v->key, key, sizeof v->key);
        SDL_strlcpy(v->name, name, sizeof v->name);
    }
    v->type = type;
    v->size = size > sizeof v->data ? sizeof v->data : size;
    memcpy(v->data, data, v->size);
}
static void reg_defaults(void) {
    static int done;
    if (done) return;
    done = 1;
    static const char *const names[] = {"INSTALLDIR", "data", "save", "movie", "sound"};
    char value[64];
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
        uint32_t n = reg_default_value(names[i], value, sizeof value);
        if (n) reg_put("*", names[i], 1, value, n);   // REG_SZ
    }
}

typedef struct { char path[128]; } RegKey;
#define A(name) void imp_ADVAPI32_##name(CPU *c)
// Opens a key that exists (reg_key_exists): only SH3's own; ERROR_FILE_NOT_FOUND (2) otherwise, e.g. Silent Hill 2.
static uint32_t reg_open(CPU *c, uint32_t subkey, uint32_t result) {
    HOSTFN;
    reg_defaults();
    const char *path = GSTR(subkey) ? GSTR(subkey) : "";
    rt_log("RegOpenKey(\"%s\") -> %s\n", path, reg_key_exists(path) ? "ok" : "not found");
    W32(result, 0);
    if (!reg_key_exists(path)) return 2;
    RegKey *k = calloc(1, sizeof *k);
    if (!k) return 8;
    SDL_strlcpy(k->path, path, sizeof k->path);
    W32(result, h_new(H_REGKEY, k));
    return 0;
}
A(RegOpenKeyA) { HOSTFN; STDRET(3, reg_open(c, ARG(2), ARG(3))); }            // (hKey, lpSubKey, phkResult)
A(RegOpenKeyExA) { HOSTFN; STDRET(5, reg_open(c, ARG(2), ARG(5))); }          // (hKey, lpSubKey, options, sam, phkResult)
A(RegCloseKey) { HOSTFN; free(h_get(ARG(1), H_REGKEY)); h_close(ARG(1)); STDRET(1, 0); }
A(RegQueryValueExA) {
    HOSTFN;
    RegKey *k = h_get(ARG(1), H_REGKEY);
    RegVal *v = k ? reg_find(k->path, GSTR(ARG(2)) ? GSTR(ARG(2)) : "") : NULL;
    if (!v) STDRET(6, 2); // ERROR_FILE_NOT_FOUND
    if (ARG(4)) W32(ARG(4), v->type);
    uint32_t cap = ARG(6) ? R32(ARG(6)) : 0;
    if (ARG(6)) W32(ARG(6), v->size);
    if (ARG(5)) {
        if (cap < v->size) STDRET(6, 234); // ERROR_MORE_DATA
        memcpy(mb + ARG(5), v->data, v->size);
    }
    STDRET(6, 0);
}
A(RegSetValueExA) {
    HOSTFN;
    RegKey *k = h_get(ARG(1), H_REGKEY);
    if (k) reg_put(k->path, GSTR(ARG(2)) ? GSTR(ARG(2)) : "", ARG(4), mb + ARG(5), ARG(6));
    STDRET(6, 0);
}
A(RegDeleteValueA) {
    HOSTFN;
    RegKey *k = h_get(ARG(1), H_REGKEY);
    RegVal *v = k ? reg_find(k->path, GSTR(ARG(2)) ? GSTR(ARG(2)) : "") : NULL;
    if (v) *v = reg[--reg_n];
    STDRET(2, v ? 0 : 2);
}

// ---------- GDI32 / IMM32 ----------
void imp_GDI32_DeleteObject(CPU *c) { STDRET(1, 1); }
void imp_IMM32_ImmAssociateContextEx(CPU *c) { STDRET(3, 1); }

// ---------- WINMM ----------
#define W(name) void imp_WINMM_##name(CPU *c)
W(timeBeginPeriod) { STDRET(1, 0); }
W(timeEndPeriod) { STDRET(1, 0); }
W(timeGetTime) { STDRET(0, w32_tick_ms()); }

// timeSetEvent / timeKillEvent: mm_timer.c

// ---------- OLE32 ----------
#define O(name) void imp_OLE32_##name(CPU *c)
O(CoInitialize) { STDRET(1, 0); }
O(CoUninitialize) { STDRET(0, 0); }
O(CoFreeUnusedLibraries) { STDRET(0, 0); }
O(CoTaskMemAlloc) { HOSTFN; STDRET(1, gheap_alloc(ARG(1))); }
O(CoTaskMemFree) { HOSTFN; gheap_free(ARG(1)); STDRET(1, 0); }
int movie_cocreate(CPU *c, uint32_t clsid, uint32_t riid, uint32_t out, uint32_t *hr);
O(CoCreateInstance) {
    HOSTFN;
    uint32_t g = ARG(1), mhr;
    // Movies: the emulated DirectShow graph plays SH3's .000. SH3_MOVIES=0 skips them, as on a PC
    // without DirectShow.
    static int movies = -1;
    if (movies < 0) movies = !(getenv("SH3_MOVIES") && !strcmp(getenv("SH3_MOVIES"), "0"));
    if (movies && movie_cocreate(c, g, ARG(4), ARG(5), &mhr)) STDRET(5, mhr);
    rt_log("CoCreateInstance({%08x-...}) -> REGDB_E_CLASSNOTREG\n", R32(g));
    W32(ARG(5), 0);
    STDRET(5, 0x80040154u);
}
