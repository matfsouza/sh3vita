// Native versions of hot game functions, for speed on the Vita: the x87 maths of D3DX and of the C runtime
// costs far more recompiled than in C. Same effects as the guest code, including the x87 stack and esp. Installed as
// wraps by recomp/image_patches.py (the original body is still recompiled, so the x87 planner knows each function's
// stack effect). SH3_NAT_OFF=1 runs the originals. SH3_NATCHECK=1 (PC) runs both and logs every difference.
#include "rt.h"
#include "ftol.h"
#include <SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int nat_on(void) {
    static int on = -1;
    if (on < 0) on = getenv("SH3_NAT_OFF") == NULL;
    return on;
}
static int nat_check(void) {
    static int on = -1;
    if (on < 0) on = getenv("SH3_NATCHECK") != NULL;
    return on;
}
static void check_log(const char *name, double a, double b) {
    static unsigned n, ok;
    static const char *seen[32];   // first check of each function: proves the run exercised it
    for (int i = 0; i < 32; i++) { if (seen[i] == name) break; if (!seen[i]) { seen[i] = name; rt_log("[natcheck] first check: %s\n", name); break; } }
    if (a == b || (a != a && b != b)) { if (!(++ok % 200000)) rt_log("[natcheck] %u equal results\n", ok); return; }
    if (n++ < 40) rt_log("[natcheck] %s: native %.17g, original %.17g\n", name, a, b);
}
// Per-native counters: calls per frame of every native and the time of the big ones, logged by
// nat_prof_report every 300 frames with SH3_STATS (sprof.c), so a Vita run tells exactly what each one costs.
enum { NP_SHADOW, NP_SEGQUAD, NP_MATMUL, NP_VT, NP_VTN, NP_V4, NP_NORM, NP_RAND, NP_PRAND, NP_QUAD, NP_CLIP, NP_FTOL, NP_QSORT, NP_STRIP, NP_N };
static const char *const np_name[NP_N] = {"shadow", "seg_quad", "matmul", "vec3transform", "vec3transformnormal",
                                         "vec4transform", "vec3normalize", "rand", "prand", "push_quad", "clip_near", "ftol2",
                                         "qsort", "strip"};
static uint32_t np_calls[NP_N];
static uint64_t np_ticks[NP_N];
#define NP(i) (np_calls[i]++)
// SH3_NATCALLERS=1: histogram of the return addresses of the native calls (which game function calls them), logged with
// the counters (diagnosis on the PC: it reads the guest stack on every call).
static struct { uint32_t ret, n; uint8_t id; } nc_tab[256];
static int nc_on(void) { static int on = -1; if (on < 0) on = getenv("SH3_NATCALLERS") != NULL; return on; }
static void nc_add(int id, uint32_t sp) {
    uint32_t ret;
    memcpy(&ret, g_mem + sp, 4);
    for (int i = 0; i < 256; i++) {
        if (nc_tab[i].n && (nc_tab[i].ret != ret || nc_tab[i].id != id)) continue;
        nc_tab[i].ret = ret; nc_tab[i].id = (uint8_t)id; nc_tab[i].n++;
        return;
    }
}
#define NPC(i) do { np_calls[i]++; if (nc_on()) nc_add(i, c->esp); } while (0)
void nat_prof_report(unsigned frames) {
    if (!frames) return;
    double f = (double)SDL_GetPerformanceFrequency();
    char line[512];
    int n = snprintf(line, sizeof line, "[natprof] per frame:");
    for (int i = 0; i < NP_N && n < (int)sizeof line - 48; i++) {
        if (!np_calls[i]) continue;
        n += snprintf(line + n, sizeof line - n, " %s %u", np_name[i], np_calls[i] / frames);
        if (np_ticks[i]) n += snprintf(line + n, sizeof line - n, " (%.2f ms)", 1e3 * np_ticks[i] / f / frames);
    }
    rt_log("%s\n", line);
    if (nc_on())
        for (int i = 0; i < 256; i++)
            if (nc_tab[i].n / frames >= 20) rt_log("[natprof]   %s from %08x: %u per frame\n", np_name[nc_tab[i].id], nc_tab[i].ret, nc_tab[i].n / frames);
    memset(nc_tab, 0, sizeof nc_tab);
    memset(np_calls, 0, sizeof np_calls);
    memset(np_ticks, 0, sizeof np_ticks);
}

// Float results that may differ from the recompiled original in the last bits: equal within 1e-5 relative (or 1e-6
// absolute near zero). The D3DX kernels below compute in single precision like the game on the PC (Direct3D 8 puts the
// x87 in 24-bit precision: CreateDevice without D3DCREATE_FPU_PRESERVE, flags 0x80 in the log), the recompiled code in
// double.
static void check_near(const char *name, float a, float b) {
    float d = fabsf(a - b), m = fabsf(a) > fabsf(b) ? fabsf(a) : fabsf(b);
    check_log(name, a, (d <= 1e-5f * m || d <= 1e-6f) ? a : b);
}

// ---------- D3DX kernels in single precision ( these four took 15% of the main thread in the park in
// double precision, mostly float <-> double conversions). Same order of operations as the x87 code, every operation
// rounded to float like the PC with Direct3D's 24-bit x87 precision. NEON on the Vita: vmla is not fused on the
// Cortex-A9 (the product is rounded before the add), so the results equal the scalar float code. ----------
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
static inline void k_matmul(float *o, const uint8_t *a, const uint8_t *b) {   // o = A * B, row-major 4x4
    float32x4_t b0 = vld1q_f32((const float *)b), b1 = vld1q_f32((const float *)(b + 16));
    float32x4_t b2 = vld1q_f32((const float *)(b + 32)), b3 = vld1q_f32((const float *)(b + 48));
    for (int r = 0; r < 4; r++) {
        float32x4_t ar = vld1q_f32((const float *)(a + 16 * r));
        float32x4_t acc = vmulq_lane_f32(b0, vget_low_f32(ar), 0);
        acc = vmlaq_lane_f32(acc, b1, vget_low_f32(ar), 1);
        acc = vmlaq_lane_f32(acc, b2, vget_high_f32(ar), 0);
        acc = vmlaq_lane_f32(acc, b3, vget_high_f32(ar), 1);
        vst1q_f32(o + 4 * r, acc);
    }
}
// o[k] = ((v0*m0k + v1*m1k) + v2*m2k) [+ v3*m3k | + m3k]; mode 3: 3x3 part, 4: (v, 1), 5: v has 4 components
static inline void k_vecmat(float *o, const float *v, const uint8_t *m, int mode) {
    float32x4_t acc = vmulq_n_f32(vld1q_f32((const float *)m), v[0]);
    acc = vmlaq_n_f32(acc, vld1q_f32((const float *)(m + 16)), v[1]);
    acc = vmlaq_n_f32(acc, vld1q_f32((const float *)(m + 32)), v[2]);
    if (mode == 4) acc = vaddq_f32(acc, vld1q_f32((const float *)(m + 48)));
    else if (mode == 5) acc = vmlaq_n_f32(acc, vld1q_f32((const float *)(m + 48)), v[3]);
    vst1q_f32(o, acc);
}
#else
static inline void k_matmul(float *o, const uint8_t *a8, const uint8_t *b8) {
    float a[16], b[16];
    memcpy(a, a8, sizeof a); memcpy(b, b8, sizeof b);
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < 4; k++) {
            volatile float s = a[r * 4] * b[k];   // volatile: one float rounding per operation on the x87 PC build too
            s = s + a[r * 4 + 1] * b[4 + k]; s = s + a[r * 4 + 2] * b[8 + k]; s = s + a[r * 4 + 3] * b[12 + k];
            o[r * 4 + k] = s;
        }
}
static inline void k_vecmat(float *o, const float *v, const uint8_t *m8, int mode) {
    float m[16];
    memcpy(m, m8, sizeof m);
    for (int k = 0; k < 4; k++) {
        volatile float s = v[0] * m[k];
        s = s + v[1] * m[4 + k]; s = s + v[2] * m[8 + k];
        if (mode == 4) s = s + m[12 + k];
        else if (mode == 5) s = s + v[3] * m[12 + k];
        o[k] = s;
    }
}
#endif

void F_00662c40_orig(CPU *c);
void F_00648639_orig(CPU *c);
void F_006483a7_orig(CPU *c);
void F_006634a0_orig(CPU *c);
void F_00667a7f_orig(CPU *c);

// 0x662c40 MSVC __ftol2: edx:eax = ST(0) truncated toward zero, pops ST(0); cdecl, no argument. Called by every
// float -> int cast of the game; the recompiled x87 sequence (fld/fst/fistp/fild + fix-up) costs far more.
void nat_ftol2(CPU *c) {
    NP(NP_FTOL);
    if (!nat_on()) { F_00662c40_orig(c); return; }
    int64_t r = sh3_ftol(ST(0));
    FPOP();
    c->eax = (uint32_t)r;
    c->edx = (uint32_t)((uint64_t)r >> 32);
    c->esp += 4;
}

// 0x648639 D3DXMatrixMultiply (x87 version picked by D3DX's CPU dispatch): out = m1 * m2 (row-major 4x4 floats),
// stdcall (out, m1, m2), returns out. The game calls it through the D3DX dispatch table for every transform.
// Computed in a temporary, so out may alias m1 and/or m2 as the original allows.
void nat_d3dx_matmul(CPU *c) {
    NPC(NP_MATMUL);
    if (!nat_on()) { F_00648639_orig(c); return; }
    uint8_t *const mb = g_mem;
    uint32_t out = R32(c->esp + 4), pa = R32(c->esp + 8), pb = R32(c->esp + 12);
    float o[16];
    k_matmul(o, mb + pa, mb + pb);
    if (nat_check()) {
        F_00648639_orig(c);
        for (int k = 0; k < 16; k++) { float g; memcpy(&g, mb + out + 4 * k, 4); check_near("matmul", o[k], g); }
        memcpy(mb + out, o, sizeof o);   // the single-precision result stays: callers checked byte for byte see the same maths
        return;
    }
    memcpy(mb + out, o, sizeof o);
    c->eax = out;
    c->esp += 16;
}

// (0x62a867 used to be wrapped here as a MatrixMultiply thunk; its slot holds D3DXVec3Transform, so that fast path never
// ran. All D3DX thunks now get the generic fast path from the recompiler: x86rec.py d3dx_thunk_fast.)

