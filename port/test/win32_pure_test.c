// Tests of the pure Win32 helpers: guest printf formatting (wsprintfA) and RIFF chunk search (mmioDescend).
// Built and run by tests/test_win32_pure.py.
#include "guest_fmt.h"
#include "mmio_core.h"
#include "registry_defaults.h"
#include "resource_strings.h"
#include "w32_time.h"
#include <stdio.h>
#include <string.h>

static uint8_t mem[4096];
static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t put_str(uint32_t a, const char *s) { strcpy((char *)mem + a, s); return a; }
static void put_u32(uint32_t a, uint32_t v) { memcpy(mem + a, &v, 4); }

static void test_format(void) {
    char out[256];
    uint32_t fmt = put_str(0x100, "%d %u %x %X %08x [%s] %c %%");
    uint32_t args = 0x400, s = put_str(0x200, "abc");
    uint32_t v[] = {(uint32_t)-42, 7u, 0xbeefu, 0xbeefu, 0x1fu, s, 'Z'};
    for (unsigned i = 0; i < 7; i++) put_u32(args + 4 * i, v[i]);
    int n = guest_vformat(mem, out, sizeof out, fmt, args);
    CHECK(!strcmp(out, "-42 7 beef BEEF 0000001f [abc] Z %"), "got \"%s\"", out);
    CHECK(n == (int)strlen(out), "length %d", n);

    fmt = put_str(0x100, "%-5s|%5d|%ld|%lu|%hs");
    s = put_str(0x200, "ab");
    uint32_t w[] = {s, 12u, (uint32_t)-3, 3000000000u, s};
    for (unsigned i = 0; i < 5; i++) put_u32(args + 4 * i, w[i]);
    guest_vformat(mem, out, sizeof out, fmt, args);
    CHECK(!strcmp(out, "ab   |   12|-3|3000000000|ab"), "got \"%s\"", out);

    fmt = put_str(0x100, "data\\%s%02d.bin");
    s = put_str(0x200, "bg");
    put_u32(args, s); put_u32(args + 4, 5);
    guest_vformat(mem, out, sizeof out, fmt, args);
    CHECK(!strcmp(out, "data\\bg05.bin"), "got \"%s\"", out);

    fmt = put_str(0x100, "%s");
    put_u32(args, 0);
    guest_vformat(mem, out, sizeof out, fmt, args);
    CHECK(!strcmp(out, "(null)"), "null string got \"%s\"", out);

    fmt = put_str(0x100, "0123456789");
    n = guest_vformat(mem, out, 5, fmt, args);
    CHECK(!strcmp(out, "0123") && n == 4, "truncation got \"%s\" n=%d", out, n);
}

// Same RIFF bytes for both streams: RIFF/WAVE { "fmt " (16 bytes), "data" (5 bytes + pad byte) }.
static size_t make_riff(uint8_t *out) {
    size_t n = 0;
#define PUT4(s) (memcpy(out + n, s, 4), n += 4)
#define PUTU(v) do { uint32_t v_ = (v); memcpy(out + n, &v_, 4); n += 4; } while (0)
    PUT4("RIFF"); PUTU(4 + 8 + 16 + 8 + 5 + 1); PUT4("WAVE");
    PUT4("fmt "); PUTU(16); for (int i = 0; i < 16; i++) out[n++] = (uint8_t)i;
    PUT4("data"); PUTU(5); memcpy(out + n, "hello", 5); n += 5; out[n++] = 0;
#undef PUT4
#undef PUTU
    return n;
}

static void riff_checks(MmioStream *s, const char *kind) {
    uint32_t riff[5] = {0}, ck[5] = {0};
    riff[2] = MMIO_FOURCC('W', 'A', 'V', 'E');
    CHECK(mmio_descend(s, riff, NULL, MMIO_FINDRIFF) == 0, "%s: find RIFF WAVE", kind);
    CHECK(riff[0] == MMIO_FOURCC('R', 'I', 'F', 'F') && riff[3] == 8 && ms_tell(s) == 12,
          "%s: riff ckid=%08x off=%u pos=%ld", kind, riff[0], riff[3], ms_tell(s));

    ck[0] = MMIO_FOURCC('d', 'a', 't', 'a');
    CHECK(mmio_descend(s, ck, riff, MMIO_FINDCHUNK) == 0, "%s: find data", kind);
    CHECK(ck[1] == 5 && ck[3] == 12 + 8 + 16 + 8 && ms_tell(s) == (long)ck[3], "%s: data size=%u off=%u pos=%ld", kind, ck[1], ck[3], ms_tell(s));
    char buf[6] = {0};
    CHECK(ms_read(s, buf, 5) == 5 && !strcmp(buf, "hello"), "%s: data contents", kind);
    CHECK(mmio_ascend(s, ck) == 0 && ms_tell(s) == 50, "%s: ascend skips pad: pos=%ld", kind, ms_tell(s));
    CHECK(ms_read(s, buf, 4) == 0, "%s: read at end returns 0", kind);

    ms_seek(s, 12, SEEK_SET);
    ck[0] = MMIO_FOURCC('L', 'I', 'S', 'T');
    CHECK(mmio_descend(s, ck, riff, MMIO_FINDCHUNK) == MMIOERR_CHUNKNOTFOUND, "%s: missing chunk", kind);

    ms_seek(s, 12, SEEK_SET);
    memset(ck, 0, sizeof ck);
    CHECK(mmio_descend(s, ck, riff, 0) == 0 && ck[0] == MMIO_FOURCC('f', 'm', 't', ' ') && ck[1] == 16, "%s: next chunk, no search", kind);
    CHECK(ms_seek(s, -4, SEEK_END) == 0 && ms_tell(s) == 46, "%s: seek from end", kind);
}

