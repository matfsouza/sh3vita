// Common infrastructure of the emulated Win32 APIs.
#pragma once
#include "rt.h"
#include <SDL.h>

enum { H_FREE, H_FILE, H_EVENT, H_MUTEX, H_THREAD, H_REGKEY, H_TIMER, H_SEM, H_MMIO, H_FIND, H_MAPPING };

uint32_t h_new(int type, void *obj);
void *h_get(uint32_t h, int type);
int h_type(uint32_t h);
void h_close(uint32_t h);   // for objects that are never freed (events, threads...): the number is free at once
// Objects another thread may free while a call uses them (files, mmio, find, registry keys): every call takes
// obj = h_acquire(h, type) and ends with h_release(h); the close takes obj = h_retire(h, type, &now). Whoever gets
// 1 (h_release) or now = 1 (h_retire) destroys the object; the handle number is reused only after that (handle_ref.h).
void *h_acquire(uint32_t h, int type);
int h_release(uint32_t h);
void *h_retire(uint32_t h, int type, int *now);

// Waitable objects: all protected by a global lock and a common signal.
extern SDL_mutex *g_sync;
extern SDL_cond *g_sync_cv;
typedef struct { int manual, state; uint32_t epoch, tokens, waiters; } Event;   // epoch/tokens/waiters: PulseEvent (handles.c)
typedef struct { int32_t count, max; } Semaphore;
typedef struct { SDL_Thread *t; CPU *c; uint32_t start, param, tid; int done, suspend; void *exit_jb; } Thread;   // suspend, exit_jb: w32_thread.c
int w32_signaled(uint32_t h);        // under g_sync
int w32_has_waiters(uint32_t h);     // under g_sync: a thread is in w32_wait on h
int w32_consume(uint32_t h);         // under g_sync: resets an auto event, takes a semaphore count (0: none left)
int w32_sem_try(uint32_t h);         // lock-free: takes one count of a semaphore if it has one (1 = taken)
uint32_t w32_wait(uint32_t n, const uint32_t *hs, int all, uint32_t timeout_ms);

void w32_set_last_error(CPU *c, uint32_t e);
// SH3_TRACE_SYNC=1: logs synchronisation calls (handle, type, tid), up to 600 lines.
void w32_trace_sync(const char *what, uint32_t h, uint32_t extra);
void w32_event_set(uint32_t h, int v);   // SetEvent/ResetEvent from any thread
void w32_event_pulse(uint32_t h);        // PulseEvent
void w32_host_path(const char *gpath, char *out, size_t n);
void w32_post_message(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);   // to the window procedure, any thread
uint32_t w32_tick_ms(void);
uint64_t w32_qpc(void);
void w32_sleep_until(uint64_t t);
unsigned w32_hz(void);

#define GUEST_ROOT "C:\\SH3\\"
#define WAIT_TIMEOUT_ 0x102u
