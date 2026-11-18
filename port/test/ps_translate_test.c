// ps.1.x -> GLSL translator tests (BUG-008). Built and run by tests/test_ps_translate.py with SH3_TRANSLATOR_STANDALONE.
#include "shader_translate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// Token helpers: register type in bits 28-30, bit 31 always set on parameters.
#define DST_T(n) (0x80000000u | 3u << 28 | 0xfu << 16 | (n))   // tN, .xyzw write mask
#define DST_R(n) (0x80000000u | 0u << 28 | 0xfu << 16 | (n))   // rN
#define SRC_T(n) (0x80000000u | 3u << 28 | 0xe4u << 16 | (n))  // tN, .xyzw swizzle
#define END 0x0000ffffu

static void test_texreg2ar_keeps_sync(void) {
    const uint32_t ps[] = {0xffff0101u, 66, DST_T(0),            // tex t0
                           69, DST_T(1), SRC_T(0),               // texreg2ar t1, t0
                           1, DST_R(0), SRC_T(1),                // mov r0, t1
                           END};
    int err = -1;
    char *s = ps_translate(ps, &err);
    CHECK(s && err == 0, "err=%d", err);
    CHECK(s && strstr(s, "t1 = texture2D(s1, t0.wx);"), "texreg2ar sampling missing:\n%s", s ? s : "");
    CHECK(s && strstr(s, "r0 = ((t1))"), "mov after texreg2ar lost:\n%s", s ? s : "");
    shader_translate_free(s);
}

static void test_texreg2gb(void) {
    const uint32_t ps[] = {0xffff0101u, 66, DST_T(0), 70, DST_T(2), SRC_T(0), 1, DST_R(0), SRC_T(2), END};
    int err = -1;
    char *s = ps_translate(ps, &err);
    CHECK(s && err == 0 && strstr(s, "t2 = texture2D(s2, t0.yz);"), "texreg2gb:\n%s", s ? s : "");
    shader_translate_free(s);
}

static void test_unsupported_tex_ops_keep_sync(void) {
    const uint32_t ps[] = {0xffff0101u, 66, DST_T(0),
                           71, DST_T(1), SRC_T(0),               // texm3x2pad t1, t0  (1 source)
                           72, DST_T(2), SRC_T(0),               // texm3x2tex t2, t0  (1 source)
                           1, DST_R(0), SRC_T(2), END};
    int err = -1;
    char *s = ps_translate(ps, &err);
    CHECK(s && strstr(s, "r0 = ((t2))"), "instruction after texm3x2 lost:\n%s", s ? s : "");
    shader_translate_free(s);
}

static void test_unterminated_is_rejected(void) {
    enum { N = 8192 };
    uint32_t *ps = calloc(N, sizeof *ps);   // version, then nops and no end token
    ps[0] = 0xffff0101u;
    int err = -1;
    char *s = ps_translate(ps, &err);
    CHECK(!s && err == 3, "unterminated stream must fail with err=3, got err=%d", err);
    shader_translate_free(s);
    free(ps);
}

static void test_unterminated_vs_is_rejected(void) {
    enum { N = 8192 };
    uint32_t *vs = calloc(N, sizeof *vs);   // vs.1.1, then nops and no end token
    vs[0] = 0xfffe0101u;
    int err = -1;
    char *s = vs_translate(vs, &err);
    CHECK(!s && err == 3, "unterminated vertex shader must fail with err=3, got err=%d", err);
    shader_translate_free(s);
    free(vs);
}

// BUG-007: the flashlight's light shape is a cube map on stage 3, sampled with the light-space direction in vT3.xyz.
static void test_cube_stage_sampling(void) {
    const char *fs = "uniform sampler2D s0;\nuniform sampler2D s3;\nvoid main() {\n"
                     "  t0 = texture2D(s0, vT0.xy);\n  t3 = texture2D(s3, vT3.xy);\n  x = texture2D(s3, vT1.xy);\n}\n";
    char *s = glsl_cube_stages(fs, 1u << 3);
    CHECK(s && strstr(s, "uniform samplerCube s3;"), "cube sampler declaration:\n%s", s ? s : "");
    CHECK(s && strstr(s, "uniform sampler2D s0;"), "2D stage must stay 2D:\n%s", s ? s : "");
    CHECK(s && strstr(s, "t3 = textureCube(s3, vT3.xyz);"), "cube sampling of stage 3:\n%s", s ? s : "");
    CHECK(s && strstr(s, "x = textureCube(s3, vT1.xyz);"), "cube sampling with another coordinate set:\n%s", s ? s : "");
    CHECK(s && strstr(s, "t0 = texture2D(s0, vT0.xy);"), "2D sampling must stay:\n%s", s ? s : "");
    shader_translate_free(s);
    s = glsl_cube_stages(fs, 0);
    CHECK(s && !strcmp(s, fs), "no cube stage: unchanged");
    shader_translate_free(s);
}

int main(void) {
    test_cube_stage_sampling();
    test_unterminated_vs_is_rejected();
    test_texreg2ar_keeps_sync();
    test_texreg2gb();
    test_unsupported_tex_ops_keep_sync();
    test_unterminated_is_rejected();
    printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
    return fails != 0;
}