static void test_riff(const char *path) {
    uint8_t bytes[64];
    size_t n = make_riff(bytes);
    FILE *f = fopen(path, "w+b");
    fwrite(bytes, 1, n, f);
    fflush(f);
    fseek(f, 0, SEEK_SET);
    MmioStream fs = {.f = f};
    riff_checks(&fs, "file");
    fclose(f);
    remove(path);

    // BUG-012: SH3 opens sounds already loaded in memory (MMIOINFO.fccIOProc = 'MEM ').
    MmioStream ms = {.mem = bytes, .size = (uint32_t)n};
    riff_checks(&ms, "memory");
    CHECK(ms_seek(&ms, 100, SEEK_SET) != 0, "memory: seek past the end must fail");
}

static void test_registry_defaults(void) {
    // BUG-005: sh3.exe reads one base folder per data class (0x5f0dd0) besides INSTALLDIR.
    const char *names[] = {"INSTALLDIR", "data", "save", "movie", "sound", "SOUND"};
    char out[64];
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
        uint32_t n = reg_default_value(names[i], out, sizeof out);
        CHECK(n == sizeof "C:\\SH3\\" && !strcmp(out, "C:\\SH3\\"), "%s -> n=%u \"%s\"", names[i], n, out);
    }
    CHECK(reg_default_value("Install Language", out, sizeof out) == 0, "unknown value must be absent");
    // Only the SH3 key exists: a PC without Silent Hill 2 has no SH2 key (final review / BUG-019).
    CHECK(reg_key_exists("SOFTWARE\\KONAMI\\SILENT HILL 3"), "SH3 key");
    CHECK(reg_key_exists("Software\\Konami\\Silent Hill 3\\"), "SH3 key, other case and trailing backslash");
    CHECK(!reg_key_exists("SOFTWARE\\Konami\\Silent Hill2"), "SH2 key must not exist");
    CHECK(!reg_key_exists("SOFTWARE\\KONAMI\\SILENT HILL 3X"), "prefix of another key");
    CHECK(!reg_key_exists(""), "empty path");
}

static void test_string_block(void) {
    // RT_STRING block: 16 entries, each a u16 length then that many UTF-16 code units.
    uint16_t block[64] = {0};
    unsigned k = 0;
    block[k++] = 0;                                        // string 0: empty
    block[k++] = 3; block[k++] = 'A'; block[k++] = 'b'; block[k++] = 0xe9;   // string 1: "Ab" + e-acute (cp1252 0xe9)
    block[k++] = 2; block[k++] = 'H'; block[k++] = 'i';   // string 2: "Hi"
    char out[16];
    CHECK(rsrc_string_from_block((uint8_t *)block, sizeof block, 1, out, sizeof out) == 3 && !strcmp(out, "Ab\xe9"), "string 1 \"%s\"", out);
    CHECK(rsrc_string_from_block((uint8_t *)block, sizeof block, 2, out, sizeof out) == 2 && !strcmp(out, "Hi"), "string 2 \"%s\"", out);
    CHECK(rsrc_string_from_block((uint8_t *)block, sizeof block, 0, out, sizeof out) == 0 && !out[0], "string 0 empty");
    CHECK(rsrc_string_from_block((uint8_t *)block, sizeof block, 1, out, 3) == 2 && !strcmp(out, "Ab"), "truncated \"%s\"", out);
    CHECK(rsrc_string_from_block((uint8_t *)block, 6, 2, out, sizeof out) == 0, "block too short must fail");
}

static void test_filetime(void) {
    // SYSTEMTIME order: year, month, day of week (0 = Sunday), day, hour, minute, second, milliseconds.
    // BUG-013: FILETIME 0 (1601-01-01) crashed the old gmtime-based conversion.
    static const struct { uint64_t ft; uint16_t st[8]; } cases[] = {
        {0ull, {1601, 1, 1, 1, 0, 0, 0, 0}},
        {125911584000000000ull, {2000, 1, 6, 1, 0, 0, 0, 0}},
        {134380100967890000ull, {2026, 11, 0, 1, 12, 34, 56, 789}},
        {94406687990000000ull, {1900, 3, 4, 1, 23, 59, 59, 0}},
        {133536600000000000ull, {2024, 2, 4, 29, 6, 0, 0, 0}},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint16_t st[8];
        filetime_to_systemtime(cases[i].ft, st);
        CHECK(!memcmp(st, cases[i].st, sizeof st), "case %u: got %u-%u-%u (wday %u) %u:%u:%u.%u", i,
              st[0], st[1], st[3], st[2], st[4], st[5], st[6], st[7]);
    }
}

int main(int argc, char **argv) {
    test_filetime();
    test_string_block();
    test_registry_defaults();
    test_format();
    test_riff(argc > 1 ? argv[1] : "riff_test.tmp");
    printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
    return fails != 0;
}