// 0x6483a7 D3DXVec3Transform (x87 version): out (4 floats) = (v, 1) * m; stdcall (out, v, m), returns out.
void nat_d3dx_vec3transform(CPU *c) {
    uint8_t *const mb = g_mem;
    NPC(NP_VT);
    uint32_t out = R32(c->esp + 4), pv = R32(c->esp + 8), pm = R32(c->esp + 12);
    float v[4], o[4];
    memcpy(v, mb + pv, 12);
    k_vecmat(o, v, mb + pm, 4);
    if (nat_check()) {
        F_006483a7_orig(c);
        for (int k = 0; k < 4; k++) { float g; memcpy(&g, mb + out + 4 * k, 4); check_near("vec3transform", o[k], g); }
        memcpy(mb + out, o, sizeof o);
        return;
    }
    if (!nat_on()) { F_006483a7_orig(c); return; }
    memcpy(mb + out, o, sizeof o);
    c->eax = out;
    c->esp += 16;
}

// 0x64856f D3DXVec3Normalize (x87 version): out = v / |v| with D3DX's table-based reciprocal square root (table at
// 0x726820: pairs of floats a, b indexed by the top mantissa bits); stdcall (out, v), returns out. Same steps and
// double intermediates as the x87 code: zero length gives 0, a length within 1e-5 of 1 copies v.
#define D3DX_RSQRT_TABLE 0x726820u
void F_0064856f_orig(CPU *c);
void nat_d3dx_vec3normalize(CPU *c) {
    uint8_t *const mb = g_mem;
    NPC(NP_NORM);
    uint32_t out = R32(c->esp + 4), pv = R32(c->esp + 8);
    float v[3], o[3];
    memcpy(v, mb + pv, sizeof v);
    double sum = ((double)v[0] * v[0] + (double)v[1] * v[1]) + (double)v[2] * v[2];
    float sf = (float)sum, d = (float)(sum - 1.0);
    uint32_t a, db;
    memcpy(&a, &sf, 4);
    memcpy(&db, &d, 4);
    int copy = 0;
    if (a == 0) o[0] = o[1] = o[2] = 0.0f;
    else if ((db & 0x7fffffffu) <= 0x3727c5acu) { copy = 1; memcpy(o, v, sizeof o); }
    else {
        uint32_t idx = (a >> 12) & 0xff8u, mbits = (a & ((1u << 24) - 1)) | 0x3f000000u, ebits = ((0xbeffffffu - a) >> 1) & 0xff800000u;
        float m, e, t0, t1;
        memcpy(&m, &mbits, 4); memcpy(&e, &ebits, 4);
        t0 = rt_f32(R32(D3DX_RSQRT_TABLE + idx)); t1 = rt_f32(R32(D3DX_RSQRT_TABLE + idx + 4));
        double r = ((double)m * t0 + t1) * e;
        for (int k = 0; k < 3; k++) o[k] = (float)(v[k] * r);
    }
    if (nat_check()) {
        F_0064856f_orig(c);
        for (int k = 0; k < 3; k++) { float g; memcpy(&g, mb + out + 4 * k, 4); check_log("vec3normalize", o[k], g); }
        return;
    }
    if (!nat_on()) { F_0064856f_orig(c); return; }
    if (!copy || out != pv) memcpy(mb + out, o, sizeof o);
    c->eax = out;
    c->esp += 12;
}

// 0x6634a0 CRT floor (double on the stack, result in ST(0); cdecl). Not finite: the original (it reports the error).
void nat_floor(CPU *c) {
    uint8_t *const mb = g_mem;
    double x;
    memcpy(&x, mb + c->esp + 4, 8);
    if (nat_check()) {
        F_006634a0_orig(c);
        check_log("floor", floor(x), ST(0));
        return;
    }
    if (!nat_on() || !isfinite(x)) { F_006634a0_orig(c); return; }
    FPUSH(floor(x));
    c->esp += 4;
}

// 0x48c290: clip of a segment against the camera's near plane.
// cdecl (out, a, b), vec3 floats; eax = 1 (b in front, out = point on the plane), 2 or 0 (out = b pushed along a->b).
// Same double intermediates and the same comparisons (NaN included) as the x87 code; it also writes its float scratch
// (b - a in x and y below the stack, the threshold over the b argument slot) like the original.
#define CLIP_NEAR_Z    0x72c26cu   // near plane distance
#define CLIP_NEAR_K    0x6909f4u
#define CLIP_SIDE_K    0x68ebacu
#define CLIP_ZERO      0x68a868u
#define CLIP_ONE       0x68a754u
#define CLIP_PUSH      0x690010u
void F_0048c290_orig(CPU *c);
static inline uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
void nat_clip_near(CPU *c) {
    uint8_t *const mb = g_mem;
    NPC(NP_CLIP);
    uint32_t sp = c->esp, out = R32(sp + 4), pa = R32(sp + 8), pb = R32(sp + 12);
    float a[3], b[3], o[3];
    memcpy(a, mb + pa, sizeof a);
    memcpy(b, mb + pb, sizeof b);
    double n = (double)rt_f32(R32(CLIP_NEAR_Z)) * rt_f32(R32(CLIP_NEAR_K));
    float d0 = (float)((double)b[0] - a[0]), d1 = (float)((double)b[1] - a[1]);
    double d2 = (double)b[2] - a[2];
    float t = (float)(rt_f32(R32(CLIP_SIDE_K)) * d2);
    double zero = rt_f32(R32(CLIP_ZERO)), one = rt_f32(R32(CLIP_ONE));
    uint32_t ret;
    if (d2 < zero && b[2] > n && (fabs(d0) < t || fabs(d1) < t)) {
        double r = one / d2;
        o[0] = (float)(((n - a[2]) * b[0] + (b[2] - n) * a[0]) * r);
        o[1] = (float)(((b[2] - n) * a[1] + (n - a[2]) * b[1]) * r);
        o[2] = (float)n;
        ret = 1;
    } else {
        double l = sqrt(((double)d1 * d1 + (double)d0 * d0) + d2 * d2);
        double f = l == zero ? one : rt_f32(R32(CLIP_PUSH)) / l;
        o[0] = (float)(d0 * f + b[0]);
        o[1] = (float)(d1 * f + b[1]);
        o[2] = (float)(f * d2 + b[2]);
        ret = n < b[2] ? 0 : 2;
    }
    if (nat_check()) {
        F_0048c290_orig(c);
        for (int k = 0; k < 3; k++) { float g; memcpy(&g, mb + out + 4 * k, 4); check_log("clip_near", o[k], g); }
        check_log("clip_near ret", ret, c->eax);
        return;
    }
    if (!nat_on()) { F_0048c290_orig(c); return; }
    W32(sp - 12, fbits(d0));
    W32(sp - 8, fbits(d1));
    W32(sp + 12, fbits(t));
    memcpy(mb + out, o, sizeof o);
    c->eax = ret; c->ecx = pb; c->edx = pa;
    c->esp += 4;
}

// 0x48c4e0: appends a quad (p0 p1 p2, p0 p2 p3: two triangles of vec3) to the game's polygon buffer, unless it is full.
// cdecl (p0, p1, p2, p3). Registers as the original leaves them (ebx, esi, edi restored).
#define POLY_PTR   0xb5445cu
#define POLY_COUNT 0xb544a0u
void F_0048c4e0_orig(CPU *c);
void nat_push_quad(CPU *c) {
    uint8_t *const mb = g_mem;
    NPC(NP_QUAD);
    if (!nat_on()) { F_0048c4e0_orig(c); return; }
    uint32_t sp = c->esp, n = R32(POLY_COUNT), p = R32(POLY_PTR);
    int32_t need = (int32_t)((n * 3 + 3) * 2);
    static const uint8_t order[6] = {0, 1, 2, 0, 2, 3};
    if (nat_check()) {
        uint8_t want[72];
        for (int i = 0; i < 6; i++) memcpy(want + 12 * i, mb + R32(sp + 4 + 4 * order[i]), 12);
        F_0048c4e0_orig(c);
        if (need > 0x8000) check_log("push_quad full", R32(POLY_COUNT), n);
        else {
            check_log("push_quad data", memcmp(want, mb + p, 72), 0);
            check_log("push_quad count", R32(POLY_COUNT), n + 1);
            check_log("push_quad ptr", R32(POLY_PTR), p + 72);
            check_log("push_quad edx", c->edx, p + 60);
        }
        return;
    }
    if (need > 0x8000) { c->eax = p; c->ecx = (uint32_t)need; c->esp += 4; return; }
    for (int i = 0; i < 6; i++) memmove(mb + p + 12 * i, mb + R32(sp + 4 + 4 * order[i]), 12);
    W32(POLY_PTR, p + 72);
    W32(POLY_COUNT, n + 1);
    c->eax = n + 1;
    c->ecx = R32(R32(sp + 16) + 8);
    c->edx = p + 60;
    c->esp += 4;
}

// 0x648432 D3DXVec3TransformNormal, the x87 version D3DX's CPU check puts in the table (slot 0x7257f0, reached through
// the thunk 0x62a89d: 2.3% of the Vita profile in the park): out = v * M (3x3 part); stdcall (out, v, m), returns out.
// Per component (v.x*m[0][k] + v.y*m[1][k]) + v.z*m[2][k], as the x87 code adds its three columns.
void F_00648432_orig(CPU *c);
void nat_d3dx_vec3transformnormal(CPU *c) {
    uint8_t *const mb = g_mem;
    NPC(NP_VTN);
    uint32_t out = R32(c->esp + 4), pv = R32(c->esp + 8), pm = R32(c->esp + 12);
    float v[4], o[4];
    memcpy(v, mb + pv, 12);
    k_vecmat(o, v, mb + pm, 3);
    if (nat_check()) {
        F_00648432_orig(c);
        for (int k = 0; k < 3; k++) { float g; memcpy(&g, mb + out + 4 * k, 4); check_near("vec3transformnormal", o[k], g); }
        memcpy(mb + out, o, 12);
        return;
    }
    if (!nat_on()) { F_00648432_orig(c); return; }
    memcpy(mb + out, o, 12);
    c->eax = out;
    c->esp += 16;
}

// 0x648491 D3DXVec4Transform, the x87 version in the table (slot 0x7257e0, thunk 0x62ade0): out = v * M (v has 4
// components); stdcall (out, v, m), returns out. Per component ((x*m[0][k] + y*m[1][k]) + z*m[2][k]) + w*m[3][k].
void F_00648491_orig(CPU *c);
void nat_d3dx_vec4transform(CPU *c) {
    uint8_t *const mb = g_mem;
    NPC(NP_V4);
    uint32_t out = R32(c->esp + 4), pv = R32(c->esp + 8), pm = R32(c->esp + 12);
    float v[4], o[4];
    memcpy(v, mb + pv, sizeof v);
    k_vecmat(o, v, mb + pm, 5);
    if (nat_check()) {
        F_00648491_orig(c);
        for (int k = 0; k < 4; k++) { float g; memcpy(&g, mb + out + 4 * k, 4); check_near("vec4transform", o[k], g); }
        memcpy(mb + out, o, sizeof o);
        return;
    }
    if (!nat_on()) { F_00648491_orig(c); return; }
    memcpy(mb + out, o, sizeof o);
    c->eax = out;
    c->esp += 16;
}

