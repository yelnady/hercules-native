/* AUDSHELL: HERCULES letter display shared by ENG1 (objects.c) and ENG3 (e3game.c); see scrolls.c. */
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include "eng1.h"
#include "render.h"
#include "sprite.h"

typedef struct Scrolls Scrolls;

typedef struct {
    const PeImage *pe;          /* the DLL holding the tables */
    uint32_t tx, tframe, tscript;       /* x table (u32 16.16), still-frame table (s16 every 4 bytes), script pointer table */
    int ty;                     /* resting y (16.16): 0x460000 in ENG1, 0x320000 in ENG3 */
    int pairs;                  /* script format: 1 = {s16, s16} entries (ENG3), 0 = s16 words (ENG1) */
    int home_bank;              /* bank of the still frames (ENG1 2, ENG3 8) */
    int steps;                  /* pixels moved per tick (6 in ENG1, 8 in ENG3) */
} ScrollCfg;

Scrolls *scrolls_new(void);
void scrolls_free(Scrolls *S);
void scrolls_reset(Scrolls *S);
void scrolls_trigger(Scrolls *S, int id);
void scrolls_tick(Scrolls *S, const ScrollCfg *C, void (*tick_sound)(void *, int x), void *ctx);   /* x = icon screen x */
void scrolls_draw(Scrolls *S, const ScrollCfg *C, Renderer *R, uint32_t owned, Bank *(*get_bank)(void *, int), void *ctx);
