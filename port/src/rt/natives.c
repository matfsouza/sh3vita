// Native replacements of guest functions (recomp/x86rec.py OVERRIDES / NAME_OVERRIDES).
// Convention: same effects as the guest function, including on esp (cdecl: RET pops only the return address).
#include "rt.h"
#include <string.h>

// CRT memcpy / memmove (dst, src, n) -> dst. The guest versions jump through internal tables (BUG-014).
// memmove semantics for both, as the guest memcpy also handles overlap.
static void copy(CPU *c) {
    uint8_t *const mb = g_mem;
    uint32_t d = R32(c->esp + 4), s = R32(c->esp + 8), n = R32(c->esp + 12);
    if (n) memmove(mb + d, mb + s, n);
    c->eax = d;
    c->esp += 4;
}
void nat_memcpy(CPU *c) { copy(c); }
void nat_memmove(CPU *c) { copy(c); }