// 0x4f3300: does the segment p->q cross the quad A B C D (game collision, the 2nd function of the Vita profile in the
// park, 7%)? cdecl (out, p, q, A, B, C, D), vec3 floats. Returns 0, or 1 with out = {1 (int), hit point}. Steps of the
// original: box rejection per axis (segment min above the 4 corners, or max below them), plane n = (C-A) x (B-A),
// d = -n.A; p must be behind the plane or q in front... (both in front: no hit); the hit must be inside the 4 edges
// ((P1-P0) x (q-p)) . (p-P0) <= 0); hit = p + (q-p) * (-(n.p+d) / n.(q-p)), 0 if parallel. Same float stores, same
// double sums in the same order, same comparisons (NaN included) as the x87 code.
#define SEGQ_ZERO 0x68a868u
void F_004f3300_orig(CPU *c);
static inline void cross_f(float *o, const float *a, const float *b) {   // 0x60c030 (out, a, b): stores 3 floats
    o[0] = (float)((double)b[2] * a[1] - (double)a[2] * b[1]);
    o[1] = (float)((double)a[2] * b[0] - (double)b[2] * a[0]);
    o[2] = (float)((double)a[0] * b[1] - (double)b[0] * a[1]);
}
static uint32_t seg_quad(const float *p, const float *q, const float *const v[4], float *hit, double zero) {
    for (int i = 0; i < 3; i++) {
        double m = p[i] < q[i] ? p[i] : q[i];
        if (m > v[0][i] && m > v[1][i] && m > v[2][i] && m > v[3][i]) return 0;
    }
    for (int i = 0; i < 3; i++) {
        double m = p[i] > q[i] ? p[i] : q[i];
        if (m < v[0][i] && m < v[1][i] && m < v[2][i] && m < v[3][i]) return 0;
    }
    float e1[3], e2[3], n[3], dir[3];
    for (int i = 0; i < 3; i++) { e1[i] = (float)((double)v[1][i] - v[0][i]); e2[i] = (float)((double)v[2][i] - v[0][i]); }
    cross_f(n, e2, e1);
    float d = (float)-(((double)n[0] * v[0][0] + (double)n[1] * v[0][1]) + (double)n[2] * v[0][2]);
    double sp = (((double)n[1] * p[1] + (double)n[2] * p[2]) + (double)n[0] * p[0]) + d;
    if (sp >= zero) {
        double sq = (((double)n[1] * q[1] + (double)n[2] * q[2]) + (double)n[0] * q[0]) + d;
        if (sq >= zero) return 0;
    }
    for (int i = 0; i < 3; i++) dir[i] = (float)((double)q[i] - p[i]);
    for (int k = 0; k < 4; k++) {
        const float *a = v[k], *b = v[(k + 1) & 3];
        float e[3], w[3], o[3];
        for (int i = 0; i < 3; i++) { e[i] = (float)((double)b[i] - a[i]); w[i] = (float)((double)p[i] - a[i]); }
        cross_f(o, e, dir);
        double dot = ((double)o[2] * w[2] + (double)o[1] * w[1]) + (double)o[0] * w[0];
        if (dot > zero) return 0;
    }
    double num = -((((double)n[1] * p[1] + (double)n[2] * p[2]) + (double)n[0] * p[0]) + d);
    double den = ((double)dir[1] * n[1] + (double)dir[2] * n[2]) + (double)dir[0] * n[0];
    double t = den == zero ? zero : num / den;
    for (int i = 0; i < 3; i++) hit[i] = (float)((double)dir[i] * t + p[i]);
    return 1;
}
void nat_seg_quad(CPU *c) {
    uint8_t *const mb = g_mem;
    NPC(NP_SEGQUAD);
    {   // collision runs: the player is in the world (no "v1" label; dinput.c: circle cancels only outside it)
        extern int g_world_seen; extern unsigned g_world_frame, g_frame;
        g_world_seen = 1; g_world_frame = g_frame + 1;
    }
    if (!nat_on()) { F_004f3300_orig(c); return; }
    uint32_t sp = c->esp, out = R32(sp + 4);
    float p[3], q[3], vv[4][3], hit[3];
    memcpy(p, mb + R32(sp + 8), 12);
    memcpy(q, mb + R32(sp + 12), 12);
    for (int k = 0; k < 4; k++) memcpy(vv[k], mb + R32(sp + 16 + 4 * k), 12);
    const float *v[4] = {vv[0], vv[1], vv[2], vv[3]};
    uint32_t r = seg_quad(p, q, v, hit, rt_f32(R32(SEGQ_ZERO)));
    if (nat_check()) {
        F_004f3300_orig(c);
        check_log("seg_quad ret", r, c->eax);
        if (r && c->eax) for (int i = 0; i < 3; i++) { float g; memcpy(&g, mb + out + 4 + 4 * i, 4); check_log("seg_quad hit", hit[i], g); }
        return;
    }
    if (r) { W32(out, 1); memcpy(mb + out + 4, hit, 12); }
    c->eax = r;
    c->esp += 4;
}

