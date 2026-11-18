// Tests of the pure D3D8 helpers (cube-map layout). Built and run by tests/test_d3d8_pure.py.
#include "cube_faces.h"
#include <stdio.h>

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    // Surfaces of a cube texture: one full mip chain per face, faces in D3DCUBEMAP_FACES order.
    CHECK(cube_surface_index(0, 0, 5) == 0, "+X level 0");
    CHECK(cube_surface_index(0, 4, 5) == 4, "+X level 4");
    CHECK(cube_surface_index(1, 0, 5) == 5, "-X level 0");
    CHECK(cube_surface_index(5, 4, 5) == 29, "-Z last level");
    // D3D faces (+X, -X, +Y, -Y, +Z, -Z) map one to one on the GL cube-map targets.
    CHECK(cube_gl_target(0) == 0x8515u, "+X -> GL_TEXTURE_CUBE_MAP_POSITIVE_X");
    CHECK(cube_gl_target(1) == 0x8516u, "-X -> GL_TEXTURE_CUBE_MAP_NEGATIVE_X");
    CHECK(cube_gl_target(4) == 0x8519u, "+Z -> GL_TEXTURE_CUBE_MAP_POSITIVE_Z");
    CHECK(cube_gl_target(5) == 0x851Au, "-Z -> GL_TEXTURE_CUBE_MAP_NEGATIVE_Z");
    CHECK(!cube_face_valid(6) && cube_face_valid(5), "face range");
    printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
    return fails != 0;
}
