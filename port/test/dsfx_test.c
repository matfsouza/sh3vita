// Tests for port/src/dx/dsfx.c: Waves Reverb GUIDs and the reverb bus.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dsfx.h"

static int fails;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

// GUID_DSFX_WAVES_REVERB {87FC0268-9A55-4360-95AA-004A1D9DE26C}, in memory order (the game passes it this way)
static const uint8_t WAVES[16] = {0x68, 0x02, 0xfc, 0x87, 0x55, 0x9a, 0x60, 0x43, 0x95, 0xaa, 0x00, 0x4a, 0x1d, 0x9d, 0xe2, 0x6c};
// IID_IDirectSoundFXWavesReverb8 {46858C3A-0DC6-45E3-B760-D4EEF16CB325}
static const uint8_t WAVES_IID[16] = {0x3a, 0x8c, 0x85, 0x46, 0xc6, 0x0d, 0xe3, 0x45, 0xb7, 0x60, 0xd4, 0xee, 0xf1, 0x6c, 0xb3, 0x25};

#define RATE 48000
#define LEN (RATE * 2)

// Feeds a unit impulse into the reverb send and returns the wet output (stereo interleaved, LEN frames).
static float *impulse(float in_gain_db, float mix_db, float time_ms, float hf) {
    static DsReverb r;
    dsrev_init(&r, RATE);
    dsrev_set(&r, in_gain_db, mix_db, time_ms, hf);
    float *send = calloc(LEN, sizeof(float)), *out = calloc(LEN * 2, sizeof(float));
    send[0] = 1.f;
    dsrev_process(&r, send, out, LEN);
    free(send);
    return out;
}

static double energy(const float *out, int from_ms, int to_ms) {
    double e = 0;
    for (int i = RATE * from_ms / 1000; i < RATE * to_ms / 1000; i++) e += out[2 * i] * out[2 * i] + out[2 * i + 1] * out[2 * i + 1];
    return e;
}

int main(void) {
    uint8_t other[16];
    memcpy(other, WAVES, 16);
    other[15] ^= 1;
    CHECK(dsfx_is_waves_reverb(WAVES));
    CHECK(!dsfx_is_waves_reverb(other));
    CHECK(!dsfx_is_waves_reverb(WAVES_IID));
    CHECK(dsfx_is_waves_reverb_iid(WAVES_IID));
    CHECK(!dsfx_is_waves_reverb_iid(WAVES));

    // A room reverb: a tail exists after the impulse and decays.
    float *a = impulse(0.f, 0.f, 1000.f, 0.5f);
    int finite = 1;
    for (int i = 0; i < LEN * 2; i++) finite &= isfinite(a[i]) != 0;
    CHECK(finite);
    CHECK(energy(a, 50, 250) > 1e-4);
    CHECK(energy(a, 1500, 2000) < energy(a, 50, 250) * 0.01);

    // A longer reverb time keeps more energy late in the tail.
    float *longer = impulse(0.f, 0.f, 2500.f, 0.5f), *shorter = impulse(0.f, 0.f, 300.f, 0.5f);
    CHECK(energy(longer, 500, 1000) > energy(shorter, 500, 1000) * 10);

    // Reverb mix of -96 dB (the API minimum) is silent; lower mix means less wet energy.
    float *mute = impulse(0.f, -96.f, 1000.f, 0.5f), *quiet = impulse(0.f, -12.f, 1000.f, 0.5f);
    CHECK(energy(mute, 0, 2000) < 1e-9);
    CHECK(energy(quiet, 0, 2000) < energy(a, 0, 2000) * 0.2);

    // A dark reverb (HF ratio 0.001, what the game sends) still produces a tail.
    float *dark = impulse(0.f, 0.f, 1000.f, 0.001f);
    CHECK(energy(dark, 50, 250) > 1e-5);

    free(a); free(longer); free(shorter); free(mute); free(quiet); free(dark);
    if (fails) return 1;
    printf("ok\n");
    return 0;
}