// ---------- 0x48e140: shadow volume of one strip of the flashlight shadows ----------
// In the slowest park frames the shadow system took ~25% of the main thread, mostly in the glue between its parts:
// for every vertex the recompiled code went through D3DX thunks, the near clip and the quad push, each a full call with
// registers and x87 stack stored and reloaded. This is the same function written block by block (addresses in the
// labels): same stack slots (E = esp at entry), same argument orders, same comparisons (NaN included), same float
// stores; the maths parts are called directly, 0x48c5b0 (caps, 10 arguments) is still the game's code.
// Strip: int16 count at +0, 16-byte vertices from +0x10 (two int16 triples: A at +0, P at +8). Facing of a vertex:
// ((P-L).z*A.z + (P-L).y*A.y) + (P-L).x*A.x, L = light (world). A ring of 3 transformed points and their clipped
// copies feeds the quads of the silhouette (0x48c4e0) and the caps (0x48c5b0).
void F_0048e140_orig(CPU *c);
void F_0048c5b0(CPU *c);
static inline float gf(const uint8_t *mb, uint32_t a) { float f; memcpy(&f, mb + a, 4); return f; }
static inline void gwf(uint8_t *mb, uint32_t a, float f) { memcpy(mb + a, &f, 4); }
static void g_vt(uint8_t *mb, uint32_t out, uint32_t pv, uint32_t pm) {   // as nat_d3dx_vec3transform
    float v[4], o[4];
    memcpy(v, mb + pv, 12);
    k_vecmat(o, v, mb + pm, 4);
    memcpy(mb + out, o, sizeof o);
}
static void g_matmul(uint8_t *mb, uint32_t out, uint32_t pa, uint32_t pb) {   // as nat_d3dx_matmul
    float o[16];
    k_matmul(o, mb + pa, mb + pb);
    memcpy(mb + out, o, sizeof o);
}
static uint32_t g_clip(uint8_t *mb, uint32_t out, uint32_t pa, uint32_t pb) {   // as nat_clip_near (no scratch)
    float a[3], b[3], o[3];
    memcpy(a, mb + pa, sizeof a);
    memcpy(b, mb + pb, sizeof b);
    double n = (double)rt_f32(R32(CLIP_NEAR_Z)) * rt_f32(R32(CLIP_NEAR_K));
    float d0 = (float)((double)b[0] - a[0]), d1 = (float)((double)b[1] - a[1]);
    double d2 = (double)b[2] - a[2];
    float t = (float)(rt_f32(R32(CLIP_SIDE_K)) * d2);
    double zero = rt_f32(R32(CLIP_ZERO)), one = rt_f32(R32(CLIP_ONE));
    uint32_t ret;
    if (d2 < zero && b[2] > n && (fabs(d0) < t || fabs(d1) < t)) {
        double r = one / d2;
        o[0] = (float)(((n - a[2]) * b[0] + (b[2] - n) * a[0]) * r);
        o[1] = (float)(((b[2] - n) * a[1] + (n - a[2]) * b[1]) * r);
        o[2] = (float)n;
        ret = 1;
    } else {
        double l = sqrt(((double)d1 * d1 + (double)d0 * d0) + d2 * d2);
        double f = l == zero ? one : rt_f32(R32(CLIP_PUSH)) / l;
        o[0] = (float)(d0 * f + b[0]);
        o[1] = (float)(d1 * f + b[1]);
        o[2] = (float)(f * d2 + b[2]);
        ret = n < b[2] ? 0 : 2;
    }
    memcpy(mb + out, o, sizeof o);
    return ret;
}
static void g_quad(uint8_t *mb, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3) {   // as nat_push_quad
    uint32_t n = R32(POLY_COUNT), p = R32(POLY_PTR);
    if ((int32_t)((n * 3 + 3) * 2) > 0x8000) return;
    const uint32_t v[6] = {p0, p1, p2, p0, p2, p3};
    for (int i = 0; i < 6; i++) memmove(mb + p + 12 * i, mb + v[i], 12);
    W32(POLY_PTR, p + 72);
    W32(POLY_COUNT, n + 1);
}
#define SHV_SIDE    0xb544beu   // int8: initial side sign
#define SHV_TABLE   0x8e439cu   // caster table, 0x78 bytes per entry (world matrix at +0, light at +0x40)
#define SHV_DEVICE  0x72c610u   // IDirect3DDevice8*
#define CAP_PTR     0xb54460u   // cap polygon buffer of 0x48c5b0
#define CAP_COUNT   0xb544a8u
static void shadow_run(CPU *c) {
    uint8_t *const mb = g_mem;
    const uint32_t E = c->esp;
#define S(o) (E - (uint32_t)(o))
    const uint32_t sv_ebx = c->ebx, sv_esi = c->esi, sv_edi = c->edi, sv_ebp = c->ebp;
    const double zero = rt_f32(R32(CLIP_ZERO));
    uint32_t ebx, ecx, esi, edi, ebp, edx;
    W32(S(0x124), (uint32_t)-(int32_t)(int8_t)R8(SHV_SIDE));
    uint32_t entry = R32(E + 8) * 0x78 + R32(SHV_TABLE);
    ecx = R32(E + 4);
    edi = (uint32_t)((int32_t)(int16_t)R16(ecx) - 2);
    esi = entry + 0x40;
    ebx = ecx + 0x10;
    W32(S(0x134), entry);
    W32(S(0x120), R32(entry + 0x50));
    W32(S(0xec), ebx + 0x10);
    W32(S(0xe8), esi);
    ebp = 1;
    W32(S(0x11c), 0);
    W32(S(0x108), 1);
    {   // device->GetTransform(2 = view, S(0x40)): host function, stdcall
        uint32_t dev = R32(SHV_DEVICE), fn = R32(R32(dev) + 0x98), sp = E - 0x150;
        W32(sp, 0x48e1bcu); W32(sp + 4, dev); W32(sp + 8, 2); W32(sp + 12, S(0x40));
        c->esp = sp;
        rt_call(c, fn);
    }
    g_matmul(mb, S(0x80), R32(S(0x134)), S(0x40));      // world * view
    g_vt(mb, S(0x90), esi, S(0x80));                     // light in view space
    W32(S(0xf8), R32(S(0x90))); W32(S(0xf4), R32(S(0x8c))); W32(S(0xf0), R32(S(0x88)));
    if ((int32_t)edi <= 0) goto done;
    goto L220;
L217:
    edi = R32(S(0x114));
    esi = R32(S(0xe8));
L220:
    W32(S(0x12c), 0);
    ecx = ebx + 0xc;
L230:
    ebx += 0x10; ecx += 0x10; ebp++;
    W32(S(0x128), ebp);
    if ((int32_t)ebp > 2) { W32(S(0x128), 0); ebp = 0; }
    {   // 24c: P as floats into the ring slot, facing against the light
        double a0 = (int16_t)R16(ebx), a1 = (int16_t)R16(ecx - 0xa), a2 = (int16_t)R16(ecx - 8);
        uint32_t slot = S(0xc0) + 16 * ebp;
        gwf(mb, slot, (float)(int16_t)R16(ecx - 4));
        gwf(mb, slot + 4, (float)(int16_t)R16(ecx - 2));
        gwf(mb, slot + 8, (float)(int16_t)R16(ecx));
        edi--;
        double d0 = (double)gf(mb, slot) - gf(mb, esi), d1 = (double)gf(mb, slot + 4) - gf(mb, esi + 4);
        double d2 = (double)gf(mb, slot + 8) - gf(mb, esi + 8);
        W32(S(0x12c), R32(S(0x12c)) + 1);
        W32(S(0x124), (uint32_t)-(int32_t)R32(S(0x124)));
        gwf(mb, S(0x110), (float)((d2 * a2 + d1 * a1) + d0 * a0));
    }
    if (!((double)gf(mb, S(0x110)) < zero)) goto L303;
    if ((int32_t)edi <= 0) goto L303;
    if (R32(S(0x11c)) == 0) goto L230;
L303:
    W32(S(0x114), edi);
    esi = (int32_t)ebp > 1 ? ebp - 2 : ebp + 1;
    edi = (int32_t)ebp > 0 ? ebp - 1 : ebp + 2;
    {
        uint32_t slot = S(0xc0) + 16 * ebp, co = S(0xe4) + 12 * ebp;
        W32(S(0x118), slot);
        g_vt(mb, slot, slot, S(0x80));
        W32(S(0x10c), co);
        W32(S(0x104) + 4 * ebp, g_clip(mb, co, S(0xf8), R32(S(0x118))));
    }
    if (R32(S(0xec)) != ebx) goto L564;
    {   // 36f: first vertex of the strip: the previous vertex's A and P fill the two other slots
        uint32_t s = S(0xc0) + 16 * esi;
        gwf(mb, s, (float)(int16_t)R16(ebx - 0x10)); gwf(mb, s + 4, (float)(int16_t)R16(ebx - 0xe));
        gwf(mb, s + 8, (float)(int16_t)R16(ebx - 0xc));
        W32(S(0x130), s);
        g_vt(mb, s, s, S(0x80));
        W32(S(0x104) + 4 * esi, g_clip(mb, S(0xe4) + 12 * esi, S(0xf8), R32(S(0x130))));
        uint32_t d = S(0xc0) + 16 * edi;
        gwf(mb, d, (float)(int16_t)R16(ebx - 8)); gwf(mb, d + 4, (float)(int16_t)R16(ebx - 6));
        gwf(mb, d + 8, (float)(int16_t)R16(ebx - 4));
        W32(S(0x130), d);
        g_vt(mb, d, d, S(0x80));
        W32(S(0x104) + 4 * edi, g_clip(mb, S(0xe4) + 12 * edi, S(0xf8), R32(S(0x130))));
    }
L44b:
    if ((int32_t)R32(S(0x12c)) <= 1) goto L4e3;
L456:
    {
        uint32_t d = S(0xc0) + 16 * edi;
        W32(S(0x130), d);
        g_vt(mb, d, d, S(0x80));
        W32(S(0x104) + 4 * edi, g_clip(mb, S(0xe4) + 12 * edi, S(0xf8), R32(S(0x130))));
    }
    if ((int32_t)R32(S(0x12c)) <= 2) goto L4e3;
    {
        uint32_t s = S(0xc0) + 16 * esi;
        W32(S(0x130), s);
        g_vt(mb, s, s, S(0x80));
        W32(S(0x104) + 4 * esi, g_clip(mb, S(0xe4) + 12 * esi, S(0xf8), R32(S(0x130))));
    }
L4e3:
    if (!((double)gf(mb, S(0x110)) >= zero)) goto L74a;
    {   // 506 / 5ed: caps of the silhouette (0x48c5b0, cdecl, 10 arguments), argument order by side
        uint32_t slot_s = S(0xc0) + 16 * esi, slot_d = S(0xc0) + 16 * edi;
        uint32_t clip_s = S(0xe4) + 12 * esi, clip_d = S(0xe4) + 12 * edi, clip_cur = R32(S(0x10c));
        uint32_t flag_s = R32(S(0x104) + 4 * esi), flag_d = R32(S(0x104) + 4 * edi);
        uint32_t flag_cur = R32(S(0x104) + 4 * R32(S(0x128))), slot_cur = R32(S(0x118));
        int side = (int32_t)R32(S(0x124)) > 0;
        ebp = slot_s;
        W32(S(0x12c), clip_s);
        W32(S(0x130), slot_d);
        W32(S(0x134), clip_d);
        uint32_t a[10];
        if (side) {
            const uint32_t v[10] = {clip_d, clip_s, clip_cur, S(0xf8), flag_d, flag_s, flag_cur, slot_d, slot_s, slot_cur};
            memcpy(a, v, sizeof a);
        } else {
            const uint32_t v[10] = {clip_s, clip_d, clip_cur, S(0xf8), flag_s, flag_d, flag_cur, slot_s, slot_d, slot_cur};
            memcpy(a, v, sizeof a);
        }
        uint32_t sp = E - 0x16c - 4;
        W32(sp, 0x48e64fu);
        for (int i = 0; i < 10; i++) W32(sp + 4 + 4 * i, a[i]);
        c->esp = sp; c->ebx = ebx; c->esi = esi; c->edi = edi; c->ebp = ebp;
        F_0048c5b0(c);
    }
    if (!((double)gf(mb, S(0x110)) >= zero)) goto L746;
    edx = R32(S(0x118));
    if ((int32_t)R32(S(0x124)) <= 0) {
        g_quad(mb, edx, ebp, R32(S(0x12c)), R32(S(0x10c)));
        esi = R32(S(0x12c));
    } else {
        esi = R32(S(0x12c));
        g_quad(mb, ebp, edx, R32(S(0x10c)), esi);
    }
    if (R32(S(0x108)) == 0) goto L6e0;
    if ((int32_t)R32(S(0x124)) > 0) g_quad(mb, R32(S(0x130)), ebp, esi, R32(S(0x134)));
    else g_quad(mb, ebp, R32(S(0x130)), R32(S(0x134)), esi);
    W32(S(0x108), 0);
    W32(S(0x11c), 1);
L6e0:
    ebp = R32(S(0x128));
L6e4:
    if ((int32_t)R32(S(0x114)) > 0) goto L217;
    if (R32(S(0x11c)) == 0) goto done;
    if (!((double)gf(mb, S(0x110)) >= zero)) goto done;
    if ((int32_t)R32(S(0x124)) > 0) g_quad(mb, S(0xc0) + 16 * ebp, S(0xc0) + 16 * edi, S(0xe4) + 12 * edi, S(0xe4) + 12 * ebp);
    else g_quad(mb, S(0xc0) + 16 * edi, S(0xc0) + 16 * ebp, S(0xe4) + 12 * ebp, S(0xe4) + 12 * edi);
    goto done;
L746:
    ebp = R32(S(0x128));
L74a:
    if (R32(S(0x11c)) == 0) goto L6e4;
    if ((int32_t)R32(S(0x124)) > 0) g_quad(mb, S(0xc0) + 16 * esi, S(0xc0) + 16 * edi, S(0xe4) + 12 * edi, S(0xe4) + 12 * esi);
    else g_quad(mb, S(0xc0) + 16 * edi, S(0xc0) + 16 * esi, S(0xe4) + 12 * esi, S(0xe4) + 12 * edi);
    W32(S(0x11c), 0);
    W32(S(0x108), 1);
    goto L6e4;
L564:
    if (R32(S(0xec)) + 0x10 != ebx) goto L44b;
    if (R32(S(0x12c)) != 2) goto L44b;
    {   // 57a: second vertex: the previous vertex's P into the oldest slot
        uint32_t s = S(0xc0) + 16 * esi;
        gwf(mb, s, (float)(int16_t)R16(ebx - 0x18)); gwf(mb, s + 4, (float)(int16_t)R16(ebx - 0x16));
        gwf(mb, s + 8, (float)(int16_t)R16(ebx - 0x14));
        W32(S(0x130), s);
        g_vt(mb, s, s, S(0x80));
        W32(S(0x104) + 4 * esi, g_clip(mb, S(0xe4) + 12 * esi, S(0xf8), R32(S(0x130))));
    }
    goto L456;
done:
    c->ebx = sv_ebx; c->esi = sv_esi; c->edi = sv_edi; c->ebp = sv_ebp;
    c->esp = E + 4;
#undef S
}
void nat_shadow_volume(CPU *c) {
    uint8_t *const mb = g_mem;
    if (!nat_on()) { F_0048e140_orig(c); return; }
    if (!nat_check()) {
        uint64_t t0 = SDL_GetPerformanceCounter();
        shadow_run(c);
        np_calls[NP_SHADOW]++;
        np_ticks[NP_SHADOW] += SDL_GetPerformanceCounter() - t0;
        return;
    }
    // Check: both versions from the same state; the two polygon buffers they fill must match byte for byte.
    CPU saved = *c;
    uint32_t qp0 = R32(POLY_PTR), qn0 = R32(POLY_COUNT), cp0 = R32(CAP_PTR), cn0 = R32(CAP_COUNT);
    uint64_t tb0 = SDL_GetPerformanceCounter();
    shadow_run(c);
    uint64_t tb1 = SDL_GetPerformanceCounter();
    uint32_t qp1 = R32(POLY_PTR), qn1 = R32(POLY_COUNT), cp1 = R32(CAP_PTR), cn1 = R32(CAP_COUNT);
    uint32_t ql = qp1 - qp0 < (1u << 20) ? qp1 - qp0 : 0, cl = cp1 - cp0 < (1u << 20) ? cp1 - cp0 : 0;
    uint8_t *qb = malloc(ql + 1), *cb = malloc(cl + 1);
    if (qb) memcpy(qb, mb + qp0, ql);
    if (cb) memcpy(cb, mb + cp0, cl);
    *c = saved;
    W32(POLY_PTR, qp0); W32(POLY_COUNT, qn0); W32(CAP_PTR, cp0); W32(CAP_COUNT, cn0);
    uint64_t tb2 = SDL_GetPerformanceCounter();
    F_0048e140_orig(c);
    {   // timing of both versions (PC): native vs recompiled original
        static uint64_t tn, to; static unsigned calls;
        tn += tb1 - tb0; to += SDL_GetPerformanceCounter() - tb2;
        if (!(++calls % 20000)) rt_log("[natbench] shadow: %u calls, native %.3f us, original %.3f us per call\n", calls, 1e6 * tn / calls / SDL_GetPerformanceFrequency(), 1e6 * to / calls / SDL_GetPerformanceFrequency());
    }
    check_log("shadow quad count", qn1, R32(POLY_COUNT));
    check_log("shadow quad ptr", qp1, R32(POLY_PTR));
    check_log("shadow cap count", cn1, R32(CAP_COUNT));
    check_log("shadow cap ptr", cp1, R32(CAP_PTR));
    if (qb && R32(POLY_PTR) == qp1) check_log("shadow quad data", memcmp(qb, mb + qp0, ql), 0);
    if (cb && R32(CAP_PTR) == cp1) check_log("shadow cap data", memcmp(cb, mb + cp0, cl), 0);
    free(qb); free(cb);
}

