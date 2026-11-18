// Hardware movie decoding on the Vita: SceAvPlayer plays the <movie>.mp4 (H.264 + AAC) that
// tools/prepare_data.py writes next to each .000. movie.c hands its frames to the game exactly like pl_mpeg's, so
// the game's DirectShow path does not change; without the .mp4, or if the player fails, movie.c keeps pl_mpeg.
// Frame layout: Y plane, then V/U interleaved (YVU420P2, what the Vita ports display with SceGxm), pitch = width.
#include "movie_av.h"
#ifdef __vita__
#include <malloc.h>
#include <psp2/avplayer.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>
#include "rt.h"

static SceAvPlayerHandle s_av;
static uint8_t *s_planes;                 // Y, U, V copies of the last frame (planar)
static int s_w, s_h;
static double s_duration;
static struct { void *base, *aligned; } s_frames[8];

static void *av_alloc(void *p, uint32_t align, uint32_t size) { (void)p; return memalign(align < 16 ? 16 : align, size); }
static void av_free(void *p, void *ptr) { (void)p; free(ptr); }
static void *av_frame_alloc(void *p, uint32_t align, uint32_t size) {   // decoder output: GPU-mapped memory
    (void)p;
    if (align < 4096) align = 4096;
    for (int i = 0; i < 8; i++) {
        if (s_frames[i].base) continue;
        uint8_t *b = vglAlloc(size + align, VGL_MEM_VRAM);
        if (!b) b = vglAlloc(size + align, VGL_MEM_RAM);
        if (!b) return NULL;
        s_frames[i].base = b;
        s_frames[i].aligned = (void *)(((uintptr_t)b + align - 1) & ~(uintptr_t)(align - 1));
        return s_frames[i].aligned;
    }
    return NULL;
}
static void av_frame_free(void *p, void *ptr) {
    (void)p;
    for (int i = 0; i < 8; i++)
        if (s_frames[i].aligned == ptr) { vglFree(s_frames[i].base); s_frames[i].base = s_frames[i].aligned = NULL; }
}

int mav_open(const char *mp4, int *w, int *h, double *duration) {
    static int loaded;
    if (!loaded) loaded = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER) >= 0 ? 1 : -1;
    if (loaded < 0) { rt_log("[movie-av] SceAvPlayer module not loaded\n"); return 0; }
    SceAvPlayerInitData init;
    memset(&init, 0, sizeof init);
    init.memoryReplacement.allocate = av_alloc;
    init.memoryReplacement.deallocate = av_free;
    init.memoryReplacement.allocateTexture = av_frame_alloc;
    init.memoryReplacement.deallocateTexture = av_frame_free;
    init.basePriority = 0xA0;
    init.numOutputVideoFrameBuffers = 2;
    init.autoStart = 0;
    s_av = sceAvPlayerInit(&init);
    if (s_av < 0) { rt_log("[movie-av] init failed %08x\n", s_av); s_av = 0; return 0; }
    int r = sceAvPlayerAddSource(s_av, mp4);
    if (r < 0) { rt_log("[movie-av] %s: open failed %08x\n", mp4, r); mav_close(); return 0; }
    // The stream information arrives asynchronously.
    SceAvPlayerStreamInfo info;
    s_w = s_h = 0; s_duration = 0;
    for (int tries = 0; tries < 200 && !s_w; tries++) {
        for (uint32_t id = 0; id < 4; id++) {
            memset(&info, 0, sizeof info);
            if (sceAvPlayerGetStreamInfo(s_av, id, &info) < 0) continue;
            if (info.type == SCE_AVPLAYER_VIDEO) { s_w = (int)info.details.video.width; s_h = (int)info.details.video.height; s_duration = info.duration / 1000.0; }
        }
        if (!s_w) sceKernelDelayThread(10000);
    }
    if (!s_w || !s_h) { rt_log("[movie-av] %s: no video stream\n", mp4); mav_close(); return 0; }
    s_planes = malloc((size_t)s_w * s_h * 3 / 2);
    if (!s_planes) { mav_close(); return 0; }
    *w = s_w; *h = s_h; *duration = s_duration;
    rt_log("[movie-av] %s: %dx%d, %.1f s (hardware decoder)\n", mp4, s_w, s_h, s_duration);
    return 1;
}

void mav_start(void) { if (s_av) sceAvPlayerStart(s_av); }
void mav_pause(int paused) { if (s_av) { if (paused) sceAvPlayerPause(s_av); else sceAvPlayerResume(s_av); } }
int mav_active(void) { return s_av && sceAvPlayerIsActive(s_av); }
double mav_time(void) { return s_av ? sceAvPlayerCurrentTime(s_av) / 1000.0 : 0; }

int mav_video(uint8_t **y, uint8_t **u, uint8_t **v, double *t) {
    SceAvPlayerFrameInfo f;
    if (!s_av || !sceAvPlayerGetVideoData(s_av, &f) || !f.pData) return 0;
    size_t ys = (size_t)s_w * s_h, cs = ys / 4;
    uint8_t *py = s_planes, *pu = py + ys, *pv = pu + cs;
    memcpy(py, f.pData, ys);
    const uint8_t *vu = f.pData + ys;
    for (size_t i = 0; i < cs; i++) { pv[i] = vu[2 * i]; pu[i] = vu[2 * i + 1]; }
    *y = py; *u = pu; *v = pv; *t = f.timeStamp / 1000.0;
    return 1;
}

int mav_audio(const int16_t **pcm, int *frames, int *rate, int *channels) {
    SceAvPlayerFrameInfo f;
    if (!s_av || !sceAvPlayerGetAudioData(s_av, &f) || !f.pData) return 0;
    *channels = f.details.audio.channelCount ? f.details.audio.channelCount : 2;
    *rate = (int)f.details.audio.sampleRate;
    *frames = (int)(f.details.audio.size / (2u * (unsigned)*channels));
    *pcm = (const int16_t *)f.pData;
    return 1;
}

void mav_close(void) {
    if (s_av) { sceAvPlayerStop(s_av); sceAvPlayerClose(s_av); }
    s_av = 0;
    free(s_planes); s_planes = NULL;
}
#else
int mav_open(const char *mp4, int *w, int *h, double *duration) { (void)mp4; (void)w; (void)h; (void)duration; return 0; }
void mav_start(void) {}
void mav_pause(int paused) { (void)paused; }
int mav_active(void) { return 0; }
double mav_time(void) { return 0; }
int mav_video(uint8_t **y, uint8_t **u, uint8_t **v, double *t) { (void)y; (void)u; (void)v; (void)t; return 0; }
int mav_audio(const int16_t **pcm, int *frames, int *rate, int *channels) { (void)pcm; (void)frames; (void)rate; (void)channels; return 0; }
void mav_close(void) {}
#endif
