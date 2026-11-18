// Cube-texture layout shared by the D3D8 layer (pure: no GL calls, testable on the host).
// A cube texture keeps one full mip chain per face, faces in D3DCUBEMAP_FACES order
// (+X, -X, +Y, -Y, +Z, -Z), which is also the order of the GL cube-map face targets.
#pragma once
#include <stdint.h>

#define CUBE_FACES 6u

static inline int cube_face_valid(uint32_t face) { return face < CUBE_FACES; }
static inline uint32_t cube_surface_index(uint32_t face, uint32_t level, uint32_t levels) { return face * levels + level; }
static inline uint32_t cube_gl_target(uint32_t face) { return 0x8515u + face; }   // GL_TEXTURE_CUBE_MAP_POSITIVE_X + face
