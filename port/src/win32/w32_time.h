// FILETIME <-> SYSTEMTIME without the host C library (pure: no gmtime, valid from 1601 like Windows).
#pragma once
#include <stdint.h>

// ft: 100-ns intervals since 1601-01-01 UTC. st: year, month, day of week (0 = Sunday), day, hour, minute,
// second, milliseconds (the SYSTEMTIME field order). docs/BUGS.md BUG-013.
void filetime_to_systemtime(uint64_t ft, uint16_t st[8]);
