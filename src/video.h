/* Native player for Disney Interactive's .ETV full-motion video (HERCULES.EXE FUN_004020c0 and friends):
 * one-second chunks of PCM audio + RNC method-2 packed 4x4-block frames with an 8-bit palette.
 * The TITLE.DLL video table (0x100251e8) indexes the same numbers the PC build opens as e:\data\%d.etv. */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include "audio.h"

typedef struct {
    FILE *f;
    uint32_t w, h, nframes, fps, channels, rate, bits;
    uint32_t *chunk_size; int nchunks;
    uint32_t audio_per_chunk;
    /* decode state */
    int chunk, frame_in_chunk, frame;
    uint8_t *chunkbuf; uint32_t chunkpos, chunklen;
    uint8_t *cur, *ref;
    uint32_t pal[256];
    uint8_t *lut;               /* 256x256 blend table from the 8192-byte mask, NULL until the first one */
    int offs[47], widx;
    Sound snd;                  /* whole soundtrack, decoded up front */
    int voice;
} Video;

/* Finds <n>.ETV in the data directory candidates; returns 0 on success. */
int video_open(Video *v, const char *path);
int video_open_num(Video *v, const char *fs_dir, int n);
/* Decodes the next frame into ARGB (w*h). Returns 0 on success, -1 at the end. */
int video_next(Video *v, uint32_t *argb);
void video_close(Video *v);
