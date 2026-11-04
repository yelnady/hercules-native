/* FX2: level-data renderer extras (see fx2.c). */
#pragma once
#include "level.h"

/* FUN_10002d80: scroll texture record idx of the level by (dx, dy) 16-bit VRAM pixels (wrapped). 0 = none. */
int fx2_tex_scroll(Level *L, int idx, int dx, int dy);
