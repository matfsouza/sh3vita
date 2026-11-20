// Registry values of the SH3 PC installation.
#include "registry_defaults.h"
#include <string.h>
#include <strings.h>

static const char ROOT[] = "C:\\SH3\\";   // must match GUEST_ROOT (win32.h)
static const char *const NAMES[] = {"INSTALLDIR", "data", "save", "movie", "sound"};

int reg_key_exists(const char *path) {
    // SH3's own key, and Silent Hill 2's: sh3.exe 0x5e96f0 turns the four SH2 references on when that key has an
    // INSTALLDIR string (the PC port's stand-in for the PS2's "completed SH2 save on the memory card").
    static const char *const KEYS[] = {"SOFTWARE\\KONAMI\\SILENT HILL 3", "SOFTWARE\\KONAMI\\SILENT HILL2"};
    size_t n = strlen(path);
    if (n && (path[n - 1] == '\\' || path[n - 1] == '/')) n--;
    for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
        if (n == strlen(KEYS[i]) && !strncasecmp(path, KEYS[i], n)) return 1;
    return 0;
}

uint32_t reg_default_value(const char *name, char *out, size_t cap) {
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
        if (!strcasecmp(name, NAMES[i])) {
            if (cap >= sizeof ROOT) memcpy(out, ROOT, sizeof ROOT);
            return sizeof ROOT;
        }
    return 0;
}
