// DirectSound FX used by SH3: Waves Reverb (BUG-022). Pure code, tested by port/test/dsfx_test.c.
// The game asks for GUID_DSFX_WAVES_REVERB on every reverberated sound effect (footsteps, enemies) and gives up
// on the sound when SetFX/GetObjectInPath fail, so the effect must exist; this is its reverb, mixed as one bus.
#pragma once
#include <stdint.h>

int dsfx_is_waves_reverb(const uint8_t guid[16]);       // GUID_DSFX_WAVES_REVERB
int dsfx_is_waves_reverb_iid(const uint8_t guid[16]);   // IID_IDirectSoundFXWavesReverb8

#define DSREV_COMBS 4
#define DSREV_ALLPASSES 2
typedef struct { float buf[2048]; int len, pos; float filt, g; } DsComb;
typedef struct { float buf[1024]; int len, pos; } DsAllpass;
typedef struct DsReverb {
    DsComb comb[2][DSREV_COMBS];
    DsAllpass ap[2][DSREV_ALLPASSES];
    float in_gain, wet, damp, time_ms;
    int rate;
} DsReverb;

void dsrev_init(DsReverb *r, int rate);
// DSFXWavesReverb parameters: input gain (dB, -96..0), reverb mix (dB, -96..0), reverb time (ms, 0.001..3000),
// high-frequency RT ratio (0.001..0.999).
void dsrev_set(DsReverb *r, float in_gain_db, float mix_db, float time_ms, float hf_ratio);
// Adds the wet signal of n mono send frames to out (stereo interleaved).
void dsrev_process(DsReverb *r, const float *send, float *out, int n);
