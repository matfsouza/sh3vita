// Waves Reverb for the emulated DirectSound (see dsfx.h). A small Schroeder reverb: 4 damped combs and 2 allpasses
// per channel, cheap enough for the Vita's audio thread.
#include "dsfx.h"
#include <math.h>
#include <string.h>

static const uint8_t WAVES_REVERB[16] = {0x68, 0x02, 0xfc, 0x87, 0x55, 0x9a, 0x60, 0x43,
                                         0x95, 0xaa, 0x00, 0x4a, 0x1d, 0x9d, 0xe2, 0x6c};
static const uint8_t WAVES_REVERB_IID[16] = {0x3a, 0x8c, 0x85, 0x46, 0xc6, 0x0d, 0xe3, 0x45,
                                             0xb7, 0x60, 0xd4, 0xee, 0xf1, 0x6c, 0xb3, 0x25};

int dsfx_is_waves_reverb(const uint8_t guid[16]) { return !memcmp(guid, WAVES_REVERB, 16); }
int dsfx_is_waves_reverb_iid(const uint8_t guid[16]) { return !memcmp(guid, WAVES_REVERB_IID, 16); }

// Delay lengths in samples at 44.1 kHz (mutually prime); the right channel is offset for stereo width.
static const int COMB_LEN[DSREV_COMBS] = {1116, 1188, 1277, 1356};
static const int AP_LEN[DSREV_ALLPASSES] = {556, 441};
#define STEREO_SPREAD 23
#define INPUT_SCALE 0.015f
#define WET_SCALE 3.0f

static float db(float v) { return v <= -96.f ? 0.f : powf(10.f, v / 20.f); }

static void update_feedback(DsReverb *r) {
    float t = r->time_ms > 1.f ? r->time_ms / 1000.f : 0.001f;
    for (int ch = 0; ch < 2; ch++)
        for (int k = 0; k < DSREV_COMBS; k++)   // -60 dB after the reverb time
            r->comb[ch][k].g = powf(10.f, -3.f * (float)r->comb[ch][k].len / (t * (float)r->rate));
}

void dsrev_init(DsReverb *r, int rate) {
    memset(r, 0, sizeof *r);
    r->rate = rate;
    for (int ch = 0; ch < 2; ch++) {
        for (int k = 0; k < DSREV_COMBS; k++) {
            int len = (int)((float)COMB_LEN[k] * (float)rate / 44100.f) + ch * STEREO_SPREAD;
            r->comb[ch][k].len = len < 2048 ? len : 2048;
        }
        for (int k = 0; k < DSREV_ALLPASSES; k++) {
            int len = (int)((float)AP_LEN[k] * (float)rate / 44100.f) + ch * STEREO_SPREAD;
            r->ap[ch][k].len = len < 1024 ? len : 1024;
        }
    }
    dsrev_set(r, 0.f, 0.f, 1000.f, 0.001f);
}

void dsrev_set(DsReverb *r, float in_gain_db, float mix_db, float time_ms, float hf_ratio) {
    r->in_gain = db(in_gain_db) * INPUT_SCALE;
    r->wet = db(mix_db) * WET_SCALE;
    r->time_ms = time_ms > 3000.f ? 3000.f : time_ms;
    float d = 0.95f * (1.f - hf_ratio);   // high frequencies die sooner when the ratio is small
    r->damp = d < 0.f ? 0.f : d > 0.9f ? 0.9f : d;
    update_feedback(r);
}

void dsrev_process(DsReverb *r, const float *send, float *out, int n) {
    if (r->wet == 0.f) return;
    for (int i = 0; i < n; i++) {
        float in = send[i] * r->in_gain;
        for (int ch = 0; ch < 2; ch++) {
            float acc = 0.f;
            for (int k = 0; k < DSREV_COMBS; k++) {
                DsComb *c = &r->comb[ch][k];
                float y = c->buf[c->pos];
                c->filt = y * (1.f - r->damp) + c->filt * r->damp;
                c->buf[c->pos] = in + c->filt * c->g;
                if (++c->pos >= c->len) c->pos = 0;
                acc += y;
            }
            for (int k = 0; k < DSREV_ALLPASSES; k++) {
                DsAllpass *a = &r->ap[ch][k];
                float b = a->buf[a->pos];
                a->buf[a->pos] = acc + b * 0.5f;
                if (++a->pos >= a->len) a->pos = 0;
                acc = b - acc;
            }
            out[2 * i + ch] += acc * r->wet;
        }
    }
}
