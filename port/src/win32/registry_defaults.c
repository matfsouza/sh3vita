// Registry values of the SH3 PC installation (docs/BUGS.md BUG-005).
#include "registry_defaults.h"
#include <string.h>
#include <strings.h>

static const char ROOT[] = "C:\\SH3\\";   // must match GUEST_ROOT (win32.h)
static const char *const NAMES[] = {"INSTALLDIR", "data", "save", "movie", "sound"};

int reg_key_exists(const char *path) {
    static const char KEY[] = "SOFTWARE\\KONAMI\\SILENT HILL 3";
    size_t n = strlen(path), k = sizeof KEY - 1;
    if (n && (path[n - 1] == '\\' || path[n - 1] == '/')) n--;
    return n == k && !strncasecmp(path, KEY, k);
}

uint32_t reg_default_value(const char *name, char *out, size_t cap) {
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
        if (!strcasecmp(name, NAMES[i])) {
            if (cap >= sizeof ROOT) memcpy(out, ROOT, sizeof ROOT);
            return sizeof ROOT;
        }
    return 0;
}
