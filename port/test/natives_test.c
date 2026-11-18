// Native replacements of CRT functions. Built and run by tests/test_natives.py.
#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *g_mem;
void nat_memcpy(CPU *c);
void nat_memmove(CPU *c);

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void put32(uint32_t a, uint32_t v) { memcpy(g_mem + a, &v, 4); }

static void call(void (*fn)(CPU *), CPU *c, uint32_t d, uint32_t s, uint32_t n) {   // cdecl (dst, src, n)
    c->esp = 0x1000;
    put32(c->esp, 0xdeadbeef);   // return address
    put32(c->esp + 4, d); put32(c->esp + 8, s); put32(c->esp + 12, n);
    fn(c);
}

int main(void) {
    g_mem = calloc(1, 0x10000);
    CPU c = {0};
    for (int i = 0; i < 64; i++) g_mem[0x2000 + i] = (uint8_t)i;
    call(nat_memcpy, &c, 0x3000, 0x2000, 37);
    CHECK(!memcmp(g_mem + 0x3000, g_mem + 0x2000, 37) && g_mem[0x3000 + 37] == 0, "memcpy copies exactly n bytes");
    CHECK(c.eax == 0x3000, "memcpy returns dst, got %x", c.eax);
    CHECK(c.esp == 0x1004, "cdecl: only the return address is popped, esp=%x", c.esp);
    call(nat_memmove, &c, 0x2004, 0x2000, 32);   // overlapping, forward
    int ok = 1;
    for (int i = 0; i < 32; i++) ok &= g_mem[0x2004 + i] == (uint8_t)i;
    CHECK(ok, "memmove handles overlap");
    CHECK(c.eax == 0x2004 && c.esp == 0x1004, "memmove returns dst and pops only the return address");
    call(nat_memcpy, &c, 0x3000, 0x2000, 0);
    CHECK(c.eax == 0x3000, "zero-length copy still returns dst");
    printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
    return fails != 0;
}