// 0x667a7f CRT __getptd: GetLastError, TlsGetValue([0x7296b8]), SetLastError: three host calls just to read a TLS slot.
// Called by rand and others. A thread's first call (empty slot) runs the original, which creates the data.
#define G_PTD_TLS_INDEX 0x7296b8u
// ---------- 0x66405e CRT rand and 0x60bfa0 random float ----------
// rand: holdrand (per-thread data + 0x14) = holdrand * 214013 + 2531011, returns bits 16..30. 0x60bfa0 (a, b) draws a
// 31-bit integer from three rand calls and returns a * (((b + 1) * b) / (x * K + b) - b) on the x87 stack; for a < 0 it
// first draws three more and flips the sign of a (in its argument slot) when the first has bit 12 set.
void F_0066405e_orig(CPU *c);
void F_0060bfa0_orig(CPU *c);
static inline uint32_t ptd_of(CPU *c, const uint8_t *mb) { return R32(c->fs_base + 0xe10 + 4 * R32(G_PTD_TLS_INDEX)); }
static inline uint32_t crt_rand(uint8_t *mb, uint32_t ptd) {
    uint32_t h = R32(ptd + 0x14) * 0x343fdu + 0x269ec3u;
    W32(ptd + 0x14, h);
    return (h >> 16) & 0x7fff;
}
void nat_rand(CPU *c) {
    uint8_t *const mb = g_mem;
    NPC(NP_RAND);
    uint32_t p = ptd_of(c, mb);
    if (!p || !nat_on()) { F_0066405e_orig(c); return; }
    if (nat_check()) {
        uint32_t h0 = R32(p + 0x14), r = crt_rand(mb, p), h1 = R32(p + 0x14);
        W32(p + 0x14, h0);
        F_0066405e_orig(c);
        check_log("rand", r, c->eax);
        check_log("rand state", h1, R32(p + 0x14));
        return;
    }
    c->eax = crt_rand(mb, p);
    c->ecx = R32(p + 0x14);
    c->esp += 4;
}
#define PRAND_ZERO 0x68a868u
#define PRAND_ONE  0x68a754u
#define PRAND_K    0x690610u
static double prand_run(CPU *c, uint8_t *mb, uint32_t p) {
    uint32_t E = c->esp;
    float a = rt_f32(R32(E + 4)), b = rt_f32(R32(E + 8));
    if ((double)a < (double)rt_f32(R32(PRAND_ZERO))) {   // jp not taken: a < 0 (ordered)
        uint32_t s = crt_rand(mb, p);
        crt_rand(mb, p);
        crt_rand(mb, p);
        if (s & 0x1000) { a = -a; W32(E + 4, rt_u32f(a)); }
    }
    uint32_t r0 = crt_rand(mb, p), r1 = crt_rand(mb, p);
    float t = (float)(((double)b + rt_f32(R32(PRAND_ONE))) * (double)b);
    uint32_t r2 = crt_rand(mb, p) & 1;
    int32_t x = (int32_t)((((r2 << 15) | r1) << 15) | r0);
    c->eax = (uint32_t)x;          // as the original: eax = the integer, ecx = the last holdrand
    c->ecx = R32(p + 0x14);
    double d = (double)x * rt_f32(R32(PRAND_K)) + (double)b;
    return ((double)t / d - (double)b) * (double)a;
}
void nat_prand(CPU *c) {
    uint8_t *const mb = g_mem;
    NP(NP_PRAND);
    uint32_t p = ptd_of(c, mb);
    if (!p || !nat_on()) { F_0060bfa0_orig(c); return; }
    if (nat_check()) {
        uint32_t h0 = R32(p + 0x14), a0 = R32(c->esp + 4);
        double r = prand_run(c, mb, p);
        uint32_t h1 = R32(p + 0x14), a1 = R32(c->esp + 4);
        W32(p + 0x14, h0); W32(c->esp + 4, a0);
        F_0060bfa0_orig(c);
        check_log("prand", r, ST(0));
        check_log("prand state", h1, R32(p + 0x14));
        check_log("prand arg", a1, R32(c->esp));
        return;
    }
    double r = prand_run(c, mb, p);
    FPUSH(r);
    c->esp += 4;
}

