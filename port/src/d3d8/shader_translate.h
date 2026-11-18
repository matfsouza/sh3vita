// Pure D3D8 -> GLSL translator, also built by sh4prep without SDL or GL.
#pragma once
#include <stdint.h>
#ifndef MAX_VSC
#define MAX_VSC 96
#endif
char *vs_translate(const uint32_t *tokens, int *error);
char *vs_translate_ex(const uint32_t *tokens, uint32_t bgra, int *error);
char *ps_translate(const uint32_t *tokens, int *error);
int vs_constant_count(void);
// Fragment shader source with the stages of `cube_mask` (bit per stage) turned into cube maps: samplerCube declaration,
// texture2D(sN, vTk.xy) -> textureCube(sN, vTk.xyz). New string (shader_translate_free).
char *glsl_cube_stages(const char *src, unsigned cube_mask);
void shader_translate_free(void *source);
