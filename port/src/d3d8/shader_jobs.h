// Background shader compilation queue: the pure state machine (tests/test_shader_jobs.py). The render thread
// requests a program by its cache key and skips the draw while it is queued or compiling; a worker thread takes jobs
// in request order, compiles them (draw.c, vitaGL vglCompileGlslPairToBinary) and marks them done or failed; the
// render thread then takes the slot, links the binaries and releases it. The caller holds a lock around every call.
#pragma once
#include <stdint.h>

#define SJ_MAX 64
enum { SJ_EMPTY, SJ_QUEUED, SJ_COMPILING, SJ_DONE, SJ_FAILED, SJ_FULL };

typedef struct {
    uint64_t key[SJ_MAX];
    uint8_t state[SJ_MAX];
    uint32_t order[SJ_MAX];   // request number: the worker takes the oldest queued job first
    uint32_t next_order;
} ShaderJobs;

static inline void sj_init(ShaderJobs *q) {
    for (int i = 0; i < SJ_MAX; i++) { q->key[i] = 0; q->state[i] = SJ_EMPTY; q->order[i] = 0; }
    q->next_order = 0;
}
static inline int sj_find(const ShaderJobs *q, uint64_t key) {
    for (int i = 0; i < SJ_MAX; i++) if (q->state[i] != SJ_EMPTY && q->key[i] == key) return i;
    return -1;
}
// The state of this program's job, queueing it if it has none (SJ_FULL: no free slot, nothing queued).
static inline int sj_request(ShaderJobs *q, uint64_t key) {
    int i = sj_find(q, key);
    if (i >= 0) return q->state[i];
    for (i = 0; i < SJ_MAX; i++)
        if (q->state[i] == SJ_EMPTY) { q->key[i] = key; q->state[i] = SJ_QUEUED; q->order[i] = q->next_order++; return SJ_QUEUED; }
    return SJ_FULL;
}
// Worker: the oldest queued job, now compiling (-1: none).
static inline int sj_next(ShaderJobs *q) {
    int best = -1;
    for (int i = 0; i < SJ_MAX; i++)
        if (q->state[i] == SJ_QUEUED && (best < 0 || (int32_t)(q->order[i] - q->order[best]) < 0)) best = i;
    if (best >= 0) q->state[best] = SJ_COMPILING;
    return best;
}
static inline void sj_finish(ShaderJobs *q, int i, int ok) { q->state[i] = ok ? SJ_DONE : SJ_FAILED; }
// Render thread: the slot of a finished (done or failed) job, which stays owned until sj_release (-1: not finished).
static inline int sj_take(const ShaderJobs *q, uint64_t key) {
    int i = sj_find(q, key);
    return i >= 0 && (q->state[i] == SJ_DONE || q->state[i] == SJ_FAILED) ? i : -1;
}
static inline void sj_release(ShaderJobs *q, int i) { q->state[i] = SJ_EMPTY; q->key[i] = 0; }
static inline int sj_count(const ShaderJobs *q) {
    int n = 0;
    for (int i = 0; i < SJ_MAX; i++) n += q->state[i] != SJ_EMPTY;
    return n;
}
// Jobs queued or compiling (the worker has work).
static inline int sj_busy(const ShaderJobs *q) {
    for (int i = 0; i < SJ_MAX; i++) if (q->state[i] == SJ_QUEUED || q->state[i] == SJ_COMPILING) return 1;
    return 0;
}