// ---------- 0x48f040: strip shadow (flashlight shadows of ground strips; 262 calls and 4454 calls of
// natives per frame on the PC in the park) ----------
// Same function, block by block (labels = addresses), same guest stack slots (E = esp at entry) and the same global
// arrays: VA (0xb544c8, 12 bytes per point: transformed point), CA (0x8e2b90: its clipped copy), FL (0x8e2388: clip
// result). Basis from the light position P (entry + 0x40) and the up vector U ([0x8e43b4]): A = U x P, B = A x U, both
// normalized (0x62a5f0 = D3DXVec3Normalize); M = [A 0; U 0; B 0; 0 0 0 1] * world * view. Each strip vertex (int16
// triples) is transformed and clipped; for arg2 == 4 (mode [E-0x198] 1 or 2) a step in height larger than [0x69026c]
// gets three points at t = 0.25, 0.5, 0.75 between the two vertices. The caps (0x48c5b0, game code) start from the
// diagonal with the smaller sum of truncated height steps.
void F_0048f040_orig(CPU *c);
void F_0064856f_orig(CPU *c);
#define SS_VA   0xb544c8u
#define SS_CA   0x8e2b90u
#define SS_FL   0x8e2388u
#define SS_UP   0x8e43b4u
#define SS_MODE 0x8e43b0u
#define SS_SIGN 0xb544b4u
#define SS_STEP 0x69026cu
#define SS_DT   0x68f030u
static void g_norm(CPU *c, uint32_t sp, uint32_t v) {   // D3DXVec3Normalize(v, v) through its native (stdcall)
    uint8_t *const mb = g_mem;
    W32(sp, 0x48f1f9u); W32(sp + 4, v); W32(sp + 8, v);
    c->esp = sp;
    nat_d3dx_vec3normalize(c);
}
static inline int32_t ss_absftol(double d) { int32_t i = (int32_t)sh3_ftol(d); return i < 0 ? -i : i; }
static void strip_run(CPU *c) {
    uint8_t *const mb = g_mem;
    const uint32_t E = c->esp;
#define S(o) (E - (uint32_t)(o))
#define VA(i) (SS_VA + 12u * (uint32_t)(i))
#define CA(i) (SS_CA + 12u * (uint32_t)(i))
#define FL(i) (SS_FL + 4u * (uint32_t)(i))
#define SLOT(i) (S(0x174) + 16u * (uint32_t)(i))
#define COUT(i) (S(0x128) + 12u * (uint32_t)(i))
    const uint32_t sv_ebx = c->ebx, sv_esi = c->esi, sv_edi = c->edi, sv_ebp = c->ebp;
    const double zero = rt_f32(R32(CLIP_ZERO));
    uint32_t strip = R32(E + 4), entry = R32(E + 8) * 0x78 + R32(SHV_TABLE);
    int32_t esi = (int16_t)R16(strip);
    W32(S(0x1cc), entry);
    W32(S(0x194), R32(entry + 0x50));
    W32(S(0x1c4), R32(R32(SS_MODE) + 8));
    const uint32_t P = entry + 0x40;
    uint32_t ebp = strip + 0x10, ebx = 1, edi;
    W32(S(0x198), 0);
    if (esi <= 0) goto done;
    {   // device->GetTransform(view)
        uint32_t dev = R32(SHV_DEVICE), fn = R32(R32(dev) + 0x98), sp = E - 0x1e8 - 4;
        W32(sp, 0x48f0b2u); W32(sp + 4, dev); W32(sp + 8, 2); W32(sp + 12, S(0x40));
        c->esp = sp;
        rt_call(c, fn);
    }
    g_matmul(mb, S(0xc0), R32(S(0x1cc)), S(0x40));
    g_vt(mb, S(0xd0), P, S(0xc0));
    W32(S(0x154), R32(S(0xd0))); W32(S(0x150), R32(S(0xcc))); W32(S(0x14c), R32(S(0xc8)));
    {
        uint32_t up = R32(SS_UP);
        W32(S(0x1b4), R32(up)); W32(S(0x1b0), R32(up + 4)); W32(S(0x1ac), R32(up + 8));
        double ux = rt_f32(R32(S(0x1b4))), uy = rt_f32(R32(S(0x1b0))), uz = rt_f32(R32(S(0x1ac)));
        double p0 = rt_f32(R32(P)), p1 = rt_f32(R32(P + 4)), p2 = rt_f32(R32(P + 8));
        gwf(mb, S(0x1a8), (float)(uy * p2 - uz * p1));
        gwf(mb, S(0x1a4), (float)(uz * p0 - ux * p2));
        gwf(mb, S(0x1a0), (float)(ux * p1 - uy * p0));
        W32(S(0x13c), R32(S(0x1a8))); W32(S(0x138), R32(S(0x1a4))); W32(S(0x134), R32(S(0x1a0)));
        double ax = gf(mb, S(0x1a8)), ay = gf(mb, S(0x1a4)), az = gf(mb, S(0x1a0));
        gwf(mb, S(0x180), (float)(ay * uz - az * uy));
        gwf(mb, S(0x17c), (float)(az * ux - ax * uz));
        gwf(mb, S(0x178), (float)(ax * uy - ay * ux));
        W32(S(0x148), R32(S(0x180))); W32(S(0x144), R32(S(0x17c))); W32(S(0x140), R32(S(0x178)));
    }
    g_norm(c, E - 0x1e4, S(0x13c));
    g_norm(c, E - 0x1e4, S(0x148));
    W32(S(0x110), R32(S(0x13c))); W32(S(0x10c), R32(S(0x138))); W32(S(0x108), R32(S(0x134))); W32(S(0x104), 0);
    W32(S(0x100), R32(S(0x1b4))); W32(S(0xfc), R32(S(0x1b0))); W32(S(0xf8), R32(S(0x1ac))); W32(S(0xf4), 0);
    W32(S(0xf0), R32(S(0x148))); W32(S(0xec), R32(S(0x144))); W32(S(0xe8), R32(S(0x140))); W32(S(0xe4), 0);
    W32(S(0xe0), 0); W32(S(0xdc), 0); W32(S(0xd8), 0); W32(S(0xd4), 0x3f800000u);
    if (R32(E + 0xc) == 4) {
        if (R32(S(0x1c4)) != 0) {
            W32(S(0x198), ebx);
            if ((double)rt_f32(R32(SS_SIGN)) < zero || rt_f32(R32(SS_SIGN)) != rt_f32(R32(SS_SIGN))) W32(S(0x198), 2);
        } else W32(S(0x198), 2);
    }
    // f300: first vertex
    gwf(mb, S(0x164), (float)(int16_t)R16(ebp + 8));
    gwf(mb, S(0x160), (float)(int16_t)R16(ebp + 0xa));
    gwf(mb, S(0x15c), (float)(int16_t)R16(ebp + 0xc));
    ebp += 8;
    esi--;
    g_matmul(mb, S(0x80), S(0x110), S(0xc0));
    g_vt(mb, S(0x164), S(0x164), S(0x80));
    W32(S(0x1a8), R32(S(0x164))); W32(S(0x1a4), R32(S(0x160))); W32(S(0x1a0), R32(S(0x15c))); W32(S(0x19c), R32(S(0x158)));
    {
        uint32_t r = g_clip(mb, S(0x11c), S(0x154), S(0x164));
        W32(S(0x178), R32(S(0x114)));
        W32(VA(0), R32(S(0x164))); W32(VA(0) + 4, R32(S(0x160))); W32(VA(0) + 8, R32(S(0x15c)));
        W32(S(0x17c), R32(S(0x118)));
        W32(CA(0) + 4, R32(S(0x118)));
        W32(S(0x180), R32(S(0x11c)));
        W32(CA(0), R32(S(0x11c)));
        W32(CA(0) + 8, R32(S(0x114)));
        W32(FL(0), r);
    }
    edi = 1;
    W32(S(0x1c0), 1);
    if (esi <= 0) goto L6f1;
    W32(S(0x1cc), (uint32_t)esi);
    for (;;) {   // f437: one strip vertex per pass, ebx alternates 0 / 1
        ebp += 8;
        ebx++;
        W32(S(0x130), ebp);
        W32(S(0x188), ebx);
        if ((int32_t)ebx > 1) { W32(S(0x188), 0); ebx = 0; }
        uint32_t sb = SLOT(ebx);
        gwf(mb, sb, (float)(int16_t)R16(ebp));
        gwf(mb, sb + 4, (float)(int16_t)R16(ebp + 2));
        gwf(mb, sb + 8, (float)(int16_t)R16(ebp + 4));
        g_vt(mb, sb, sb, S(0x80));
        uint32_t cb = COUT(ebx);
        W32(S(0x18c), cb);
        uint32_t r = g_clip(mb, cb, S(0x154), sb);
        W32(S(0x1bc), r);
        W32(S(0x1b4) + 4 * ebx, r);
        uint32_t o = 1 - ebx;
        W32(S(0x190), o);
        int inter = 0;
        if (R32(S(0x198)) != 0) {
            double d = (double)gf(mb, SLOT(o) + 4) - gf(mb, sb + 4);
            int32_t i = ss_absftol(d);
            W32(S(0x1c4), (uint32_t)i);
            inter = (double)i > (double)rt_f32(R32(SS_STEP));
        }
        if (inter) {   // f524: three points between the other slot and this one
            uint32_t k = R32(S(0x1c0)), so = SLOT(o);
            float t = 0.25f;
            for (uint32_t e = k; (int32_t)e < (int32_t)(k + 3); e++) {
                double tt = t, om = 1.0 - tt;
                gwf(mb, VA(e), (float)(tt * gf(mb, sb) + om * gf(mb, so)));
                gwf(mb, VA(e) + 4, (float)(tt * gf(mb, sb + 4) + om * gf(mb, so + 4)));
                gwf(mb, VA(e) + 8, (float)(tt * gf(mb, sb + 8) + om * gf(mb, so + 8)));
                W32(FL(e), g_clip(mb, CA(e), S(0x154), VA(e)));
                if (e == k) g_quad(mb, VA(e), so, COUT(o), CA(e));
                else g_quad(mb, VA(e), VA(e - 1), CA(e - 1), CA(e));
                t = (float)((double)t + rt_f32(R32(SS_DT)));
            }
            uint32_t e = k + 3;
            g_quad(mb, sb, VA(e - 1), CA(e - 1), cb);
            W32(S(0x1c0), e);
            ebp = R32(S(0x130));
        } else {   // f675
            g_quad(mb, sb, SLOT(o), COUT(o), cb);
        }
        // f693
        uint32_t k = R32(S(0x1c0));
        W32(VA(k), R32(sb)); W32(VA(k) + 4, R32(sb + 4)); W32(VA(k) + 8, R32(sb + 8));
        W32(CA(k), R32(cb)); W32(CA(k) + 4, R32(cb + 4)); W32(CA(k) + 8, R32(cb + 8));
        W32(FL(k), R32(S(0x1bc)));
        W32(S(0x1c0), k + 1);
        uint32_t cnt = R32(S(0x1cc)) - 1;
        W32(S(0x1cc), cnt);
        if (!cnt) { edi = k + 1; break; }
    }
L6f1:
    {
        int closing = 0;
        if (R32(S(0x198)) != 0) {
            uint32_t o = R32(S(0x190)) & 1;   // set by the loop (0 or 1); never read uninitialized for strips of 2+
            double d = (double)gf(mb, S(0x170) + 16 * o) - gf(mb, S(0x170) + 16 * ebx);
            int32_t i = ss_absftol(d);
            W32(S(0x1bc), (uint32_t)i);
            closing = (double)i > (double)rt_f32(R32(SS_STEP));
        }
        uint32_t sb = SLOT(ebx);
        if (closing) {   // f73c: three points between this slot and the first vertex
            uint32_t k = R32(S(0x1c0));
            float t = 0.25f;
            for (; (int32_t)edi < (int32_t)(k + 3); edi++) {   // edi starts at k (the vertex count)
                double tt = t, om = 1.0 - tt;
                gwf(mb, VA(edi), (float)(om * gf(mb, sb) + (double)gf(mb, S(0x1a8)) * tt));
                gwf(mb, VA(edi) + 4, (float)(om * gf(mb, sb + 4) + (double)gf(mb, S(0x1a4)) * tt));
                gwf(mb, VA(edi) + 8, (float)(om * gf(mb, sb + 8) + (double)gf(mb, S(0x1a0)) * tt));
                W32(FL(edi), g_clip(mb, CA(edi), S(0x154), VA(edi)));
                if (edi == k) g_quad(mb, VA(edi), sb, COUT(ebx), CA(edi));
                else g_quad(mb, VA(edi), VA(edi - 1), CA(edi - 1), CA(edi));
                t = (float)((double)t + rt_f32(R32(SS_DT)));
            }
            g_quad(mb, S(0x1a8), VA(edi - 1), CA(edi - 1), S(0x180));
            W32(S(0x1c0), edi);
        } else {   // f869
            g_quad(mb, S(0x1a8), sb, COUT(ebx), S(0x180));
        }
    }
    {   // f88b: starting diagonal of the caps
        int32_t last = (int32_t)edi - 1, half = (int32_t)edi / 2;
        W32(S(0x1b8), (uint32_t)last);
        double acc0 = zero, acc1 = zero;
        int32_t a = 0, b = last;
        for (int32_t n = half; n > 0; n--) {
            int32_t i = ss_absftol((double)gf(mb, CA(a) + 4) - gf(mb, CA(b) + 4));
            W32(S(0x1bc), (uint32_t)i);
            acc0 = acc0 + (double)i;
            if (++a > last) a = 0;
            if (--b < 0) b = last;
        }
        if (half > 0) W32(S(0x1cc), 0);
        a = 1; b = 0;
        for (int32_t n = half; n > 0; n--) {
            int32_t i = ss_absftol((double)gf(mb, CA(a) + 4) - gf(mb, CA(b) + 4));
            W32(S(0x1bc), (uint32_t)i);
            acc1 = acc1 + (double)i;
            if (++a > last) a = 0;
            if (--b < 0) b = last;
        }
        int32_t s, p;   // esi, ebp
        if (acc0 < acc1) { s = last; p = 0; } else { s = 0; p = 1; }
        int32_t n2 = (int32_t)R32(S(0x1c0)) - 2;
        W32(S(0x1cc), 0);
        W32(S(0x1bc), (uint32_t)n2);
        for (int32_t i = 0; i < n2; i++) {
            uint32_t args[10], ret;
            if (!(i & 1)) {
                int32_t q = p + 1;
                if (q > last) q = 0;
                const uint32_t v[10] = {CA(p), CA(s), CA(q), S(0x154), R32(FL(p)), R32(FL(s)), R32(FL(q)), VA(p), VA(s), VA(q)};
                memcpy(args, v, sizeof args);
                ret = 0x48f9f0u;
                s--; p = q;
                if (s < 0) s = last;
            } else {
                int32_t q = s + 1;
                if (q > last) q = 0;
                const uint32_t v[10] = {CA(s), CA(p), CA(q), S(0x154), R32(FL(s)), R32(FL(p)), R32(FL(q)), VA(s), VA(p), VA(q)};
                memcpy(args, v, sizeof args);
                ret = 0x48fa6bu;
            }
            uint32_t sp = E - 0x204 - 4;
            W32(sp, ret);
            for (int k = 0; k < 10; k++) W32(sp + 4 + 4 * k, args[k]);
            c->esp = sp;
            F_0048c5b0(c);
            W32(S(0x1cc), (uint32_t)(i + 1));
        }
    }
done:
    c->ebx = sv_ebx; c->esi = sv_esi; c->edi = sv_edi; c->ebp = sv_ebp;
    c->esp = E + 4;
#undef S
#undef VA
#undef CA
#undef FL
#undef SLOT
#undef COUT
}
void nat_shadow_strip(CPU *c) {
    uint8_t *const mb = g_mem;
    if (!nat_on()) { F_0048f040_orig(c); return; }
    if (!nat_check()) {
        uint64_t t0 = SDL_GetPerformanceCounter();
        strip_run(c);
        np_calls[NP_STRIP]++;
        np_ticks[NP_STRIP] += SDL_GetPerformanceCounter() - t0;
        return;
    }
    // Check: both versions from the same state; polygon buffers, cap buffers and the three point arrays must match.
    CPU saved = *c;
    uint32_t qp0 = R32(POLY_PTR), qn0 = R32(POLY_COUNT), cp0 = R32(CAP_PTR), cn0 = R32(CAP_COUNT);
    strip_run(c);
    uint32_t qp1 = R32(POLY_PTR), qn1 = R32(POLY_COUNT), cp1 = R32(CAP_PTR), cn1 = R32(CAP_COUNT);
    uint32_t ql = qp1 - qp0 < (1u << 20) ? qp1 - qp0 : 0, cl = cp1 - cp0 < (1u << 20) ? cp1 - cp0 : 0;
    uint8_t *qb = malloc(ql + 1), *cb = malloc(cl + 1), arr[3][12 * 64];
    if (qb) memcpy(qb, mb + qp0, ql);
    if (cb) memcpy(cb, mb + cp0, cl);
    memcpy(arr[0], mb + SS_VA, sizeof arr[0]); memcpy(arr[1], mb + SS_CA, sizeof arr[1]); memcpy(arr[2], mb + SS_FL, 4 * 64);
    *c = saved;
    W32(POLY_PTR, qp0); W32(POLY_COUNT, qn0); W32(CAP_PTR, cp0); W32(CAP_COUNT, cn0);
    F_0048f040_orig(c);
    check_log("strip quad count", qn1, R32(POLY_COUNT));
    check_log("strip cap count", cn1, R32(CAP_COUNT));
    if (qb && R32(POLY_PTR) == qp1) check_log("strip quad data", memcmp(qb, mb + qp0, ql), 0);
    if (cb && R32(CAP_PTR) == cp1) check_log("strip cap data", memcmp(cb, mb + cp0, cl), 0);
    check_log("strip points", memcmp(arr[0], mb + SS_VA, sizeof arr[0]), 0);
    check_log("strip clipped", memcmp(arr[1], mb + SS_CA, sizeof arr[1]), 0);
    check_log("strip flags", memcmp(arr[2], mb + SS_FL, 4 * 64), 0);
    free(qb); free(cb);
}

