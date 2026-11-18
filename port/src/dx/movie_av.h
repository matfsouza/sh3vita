#pragma once
#include <stdint.h>
// Hardware movie decoding (Vita SceAvPlayer, movie_av.c); every call is a no-op returning 0 elsewhere.
int mav_open(const char *mp4, int *w, int *h, double *duration);   // 1 if the player has a video stream
void mav_start(void);
void mav_pause(int paused);
int mav_active(void);                                               // 0 once the stream has ended
double mav_time(void);                                              // seconds
int mav_video(uint8_t **y, uint8_t **u, uint8_t **v, double *t);    // 1 with a new frame (planar copies, valid until the next call)
int mav_audio(const int16_t **pcm, int *frames, int *rate, int *channels);   // 1 with a block of interleaved PCM
void mav_close(void);
