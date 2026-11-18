// Registry values the SH3 PC installer writes under HKLM\SOFTWARE\KONAMI\SILENT HILL 3 (pure: no runtime state).
#pragma once
#include <stddef.h>
#include <stdint.h>

// INSTALLDIR plus one base folder per data class, read by sh3.exe 0x5f0dd0: "data", "save", "movie", "sound"
// (the installer could leave movies/sounds on the DVD). All point at the guest root, C:\SH3\ (GUEST_ROOT).
// Copies the REG_SZ value (with its NUL) into out and returns its size in bytes, or 0 if the value does not exist.
uint32_t reg_default_value(const char *name, char *out, size_t cap);
// Whether a key exists: only SOFTWARE\KONAMI\SILENT HILL 3 (any case, optional trailing backslash). SH3 also
// probes SOFTWARE\Konami\Silent Hill2, which must fail as on a PC without Silent Hill 2 (BUG-019).
int reg_key_exists(const char *path);