// ---------- 0x663a30 CRT qsort ----------
// Same algorithm as this exe's code, step for step, so the order of equal elements is the same: ranges of 8 or fewer
// elements by selection of the maximum (0x6639c0), otherwise median of lo/mid/hi, partition around mid, the smaller
// side pushed on a 30-entry stack. The comparisons the game passes are done in C when they are known (int, int16,
// float fields, ascending or descending, as their code); any other comparison function is called as guest code. The
// swaps are host memory copies instead of the byte loop.
void F_00663a30_orig(CPU *c);
typedef struct { CPU *c; uint8_t *mb; uint32_t cmp, sp; } QsCtx;
static int32_t qs_cmp(QsCtx *q, uint32_t a, uint32_t b) {
    uint8_t *const mb = q->mb;
    switch (q->cmp) {
    case 0x5f49b0u: return (int32_t)(R32(b) - R32(a));
    case 0x591220u: return (int32_t)(int16_t)R16(a) - (int32_t)(int16_t)R16(b);
    case 0x600700u: return (int32_t)(R32(R32(a) + 8) - R32(R32(b) + 8));
    case 0x5e2290u: {
        uint32_t r = R32(a + 8) - R32(b + 8);
        if (!r) r = R32(a + 4) - R32(b + 4);
        if (!r) r = R32(a) - R32(b);
        return (int32_t)r;
    }
    case 0x4399a0u: { float x = rt_f32(R32(a + 4)), y = rt_f32(R32(b + 4)); return x < y ? -1 : x > y ? 1 : 0; }
    case 0x462240u: { float x = rt_f32(R32(a + 8)), y = rt_f32(R32(b + 8)); return x < y ? 1 : x > y ? -1 : 0; }
    default: {   // guest comparison function, cdecl (a, b)
        CPU *c = q->c;
        uint32_t sp = q->sp;
        W32(sp, 0x663acdu); W32(sp + 4, a); W32(sp + 8, b);
        c->esp = sp;
        rt_call(c, q->cmp);
        return (int32_t)c->eax;
    }
    }
}
static void qs_swap(uint8_t *mb, uint32_t a, uint32_t b, uint32_t w) {
    uint8_t t[64];
    while (w) {
        uint32_t n = w < sizeof t ? w : (uint32_t)sizeof t;
        memcpy(t, mb + a, n); memmove(mb + a, mb + b, n); memcpy(mb + b, t, n);
        a += n; b += n; w -= n;
    }
}
static void qs_run(QsCtx *q, uint32_t base, uint32_t num, uint32_t w) {
    uint8_t *const mb = q->mb;
    if (num < 2 || !w) return;
    uint32_t lostk[30], histk[30];
    int stkptr = 0;
    uint32_t lo = base, hi = base + (num - 1) * w;
    for (;;) {
        uint32_t size = (hi - lo) / w + 1;
        if (size <= 8) {   // 0x6639c0
            uint32_t h = hi;
            while (h > lo) {
                uint32_t max = lo;
                for (uint32_t p = lo + w; p <= h; p += w)
                    if (qs_cmp(q, p, max) > 0) max = p;
                if (max != h) qs_swap(mb, max, h, w);
                h -= w;
            }
        } else {
            uint32_t mid = lo + (size >> 1) * w;
            if (qs_cmp(q, lo, mid) > 0 && lo != mid) qs_swap(mb, lo, mid, w);
            if (qs_cmp(q, lo, hi) > 0 && lo != hi) qs_swap(mb, lo, hi, w);
            if (qs_cmp(q, mid, hi) > 0 && mid != hi) qs_swap(mb, mid, hi, w);
            uint32_t loguy = lo, higuy = hi;
            for (;;) {
                if (mid > loguy) {
                    do loguy += w; while (loguy < mid && qs_cmp(q, loguy, mid) <= 0);
                }
                if (mid <= loguy) {
                    do loguy += w; while (loguy <= hi && qs_cmp(q, loguy, mid) <= 0);
                }
                do higuy -= w; while (higuy > mid && qs_cmp(q, higuy, mid) > 0);
                if (higuy < loguy) break;
                if (loguy != higuy) qs_swap(mb, loguy, higuy, w);
                if (mid == higuy) mid = loguy;
            }
            higuy += w;
            if (mid < higuy) {
                do higuy -= w; while (higuy > mid && qs_cmp(q, higuy, mid) == 0);
            }
            if (mid >= higuy) {
                do higuy -= w; while (higuy > lo && qs_cmp(q, higuy, mid) == 0);
            }
            if ((int32_t)(higuy - lo) >= (int32_t)(hi - loguy)) {
                if (lo < higuy) { lostk[stkptr] = lo; histk[stkptr] = higuy; stkptr++; }
                if (loguy < hi) { lo = loguy; continue; }
            } else {
                if (loguy < hi) { lostk[stkptr] = loguy; histk[stkptr] = hi; stkptr++; }
                if (lo < higuy) { hi = higuy; continue; }
            }
        }
        if (--stkptr < 0) return;
        lo = lostk[stkptr]; hi = histk[stkptr];
    }
}
void nat_qsort(CPU *c) {
    uint8_t *const mb = g_mem;
    if (!nat_on()) { F_00663a30_orig(c); return; }
    uint32_t E = c->esp, base = R32(E + 4), num = R32(E + 8), w = R32(E + 12);
    QsCtx q = {c, mb, R32(E + 16), E - 0x200};
    const uint32_t sv_ebx = c->ebx, sv_esi = c->esi, sv_edi = c->edi, sv_ebp = c->ebp;
    uint64_t t0 = SDL_GetPerformanceCounter();
    if (nat_check() && num < 4096 && w <= 256) {
        uint32_t n = num * w;
        uint8_t *before = malloc(n + 1), *mine = malloc(n + 1);
        if (before && mine) {
            memcpy(before, mb + base, n);
            qs_run(&q, base, num, w);
            memcpy(mine, mb + base, n);
            memcpy(mb + base, before, n);
            c->esp = E; c->ebx = sv_ebx; c->esi = sv_esi; c->edi = sv_edi; c->ebp = sv_ebp;
            F_00663a30_orig(c);
            check_log("qsort data", memcmp(mine, mb + base, n), 0);
            free(before); free(mine);
            return;
        }
        free(before); free(mine);
    }
    qs_run(&q, base, num, w);
    np_calls[NP_QSORT]++;
    np_ticks[NP_QSORT] += SDL_GetPerformanceCounter() - t0;
    c->ebx = sv_ebx; c->esi = sv_esi; c->edi = sv_edi; c->ebp = sv_ebp;
    c->esp = E + 4;
}

