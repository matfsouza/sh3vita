// The port's own fixes to sh3.exe behaviour that are not about the screen layout (those are in widescreen.c).
// Installed by recomp/image_patches.py.
#include "rt.h"
#include "win32/win32.h"
#include <stdlib.h>
#include <SDL.h>

// 0x5f09f0: reads savedata/disp.ini (graphics options) and empties it while the game runs; an empty file at start
// means the last run did not exit cleanly, and the game then shows "A crash occurred..." and drops to 640x480 safe
// options. The port has a single display mode and the Vita closes apps without notice, so the check never fires
// (PC Fix DisableSafeMode). The options are still read.
void F_005f09f0_orig(CPU *c);
void fix_no_safe_mode(CPU *c) {
    F_005f09f0_orig(c);
    c->eax = 0;
}

// 0x41b2c0 frame pacing: waits [0x72c7e8] vblanks of 1/refresh s ([0x72c790], 60 if 0) after the previous target
// ([0x72c808], QPC units of [0x72c818] per second; [0x72c810] remainder of the period division). A frame that took
// longer still waited for the next vblank boundary: up to 16 ms lost per frame on the Vita, which already waits for
// its own vblank. When the
// frame is late, the previous target moves back so that the new one lands 1 ms from now (the loop always waits at
// least once, for the first vblank that is still ahead): a 1 ms wait instead of up to 16.7; frames on time keep the
// game's pacing.
#define G_VBLANKS   0x72c7e8u
#define G_REFRESH   0x72c790u
#define G_TARGET    0x72c808u
#define G_REMAINDER 0x72c810u
#define G_QPF       0x72c818u
// NOT TO BE ENABLED: the limiter also writes how many vblanks passed ([0x72c7f4], total [0x72c7f0]) and
// the game advances animation and cutscenes by that count. Moving the target back made a 100 ms frame count as ~1
// vblank: slow motion, with the streamed audio running ahead. Kept only behind SH3_FRAME_PACING for the record.
//
// SH3_FPS30=1: 2 vblanks per frame (30 FPS, the PS2 rate) instead of the PC's 1 (60 FPS). The game's own setting:
// it then counts 2 vblanks per frame and keeps real-time speed.
#define G_ELAPSED   0x72c7f4u
#define G_TOTAL     0x72c7f0u
#define G_WAITS     0x72c7f8u
#define G_QPC_MODE  0x72c820u   // byte: the QueryPerformanceCounter branch of the limiter (else timeGetTime)
void F_0041b2c0_orig(CPU *c);
// The limiter of 0x41b2c0 (QPC branch) with one change: a frame that is already late no longer
// waits for the next vblank boundary. The original steps the target one period at a time ((qpf + remainder) / refresh,
// exact remainder kept), waits for each step still ahead, and stops at the first step ahead once it has counted
// [0x72c7e8] steps: a 36 ms frame at 2 vblanks per frame waited until 50 ms (20 FPS exactly). Here, once the count is
// reached, a late frame stops before the first step ahead: it reports only the periods that really passed and the
// target stays on the last boundary, so the fraction counts in the next frame. Total vblanks = real time (no slow
// motion, unlike SH3_FRAME_PACING), frames on time are paced exactly as before. SH3_LATE_NOWAIT=0: the original.
extern uint64_t g_t_sleep;
static int limiter_native(CPU *c) {
    uint8_t *const mb = g_mem;
    uint32_t n = R32(G_VBLANKS), hz = R32(G_REFRESH) ? R32(G_REFRESH) : 60;
    if (!n || !R8(G_QPC_MODE)) return 0;
    if (n > hz) n = hz;
    uint64_t qpf = (uint64_t)R32(G_QPF) | (uint64_t)R32(G_QPF + 4) << 32;
    uint64_t target = (uint64_t)R32(G_TARGET) | (uint64_t)R32(G_TARGET + 4) << 32;
    uint64_t rem = (uint64_t)R32(G_REMAINDER) | (uint64_t)R32(G_REMAINDER + 4) << 32;
    if (!qpf) return 0;
    uint64_t now = w32_qpc(), start = now;   // start: time of entry (the original restarts if this call lasts over 1 s)
    uint32_t i = 0, waits = 0;
    for (;;) {
        if (i >= n && waits) break;
        if (now - start > qpf && now > start) { target = now; rem = 0; break; }   // more than 1 s: restart, the steps so far counted (as the original)
        uint64_t step = (qpf + rem) / hz, next = target + step;
        if (next > now) {
            if (i >= n) break;   // late frame: the periods that passed are counted, no wait for the next boundary
            uint64_t t0 = SDL_GetPerformanceCounter();
            w32_sleep_until(next);
            while ((now = w32_qpc()) < next) SDL_Delay(0);
            if (c == g_main_cpu) g_t_sleep += SDL_GetPerformanceCounter() - t0;
            waits++;
        }
        rem = (qpf + rem) % hz;
        target = next;
        i++;
    }
    W32(G_TARGET, (uint32_t)target); W32(G_TARGET + 4, (uint32_t)(target >> 32));
    W32(G_REMAINDER, (uint32_t)rem); W32(G_REMAINDER + 4, (uint32_t)(rem >> 32));
    uint32_t total = R32(G_TOTAL) + i;
    W32(G_ELAPSED, i);
    W32(G_WAITS, waits ? waits - 1 : 0);
    W32(G_TOTAL, total);
    c->eax = i; c->ecx = total; c->edx = waits ? waits - 1 : 0;
    c->esp += 4;
    return 1;
}
void rt_sprof_frame(void);
void fix_frame_pacing(CPU *c) {
    static int on = -1, fps30 = -1;
    rt_sprof_frame();   // sampling profile of the main thread (sprof.c)
#ifndef __vita__
    {   // SH3_MEMDUMP=frame (PC diagnosis): writes the game's data and bss (0x68a000-0x0e00000) to memdump.bin
        static int dumped; extern unsigned g_frame;
        const char *e = getenv("SH3_MEMDUMP");
        if (e && !dumped && g_frame >= (unsigned)atoi(e)) {
            dumped = 1;
            FILE *f = fopen("memdump.bin", "wb");
            if (f) { fwrite(g_mem + 0x68a000, 1, 0x0e00000 - 0x68a000, f); fclose(f); }
            rt_log("[memdump] frame %u\n", g_frame);
        }
    }
#endif
    if (on < 0) on = getenv("SH3_FRAME_PACING") != NULL;
#ifdef __vita__
    if (fps30 < 0) fps30 = !getenv("SH3_FPS30") || atoi(getenv("SH3_FPS30")) != 0;   // validated on the Vita: the default there
#else
    if (fps30 < 0) fps30 = getenv("SH3_FPS30") != NULL;
#endif
    static int nowait = -1;
#ifdef __vita__
    if (nowait < 0) nowait = !getenv("SH3_LATE_NOWAIT") || atoi(getenv("SH3_LATE_NOWAIT")) != 0;
#else
    if (nowait < 0) nowait = getenv("SH3_LATE_NOWAIT") && atoi(getenv("SH3_LATE_NOWAIT")) != 0;
#endif
    if (fps30) {
        uint8_t *const mb = g_mem;
        static unsigned n;
        // The game's own adaptation (0x5f15e0) asks for 3 vblanks (20 FPS) once a frame lasts 3 or more, and comes back
        // only after a long run of fast frames: the "locked at 20" in the park. With late frames not waiting (nowait), the
        // target stays at 2: a 36 ms frame shows at 36 ms, not 50, and the count of elapsed vblanks stays exact.
        if (R32(G_VBLANKS) == 1 || (nowait && R32(G_VBLANKS) == 3)) W32(G_VBLANKS, 2);
        static uint64_t t0; static uint32_t v0;
        if (++n % 300 == 0) {   // game time (vblanks counted / 60) against real time: equal = no slow motion
            uint64_t t = SDL_GetPerformanceCounter();
            if (t0) rt_log("[fps30] vblanks per frame %u, last frame counted %u; 300 frames in %.0f ms real, %.0f ms game time\n",
                           R32(G_VBLANKS), R32(G_ELAPSED), (double)(t - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency(),
                           (double)(R32(G_TOTAL) - v0) * 1000.0 / 60.0);
            t0 = t; v0 = R32(G_TOTAL);
        }
    }
    if (!on && nowait && limiter_native(c)) return;
    if (!on) { F_0041b2c0_orig(c); return; }
    uint8_t *const mb = g_mem;
    uint32_t n = R32(G_VBLANKS), hz = R32(G_REFRESH) ? R32(G_REFRESH) : 60;
    uint64_t qpf = (uint64_t)R32(G_QPF) | (uint64_t)R32(G_QPF + 4) << 32;
    uint64_t target = (uint64_t)R32(G_TARGET) | (uint64_t)R32(G_TARGET + 4) << 32;
    if (n && qpf && target) {
        if (n > hz) n = hz;
        uint64_t budget = qpf * n / hz, now = w32_qpc();
        if (now > target + budget) {
            uint64_t t = now - budget + qpf / 1000;
            W32(G_TARGET, (uint32_t)t); W32(G_TARGET + 4, (uint32_t)(t >> 32));
            W32(G_REMAINDER, 0); W32(G_REMAINDER + 4, 0);
        }
    }
    F_0041b2c0_orig(c);
}

// Diagnosis of (crash at the park gate on the Vita, in 0x457ee0 called by 0x458010 at 0x458064): a link of
// the model's lists read 0x3f000000 (float 0.5) as an offset. Before the original runs, the whole walk is repeated
// with bounds checks; the first link that leaves guest memory is logged with the path that led to it, the bytes of
// the node, and the same bytes re-read from the file they were loaded from (corrupted after the read or not).
// Not installed any more (recomp/image_patches.py): built only with -DSH3_DIAG_MODEL_WALK and the wrap added back.
#ifdef SH3_DIAG_MODEL_WALK
void F_00458010_orig(CPU *c);
typedef struct { uint32_t list, part, sub, node, bad, from; } WalkBad;   // from: the node whose link gave bad
static int out_of_mem(uint32_t a) { return a < 0x10000 || a >= g_mem_size - 0x40; }
// 0x457ee0 (ecx = part): sub-lists at part + [part+4], count word [part+0x18]; each sub-list is a chain of nodes
// (next: node + [node+8] while [node] != 0), each node owning an inner chain at node + [node+4] (same links).
static int walk_part(uint8_t *mb, uint32_t part, WalkBad *w) {
    w->part = part;
    if (out_of_mem(part)) { w->bad = part; return 1; }
    uint32_t s = part + R32(part + 4);
    int32_t n = (int16_t)R16(part + 0x18);
    for (int32_t i = 0; i < n; i++) {
        w->sub = s;
        if (out_of_mem(s)) { w->bad = s; return 1; }
        uint32_t d = s + R32(s + 4);
        for (int guard = 0; guard < 100000; guard++) {
            w->node = d;
            if (out_of_mem(d)) { w->bad = d; w->from = s; return 1; }
            uint32_t e = d + R32(d + 4);
            for (int g2 = 0; g2 < 100000; g2++) {
                if (out_of_mem(e)) { w->bad = e; w->from = d; return 1; }
                if (!R32(e)) break;
                uint32_t next = e + R32(e + 8);
                if (out_of_mem(next)) { w->bad = next; w->from = e; return 1; }
                e = next;
            }
            if (!R32(d)) break;
            uint32_t next = d + R32(d + 8);
            if (out_of_mem(next)) { w->bad = next; w->from = d; return 1; }
            d = next;
        }
        s += R32(s + 8);
    }
    return 0;
}
// 0x458010: parts at hdr + [hdr+0x24] (count word [hdr+0x44]) then hdr + [hdr+0x1c] (count: instance [+0x1c]).
static int walk_model(uint8_t *mb, uint32_t inst, uint32_t hdr, WalkBad *w) {
    static const uint32_t list_off[2] = {0x24, 0x1c};
    for (int l = 0; l < 2; l++) {
        int32_t n = l ? (int32_t)R32(inst + 0x1c) : (int16_t)R16(hdr + 0x44);
        uint32_t p = hdr + R32(hdr + list_off[l]);
        w->list = list_off[l];
        for (int32_t i = 0; i < n; i++) {
            if (walk_part(mb, p, w)) return 1;
            p += R32(p + 8);
        }
    }
    return 0;
}
static void log_bytes(uint8_t *mb, const char *what, uint32_t a) {
    if (out_of_mem(a)) { rt_log("  %s %08x: outside guest memory\n", what, a); return; }
    rt_log("  %s %08x: %08x %08x %08x %08x %08x %08x %08x %08x\n", what, a, R32(a), R32(a + 4), R32(a + 8),
           R32(a + 12), R32(a + 16), R32(a + 20), R32(a + 24), R32(a + 28));
}
void diag_model_walk(CPU *c) {
    uint8_t *const mb = g_mem;
    static int logged;
    uint32_t inst = R32(c->esp + 4), hdr = out_of_mem(inst) ? 0 : R32(inst);
    WalkBad w = {0};
    static unsigned calls;
#ifndef __vita__
    { void rt_watch_start(void); rt_watch_start(); }
#endif
    if (++calls <= 50) {
        extern unsigned g_frame;
        rt_log("[model] walk #%u frame %u instance %08x header %08x caller %08x\n", calls, g_frame, inst, hdr, R32(c->esp));
        if (hdr && !out_of_mem(hdr)) {
            log_bytes(mb, "header  ", hdr);
            void w32_read_ring_compare(uint32_t addr);
            w32_read_ring_compare(hdr);   // which file read put the model there
        }
    }
    // Every valid model starts with ffffffff (PC, same address, same load). on the Vita the third model was
    // not there yet. If a loader thread is still writing it, waiting lets it finish; the log tells how long it took.
    if (hdr && !out_of_mem(hdr) && R32(hdr) != 0xffffffffu) {
        unsigned waited = 0;
        while (R32(hdr) != 0xffffffffu && waited < 5000) { SDL_Delay(5); waited += 5; }
        rt_log("[model] header %08x not ready: %s after %u ms (first word %08x)\n", hdr,
               R32(hdr) == 0xffffffffu ? "ready" : "NEVER READY", waited, R32(hdr));
        rt_log_flush();
    }
    if (!logged && hdr && !out_of_mem(hdr) && walk_model(mb, inst, hdr, &w)) {
        logged = 1;
        rt_log("[model] bad link: list +%02x part %08x sub %08x node %08x: %08x <- %08x; header %08x, instance %08x, "
               "caller %08x\n", w.list, w.part, w.sub, w.node, w.bad, w.from, hdr, inst, R32(c->esp));
        log_bytes(mb, "header  ", hdr);
        log_bytes(mb, "instance", inst);
        log_bytes(mb, "part    ", w.part);
        log_bytes(mb, "sub     ", w.sub);
        log_bytes(mb, "node    ", w.node);
        log_bytes(mb, "from    ", w.from);
        void w32_read_ring_compare(uint32_t addr);
        void w32_read_ring_dump(uint32_t addr, uint32_t n);
        w32_read_ring_compare(hdr);
        if (!out_of_mem(w.from)) w32_read_ring_compare(w.from);
        w32_read_ring_dump(hdr, w.from > hdr ? w.from - hdr + 0x40 : 0x1000);
        void rt_log_flush(void);
        rt_log_flush();   // the original crashes next; the Vita log thread writes only every 100 ms
    }
    F_00458010_orig(c);
}
#endif

// Pause menu background (0x5f2fxx): the frame is copied into a texture asked at the frame size, which D3DX rounds up
// to a power of two (790x448 -> 1024x512), but the game draws it with u, v up to 1.0 ([0xbcc728], [0xbcc72c]), so the
// paused scene came out shrunk into the top-left corner with black around it (PC Fix FullscreenPauseMenu). Hook at
// 0x5f30fa, after the texture is made: the maximum u, v become asked size / real size. Stack as in the function body:
// asked width, height at [esp+0x24], [esp+0x28]; texture D3DSURFACE_DESC at [esp+0x2c] (Width +0x18, Height +0x1c).
#define G_PAUSE_TEX 0xbcc71cu
#define G_PAUSE_UV  0xbcc728u
void fix_pause_uv(CPU *c) {
    uint8_t *const mb = g_mem;
    uint32_t want_w = R32(c->esp + 0x24), want_h = R32(c->esp + 0x28);
    uint32_t tex_w = R32(c->esp + 0x44), tex_h = R32(c->esp + 0x48);
    if (!R32(G_PAUSE_TEX) || !tex_w || !tex_h || want_w > tex_w || want_h > tex_h) return;
    W32(G_PAUSE_UV, rt_u32f((float)want_w / (float)tex_w));
    W32(G_PAUSE_UV + 4, rt_u32f((float)want_h / (float)tex_h));
}
