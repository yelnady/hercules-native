/* FX: ENG1 visual effects (see fx.c). */
#pragma once
#include <stdint.h>
#include "level.h"
#include "render.h"

/* Draw the 0x2024 shadow (or, below water_y, the water reflection) of a sprite standing at x,y,z (16.16).
 * octx = Objects* for platform floors (NULL = map only), scale 0x100 = 1, bright = parent +0x4a.
 * water_y < 0 selects the generic flags54 & 8 actor shadow (FUN_1000ef10 / FUN_10030790) instead. */
void fx_shadow_draw(Renderer *R, const Level *L, void *octx, const Image *img, int32_t x, int32_t y, int32_t z,
                    int flip, int scale, int bright, int water_y);
int fx_has_shadow(uint32_t init_fn);