// ---------- 0x436430: 8-bit paletted picture into a 32-bit texture ----------
// eax = picture object (texture at +0x58, indices at +8, palette at +0xc); cdecl (width, height). LockRect level 0 on
// {0, 0, width, height}, then per pixel: index with bits 3 and 4 swapped (PS2 palette layout), colour with red and blue
// swapped; rows advance by the pitch, the indices are contiguous; UnlockRect. Same guest stack slots as the original.
void F_00436430_orig(CPU *c);
static uint32_t com_call(CPU *c, uint32_t sp, uint32_t self, uint32_t slot, int nargs, const uint32_t *args) {
    uint8_t *const mb = g_mem;
    W32(sp, 0x436470u);
    W32(sp + 4, self);
    for (int i = 0; i < nargs; i++) W32(sp + 8 + 4 * i, args[i]);
    c->esp = sp;
    rt_call(c, R32(R32(self) + slot));
    return c->eax;
}
static void pal_run(CPU *c);
void nat_pal_texture(CPU *c) {
    uint8_t *const mb = g_mem;
    if (!nat_on()) { F_00436430_orig(c); return; }
    if (!nat_check()) { pal_run(c); return; }
    // Check: the native fills the locked rows; the original then fills them again and must write the same bytes.
    CPU saved = *c;
    uint32_t E = c->esp, w = R32(E + 4), h = R32(E + 8);
    pal_run(c);
    uint32_t pitch = R32(E - 0x18), row0 = R32(E - 0x14), n = (w && h && w <= 4096 && h <= 4096) ? w * 4 : 0;
    uint8_t *mine = n ? malloc((size_t)n * h) : NULL;
    if (mine) for (uint32_t y = 0; y < h; y++) memcpy(mine + (size_t)y * n, mb + row0 + y * pitch, n);
    *c = saved;
    F_00436430_orig(c);
    if (mine && R32(E - 0x14) == row0) {
        int diff = 0;
        for (uint32_t y = 0; y < h && !diff; y++) diff = memcmp(mine + (size_t)y * n, mb + row0 + y * pitch, n) != 0;
        check_log("pal_texture data", diff, 0);
    }
    free(mine);
}
static void pal_run(CPU *c) {
    uint8_t *const mb = g_mem;
    const uint32_t E = c->esp, obj = c->eax;
    const uint32_t sv_ebx = c->ebx, sv_esi = c->esi, sv_edi = c->edi, sv_ebp = c->ebp;
    uint32_t w = R32(E + 4), h = R32(E + 8), tex = R32(obj + 0x58), src = R32(obj + 8), pal = R32(obj + 0xc);
    W32(E - 4, h); W32(E - 0x10, 0); W32(E - 0xc, 0); W32(E - 8, w);   // RECT {0, 0, w, h}
    const uint32_t la[4] = {0, E - 0x18, E - 0x10, 0};
    uint32_t hr = com_call(c, E - 0x4c, tex, 0x40, 4, la);
    if ((int32_t)hr >= 0) {
        uint32_t pitch = R32(E - 0x18), row = R32(E - 0x14);
        for (int32_t y = 0; y < (int32_t)h; y++, row += pitch) {
            if ((int32_t)w <= 0) continue;
            const uint8_t *s = mb + src;
            uint8_t *d = mb + row;
            for (uint32_t x = 0; x < w; x++) {
                uint32_t i = s[x], k = ((i >> 1) & 8) | ((i & 8) << 1) | (i & 0xe7);
                uint32_t col = R32(pal + 4 * k);
                uint32_t o = ((col >> 16) & 0xff) | ((col & 0xff) << 16) | (col & 0xff00ff00u);
                memcpy(d + 4 * x, &o, 4);
            }
            src += w;
        }
        const uint32_t ua[1] = {0};
        com_call(c, E - 0x3c, tex, 0x44, 1, ua);
    }
    c->ebx = sv_ebx; c->esi = sv_esi; c->edi = sv_edi; c->ebp = sv_ebp;
    c->esp = E + 4;
}

// ---------- 0x478150: add or update an entry of the 0x2c-byte list at 0x7142140 ----------
// cdecl (id, src (4 dwords), value): looks for the entry with +0x28 == id among the int16 count at 0x7143f78; if none,
// the next one (count + 1). Fills +0..0xc from src, then +0xc = value, +0x10..0x18 = 0, +0x1c = [0x7143f68],
// +0x20 = 0, +0x24 = 1.0 / [0x7143f68] (float), +0x28 = id; returns the entry in eax.
void F_00478150_orig(CPU *c);
#define FXL_BASE  0x7142140u
#define FXL_COUNT 0x7143f78u
#define FXL_TIME  0x7143f68u
void nat_fx_list_add(CPU *c) {
    uint8_t *const mb = g_mem;
    if (!nat_on()) { F_00478150_orig(c); return; }
    uint32_t E = c->esp, id = R32(E + 4), src = R32(E + 8), val = R32(E + 12);
    int32_t n = (int16_t)R16(FXL_COUNT);
    uint32_t e = 0;
    for (int32_t i = 0; i < n; i++)
        if (R32(FXL_BASE + 0x2c * (uint32_t)i + 0x28) == id) { e = FXL_BASE + 0x2c * (uint32_t)i; break; }
    if (!e) { e = (uint32_t)n * 0x2c + FXL_BASE; W16(FXL_COUNT, (uint16_t)(R16(FXL_COUNT) + 1)); }
    uint32_t s0 = R32(src), s1 = R32(src + 4), s2 = R32(src + 8);
    float t = rt_f32(R32(FXL_TIME));
    if (nat_check()) {
        CPU saved = *c;
        uint8_t mine[0x2c];
        uint16_t cnt = R16(FXL_COUNT);
        W32(e, s0); W32(e + 4, s1); W32(e + 8, s2); W32(e + 0xc, val);
        W32(e + 0x10, 0); W32(e + 0x14, 0); W32(e + 0x18, 0); W32(e + 0x1c, R32(FXL_TIME));
        W32(e + 0x20, 0); W32(e + 0x24, rt_u32f((float)(1.0 / (double)t))); W32(e + 0x28, id);
        memcpy(mine, mb + e, sizeof mine);
        if (cnt != (uint16_t)(int16_t)n) W16(FXL_COUNT, (uint16_t)n);   // the original counts again
        *c = saved;
        F_00478150_orig(c);
        check_log("fx_list entry", e, c->eax);
        check_log("fx_list count", cnt, R16(FXL_COUNT));
        check_log("fx_list data", memcmp(mine, mb + e, sizeof mine), 0);
        return;
    }
    W32(e, s0); W32(e + 4, s1); W32(e + 8, s2); W32(e + 0xc, val);
    W32(e + 0x10, 0); W32(e + 0x14, 0); W32(e + 0x18, 0); W32(e + 0x1c, R32(FXL_TIME));
    W32(e + 0x20, 0); W32(e + 0x24, rt_u32f((float)(1.0 / (double)t))); W32(e + 0x28, id);
    c->eax = e;
    c->ecx = R32(FXL_TIME);
    c->edx = val;
    c->esp = E + 4;
}

// ---------- 0x475fb0: per-particle fade ----------
// cdecl (p). s = (float)(int16)[0x7143f7e], scaled by the particle height p+4 between B = [0x7143f34] and A =
// [0x7143f30] (0 below B), replaced by [0x6b8260] above C = [0x7143f38]. Then with r = random(a = [0x6b8268], 0.02):
// p+0x54 = (r + p+0x54 - (p+0x50 - s) * K1) * K2, p+0x50 += that (double sum, as the x87 keeps it), clamped to
// [0, [0x6b8260]]. Same double intermediates and comparisons (NaN included) as the recompiled x87 code.
void F_00475fb0_orig(CPU *c);
#define PF_COUNT 0x7143f7eu
#define PF_A     0x7143f30u
#define PF_B     0x7143f34u
#define PF_C     0x7143f38u
#define PF_MAX   0x6b8260u
#define PF_RA    0x6b8268u
#define PF_K1    0x68ee94u
#define PF_K2    0x68eff0u
static void pfade_run(CPU *c, uint8_t *mb, uint32_t ptd) {
    const uint32_t E = c->esp, p = R32(E + 4);
    float s = (float)(double)(int32_t)(int16_t)R16(PF_COUNT);
    double y = rt_f32(R32(p + 4));
    if (y < (double)rt_f32(R32(PF_A))) {
        double b = rt_f32(R32(PF_B));
        if (y < b) s = 0.0f;
        else s = (float)(((y - b) / ((double)rt_f32(R32(PF_A)) - b)) * (double)s);
    }
    uint32_t sbits = rt_u32f(s);
    if (y > (double)rt_f32(R32(PF_C))) sbits = R32(PF_MAX);
    W32(E - 4, sbits);
    s = rt_f32(sbits);
    const uint32_t sp = E - 0x14;
    W32(sp, 0x47603du); W32(sp + 4, R32(PF_RA)); W32(sp + 8, 0x3ca3d70au);
    c->esp = sp;
    double r = prand_run(c, mb, ptd);
    c->esp = E;
    double t = r + (double)rt_f32(R32(p + 0x54));
    t = t - ((double)rt_f32(R32(p + 0x50)) - (double)s) * (double)rt_f32(R32(PF_K1));
    t = t * (double)rt_f32(R32(PF_K2));
    W32(p + 0x54, rt_u32f((float)t));
    t = t + (double)rt_f32(R32(p + 0x50));
    W32(p + 0x50, rt_u32f((float)t));
    if (t < (double)rt_f32(R32(CLIP_ZERO))) W32(p + 0x50, 0);
    if ((double)rt_f32(R32(p + 0x50)) > (double)rt_f32(R32(PF_MAX))) W32(p + 0x50, R32(PF_MAX));
    c->ecx = sbits;
}
void nat_particle_fade(CPU *c) {
    uint8_t *const mb = g_mem;
    uint32_t ptd = ptd_of(c, mb);
    if (!ptd || !nat_on()) { F_00475fb0_orig(c); return; }
    if (nat_check()) {
        CPU saved = *c;
        uint32_t p = R32(c->esp + 4), h0 = R32(ptd + 0x14), v50 = R32(p + 0x50), v54 = R32(p + 0x54);
        pfade_run(c, mb, ptd);
        uint32_t m50 = R32(p + 0x50), m54 = R32(p + 0x54), mh = R32(ptd + 0x14);
        W32(p + 0x50, v50); W32(p + 0x54, v54); W32(ptd + 0x14, h0);
        *c = saved;
        F_00475fb0_orig(c);
        check_log("particle_fade 50", m50, R32(p + 0x50));
        check_log("particle_fade 54", m54, R32(p + 0x54));
        check_log("particle_fade rand", mh, R32(ptd + 0x14));
        return;
    }
    pfade_run(c, mb, ptd);
    c->esp += 4;
}

void nat_getptd(CPU *c) {
    uint8_t *const mb = g_mem;
    uint32_t p = R32(c->fs_base + 0xe10 + 4 * R32(G_PTD_TLS_INDEX));
    if (nat_check()) {
        F_00667a7f_orig(c);
        if (p) check_log("getptd", p, c->eax);
        return;
    }
    if (!p || !nat_on()) { F_00667a7f_orig(c); return; }
    c->eax = p;
    c->esp += 4;
}
