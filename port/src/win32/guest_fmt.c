// wsprintfA-style formatting for the guest (USER32 wsprintfA / wvsprintfA).
#include "guest_fmt.h"
#include <stdio.h>
#include <string.h>

static uint32_t rd32(const uint8_t *mb, uint32_t a) { uint32_t v; memcpy(&v, mb + a, 4); return v; }

int guest_vformat(const uint8_t *mb, char *out, size_t cap, uint32_t fmt, uint32_t args) {
    size_t n = 0;
    if (!cap) return 0;
#define PUT(ch) do { if (n + 1 < cap) out[n++] = (char)(ch); } while (0)
    for (const char *p = (const char *)mb + fmt; *p; p++) {
        if (*p != '%') { PUT(*p); continue; }
        char spec[16]; size_t k = 0;
        spec[k++] = '%';
        p++;
        while (*p == '-' || *p == '0' || *p == '#') { if (k < 12) spec[k++] = *p; p++; }
        while (*p >= '0' && *p <= '9') { if (k < 12) spec[k++] = *p; p++; }
        if (*p == '.') { if (k < 12) spec[k++] = *p; p++; while (*p >= '0' && *p <= '9') { if (k < 12) spec[k++] = *p; p++; } }
        while (*p == 'l' || *p == 'h' || *p == 'w') p++;   // all integers and pointers are 32-bit on the guest
        if (!*p) break;
        char conv = *p, buf[64];
        if (conv == '%') { PUT('%'); continue; }
        if (conv == 'i') conv = 'd';
        if (conv == 'p') conv = 'X';
        spec[k++] = conv; spec[k] = 0;
        uint32_t v = rd32(mb, args); args += 4;
        if (conv == 's') {
            const char *s = v ? (const char *)mb + v : "(null)";
            snprintf(buf, sizeof buf, "%%%s", spec + 1);      // keep flags/width/precision, %s on a host string
            char tmp[1024];
            snprintf(tmp, sizeof tmp, buf, s);
            for (const char *t = tmp; *t; t++) PUT(*t);
            continue;
        }
        if (conv == 'd') snprintf(buf, sizeof buf, spec, (int32_t)v);
        else if (conv == 'u' || conv == 'x' || conv == 'X') snprintf(buf, sizeof buf, spec, v);
        else if (conv == 'c') snprintf(buf, sizeof buf, spec, (int)(v & 0xff));
        else { snprintf(buf, sizeof buf, "%%%c", conv); }   // unknown conversion: print it as is
        for (const char *t = buf; *t; t++) PUT(*t);
    }
#undef PUT
    out[n] = 0;
    return (int)n;
}
