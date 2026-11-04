/* FX: visual effects ported from ENG1.DLL that the earlier passes left out.
 *
 *   0x2024 shadow / water reflection child (init FUN_1000f070, update FUN_1000f180, drawn with sprite mode 5
 *   FUN_10030a20 = sprite pieces through the GTE with the object's rotation +0x70/+0x72 and scale +0x3a/+0x3c).
 *   The original spawns it (FUN_10027cd0) for Hercules (FUN_10015090) and for these Forest actors:
 *   rock centaur 10008660, centaur rock 100099e0 / 10009850, 10009ec0 children, Nessus 1000bb10, fragments
 *   1000ceb0 / 1000cfb0, 1000d190 and the 0x4d fish 1000e170.
 *
 *   Shadow: the child sits on the floor under its parent (FUN_10020b50 / FUN_100299d0 distance), turned flat
 *   (rot x = parent - 0xff, i.e. -90 degrees, 0x400 = full turn) and yawed 0x40, scaled by parent scale - dist/2,
 *   flags54 | 0x684 = semi-transparent, abr 0 (average), brightness 0 -> a 50 % darkening of what is under it.
 *   Below the Forest water line (DAT_100718e0 = 0x2e1) it becomes a reflection instead: turned the other way
 *   (+0xff), flags54 | 1 = additive, keeping the parent's brightness, placed on the water surface.
 *
 *   Here the flat sprite is drawn as a vertically squashed billboard whose height is the projected depth extent
 *   of the flat quad (top edge 'h' units further away for a shadow, nearer for a reflection), which is what the
 *   GTE produces for a camera looking along the path. The 0x40 yaw skew is not reproduced.
 */
#include "fx.h"
#include "solids.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int floor_dist(void *octx, const Level *L, int x, int y, int z)
{
    int up, down, inside;
    level_probe(L, x, y, z, &up, &down, &inside);
    int d = down;
    if (octx) {
        int u2 = 0x7fff, d2 = 0x7fff;
        solids_floor(octx, x, y, z, &u2, &d2);
        if (d2 >= 0 && d2 < d) d = d2;
    }
    return d;
}

void fx_shadow_draw(Renderer *R, const Level *L, void *octx, const Image *img, int32_t x, int32_t y, int32_t z,
                    int flip, int scale, int bright, int water_y)
{
    if (!img || img->oy <= 0) return;
    int gte = water_y < 0;                              /* flags54 & 8 shadow (FUN_1000ef10 + FUN_10030790) */
    if (gte) water_y = 0;
    /* FUN_1000f180: y - 2, then down to the floor; FUN_1000ef10: from y, within 0x100, not behind z -16 */
    int ix = x >> 16, iy = (y >> 16) - (gte ? 0 : 2), iz = z >> 16;
    if (gte && z < -0x100000) return;
    int d = floor_dist(octx, L, ix, iy, iz);
    if (gte ? d > 0x100 : (d != 0x100 && d > 0xff)) return;   /* nothing close enough below: no shadow */
    int sy = iy + d;
    int sc = (scale ? scale : 0x100) - (gte ? 0 : d >> 1);   /* the GTE shadow keeps its size (matrix DAT_1005ce50) */
    if (sc <= 0) return;
    int reflect = water_y && sy > water_y;
    if (reflect) sy = water_y;
    float h = img->oy * (sc / 256.0f);                  /* sprite height above the hotspot, world units */
    float bx, bz, tx, tz, bsx, bsy, bzc, tsx, tsy, tzc;
    level_path_to_world(L, (float)ix, (float)iz, &bx, &bz, NULL);
    level_path_to_world(L, (float)ix, (float)iz + (reflect ? -h : h), &tx, &tz, NULL);
    if (!render_project(R, bx, (float)sy, bz, &bsx, &bsy, &bzc)) return;
    if (!render_project(R, tx, (float)sy, tz, &tsx, &tsy, &tzc)) return;
    float px = 320.0f / bzc;                            /* screen pixels per sprite pixel at the base (GEOM_H) */
    float ysc = (bsy - tsy) / (img->oy * px * (sc / 256.0f));   /* >0: extends up (away), <0: down (reflection) */
    if (getenv("FX_LOG")) printf("shadow d=%d sy=%d sc=%d ysc=%.3f base %.1f top %.1f\n", d, sy, sc, ysc, bsy, tsy);
    if (reflect && ysc > 0) ysc = -ysc;
    if (fabsf(ysc) < 0.02f) ysc = ysc < 0 ? -0.02f : 0.02f;
    if (reflect) {
        uint32_t b = (uint32_t)(bright < 0 ? 0 : bright > 0xff ? 0xff : bright);
        render_sprite_fx(R, img, bx, (float)sy, bz, flip, -9.5f, sc, b * 0x010101u, 1, ysc);
    } else
        render_sprite_fx(R, img, bx, (float)sy, bz, flip, -3.9f, sc, 0, 0, ysc);
}

/* The original attaches a 0x2024 child to these Forest actors (init handler address -> shadow). */
int fx_has_shadow(uint32_t init_fn)
{
    switch (init_fn) {
    case 0x10008660: case 0x10009850: case 0x100099e0: case 0x10009ec0: case 0x1000bb10:
    case 0x1000ceb0: case 0x1000cfb0: case 0x1000d190: case 0x1000e170:
        return 1;
    default: return 0;
    }
}
