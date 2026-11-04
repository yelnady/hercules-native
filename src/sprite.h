/* Eurocom 'i1.4' ANIMPSX.BIN sprite banks. Ported from ENG1 FUN_10003cd0 (frame lookup),
 * FUN_10004ca0 (RLE unpack), FUN_10004010 (piece placement) and FUN_10005780 (palettes). */
#pragma once
#include <stdint.h>
#include "fs.h"
#include "render.h"

typedef struct {
    const uint8_t *d;
    uint32_t size;
    int count;
    Image *cache;       /* lazily decoded frames, index 1..count */
    uint8_t *done;
    Image *pcache[32]; uint8_t *pdone[32];   /* FX2: 32 (ENG3 crowds use palettes up to 0x1d) */   /* LEFTOVERS: frames decoded with an object palette override (+0x1c) */
    Image *ccache[16]; uint8_t *cdone[16]; /* FX: frames decoded with a CLUT uploaded at run time (keyed 0..15) */
    int levelpal;       /* FX2: game bank: colours 0x80..0xff come from the level palette */
    int gen;            /* FX2: level-palette generation the cached frames were decoded with */
} Bank;

int bank_load(Bank *b, const Fs *fs, const char *path);
void bank_free(Bank *b);
const Image *bank_frame(Bank *b, int i);
/* LEFTOVERS: frame drawn with palette `pal` instead of its own (ENG1 FUN_10003cd0: object +0x1c != 0xff). */
const Image *bank_frame_pal(Bank *b, int i, int pal);
/* FX: frame decoded through a 256-entry ARGB palette supplied by the caller (ENG1 FUN_10005820 CLUT uploads,
 * e.g. Hades' hair-flame palettes); `key` 0..15 identifies the palette for the cache. */
const Image *bank_frame_cpal(Bank *b, int i, int key, const uint32_t pal[256]);

/* FX2: the level's screen palette (CIF); sprite colours 0x80..0xff come from it, as on the PC build. */
void sprite_set_level_palette(const uint8_t pal[256][3]);
