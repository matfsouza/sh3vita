// Saves and settings written whole or not at all. Pure stdio: tested by tests/test_safe_write.py.
// The game opens its save slots, data.sys and disp.ini with CREATE_ALWAYS, which truncates the file at once, so a crash
// or a power cut while it wrote (the Vita's sleep/resume, a GPU fault) left a cut save. kernel32.c CreateFileA writes to
// NAME.tmp instead; CloseHandle calls sw_commit, which moves the old NAME aside as NAME.bak, renames NAME.tmp to NAME
// and drops NAME.bak (rename does not replace an existing file on every platform). When NAME is missing, CreateFileA
// calls sw_recover, which brings back NAME.bak left by a crash between the two renames.
#pragma once
#include <stdio.h>
#include <string.h>

static inline int sw_suffix(const char *final, const char *suffix, char *out, size_t n) {
    size_t a = strlen(final), b = strlen(suffix);
    if (a + b + 1 > n) return -1;
    memcpy(out, final, a);
    memcpy(out + a, suffix, b + 1);
    return 0;
}
static inline int sw_tmp_path(const char *final, char *out, size_t n) { return sw_suffix(final, ".tmp", out, n); }
static inline int sw_bak_path(const char *final, char *out, size_t n) { return sw_suffix(final, ".bak", out, n); }
static inline int sw_exists(const char *p) {
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

// NAME.tmp -> NAME, keeping the old NAME as NAME.bak until the new one is in place. 0 on success.
static inline int sw_commit(const char *final) {
    char tmp[640], bak[640];
    if (sw_tmp_path(final, tmp, sizeof tmp) || sw_bak_path(final, bak, sizeof bak)) return -1;
    remove(bak);
    int had_old = sw_exists(final);
    if (had_old && rename(final, bak)) return -1;
    if (rename(tmp, final)) {
        if (had_old) rename(bak, final);   // put the old save back: the game sees the failure, not a lost slot
        return -1;
    }
    remove(bak);
    return 0;
}

// A half-written NAME.tmp is dropped; a NAME.bak with no NAME comes back as NAME (returns 1). 0 when nothing came back.
static inline int sw_recover(const char *final) {
    char tmp[640], bak[640];
    if (sw_tmp_path(final, tmp, sizeof tmp) || sw_bak_path(final, bak, sizeof bak)) return 0;
    remove(tmp);
    if (!sw_exists(bak)) return 0;
    if (sw_exists(final)) { remove(bak); return 0; }
    return rename(bak, final) == 0;
}
