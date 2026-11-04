/* FX3: the last ENG1 / ENG3 visual items.
 *  - Level light (FUN_10005a60 / FUN_100311a0, ENG3 FUN_1000aaa0 / FUN_10023b00): every sprite's colour word starts
 *    from the level colour built from the map header (ENG1 +0x2c, ENG3 +0x34), the ambient / light pair (default
 *    0,0,0 / 0x80,0x80,0x80) and the fade (DAT_1006ee84, 0x80 when faded in); the PC uses only its R byte.
 *  - Sprite draw modes (+0x23, table 0x1004d6f8): 4 sprite mesh (FUN_100304f0), 7 not drawn (FUN_1000c720),
 *    8 dissolve (FUN_100310e0). */
#pragma once
#include "objects.h"
#include "render.h"

/* FUN_10005a60 for one channel: ambient + light * fade * level colour (0..255). */
int fx3_light_channel(int level_colour, int ambient, int light, int fade);
/* Level light byte for a loaded level (ENG1 map header +0x2c, ENG3 +0x34, R channel; ambient 0, light 0x80,
 * fade 0x80). */
int fx3_level_light(const Level *L);

/* Draw an ENG1 sprite object honouring its +0x23 draw mode. Returns 1 if it handled the draw (modes 4, 7, 8),
 * 0 to let the caller draw it normally. tint = the object's colour word level (bright * 0x010101 or 0), mode =
 * the PSX blend (-1 opaque). */
int fx3_draw_mode(Objects *O, Renderer *R, Object *o, const Image *img, float wx, float wy, float wz, uint32_t tint,
                  int mode);
