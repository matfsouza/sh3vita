// wsprintfA-style formatting of a guest format string with guest stack arguments (pure: no runtime state).
#pragma once
#include <stddef.h>
#include <stdint.h>

// mb: guest memory base; fmt: guest address of the format; args: guest address of the first 32-bit argument.
// Supports %[-0][width][.prec][h|l]{d,i,u,x,X,c,s,p,%}. Writes at most cap-1 chars plus NUL; returns the length written.
int guest_vformat(const uint8_t *mb, char *out, size_t cap, uint32_t fmt, uint32_t args);
