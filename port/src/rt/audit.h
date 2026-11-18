// Development room tour: always off in sh3vita for now.
// The SH4 tour drove SH4 rooms by guest address (reference/sh4/rt/audit.c); an SH3 equivalent would come with
// the performance route of the design (docs/superpowers/specs/2026-11-01-sh3vita-design.md, section 6).
#pragma once
#include "rt.h"
#define g_audit_enabled 0
#define g_audit_frames 0
#define g_audit_checks 0
static inline void rt_audit_input(CPU *c, uint32_t keys, uint32_t size) { (void)c; (void)keys; (void)size; }
static inline unsigned rt_audit_motion(void) { return 0; }
static inline void rt_audit_frame(unsigned frame) { (void)frame; }
static inline void rt_audit_watchdog(void) {}
static inline int rt_audit_capture(unsigned frame, char *path, size_t size) { (void)frame; (void)path; (void)size; return 0; }
