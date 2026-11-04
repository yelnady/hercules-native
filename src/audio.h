/* ESF sound effects (IMA ADPCM, high nibble first; HERCULES.EXE FUN_0040f3d0) and a small SDL mixer. */
#pragma once
#include <stdint.h>
#include "fs.h"

typedef struct {
    int16_t *pcm;
    int n;
    int rate;
    int loop;           /* ESF header byte 0x0c: buffer is played with DSBPLAY_LOOPING */
    int bits8;          /* ESF header byte 0x0d: the PC port kept an 8-bit DirectSound buffer */
} Sound;

int audio_init(void);
void audio_shutdown(void);
int sound_load(Sound *s, const Fs *fs, const char *path);
void sound_free(Sound *s);
/* Plays a sound; returns a voice id or -1. vol 0..256, pan -256..256. */
int sound_play(const Sound *s, int vol, int pan, int loop);
/* Plays with explicit linear channel gains (0..1). */
int sound_play_gain(const Sound *s, float gl, float gr, int loop);
void sound_set_gain(int voice, float gl, float gr);
/* 1 while `voice` is still playing `s`. */
int sound_playing(int voice, const Sound *s);
void sound_stop(int voice);

/* Streamed background music: interleaved stereo s16 at `rate`, looped between loop_start and n frames. */
void music_set(int16_t *pcm, int frames, int rate, float gain);   /* takes ownership of pcm; NULL stops */
void music_set_gain(float gain);      /* AUDSHELL: change the gain of the playing music */
