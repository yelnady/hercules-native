/* FX2: ENG1 renderer extras that work on the level data.
 *
 * Texture scrolls (FUN_10002d80): the map holds a table of VRAM scroll records (header count u16 +0x12; table at
 * blocks + 4 * (c[0xc] + c[0xe] + c[0x10]), self-relative u32 offsets; record +6: u16 dst x, dst y, w, h,
 * src x, src y in 16-bit VRAM units). Scrolling record i by (dx, dy) keeps a wrapped offset and copies the source
 * rectangle to the destination in four wrapped pieces (FUN_10002c60 = MoveImage). The Forest water (object 0x4b,
 * FUN_1000d460) scrolls record 0 by -16 rows every frame; the Training lesson machines scroll their rope record. */
#include "fx2.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static inline uint16_t r16(const uint8_t *d, uint32_t a) { return (uint16_t)(d[a] | d[a + 1] << 8); }
static inline uint32_t r32(const uint8_t *d, uint32_t a) { return d[a] | d[a + 1] << 8 | d[a + 2] << 16 | (uint32_t)d[a + 3] << 24; }

static const void *cur_level;
static uint8_t off_x[64], off_y[64];

static void move_image(uint8_t *vram, int sx, int sy, int w, int h, int dx, int dy)
{
    if (w <= 0 || h <= 0) return;
    if (sx < 0 || sy < 0 || dx < 0 || dy < 0 || sx + w > 1024 || dx + w > 1024 || sy + h > 512 || dy + h > 512) return;
    uint8_t *tmp = malloc((size_t)w * 2 * (size_t)h);
    if (!tmp) return;
    for (int y = 0; y < h; y++) memcpy(tmp + (size_t)y * w * 2, vram + (size_t)(sy + y) * 2048 + (size_t)sx * 2, (size_t)w * 2);
    for (int y = 0; y < h; y++) memcpy(vram + (size_t)(dy + y) * 2048 + (size_t)dx * 2, tmp + (size_t)y * w * 2, (size_t)w * 2);
    free(tmp);
}

int fx2_tex_scroll(Level *L, int idx, int ddx, int ddy)
{
    if (!L || !L->d || !L->vram8) return 0;
    if (cur_level != (const void *)L->d) { cur_level = L->d; memset(off_x, 0, sizeof off_x); memset(off_y, 0, sizeof off_y); }
    const uint8_t *d = L->d;
    int nl = r16(d, 8), c12 = r16(d, 0xc), c14 = r16(d, 0xe), c16 = r16(d, 0x10), n = r16(d, 0x12);
    if (idx < 0 || idx >= n || idx >= 64) return 0;
    uint32_t base = 0x30u + 4u * (uint32_t)nl + 16u + 4u * (uint32_t)(c12 + c14 + c16);
    uint32_t e = base + 4u * (uint32_t)idx;
    if (e + 4 > L->size) return 0;
    uint32_t rec = e + r32(d, e);
    if (rec + 18 > L->size) return 0;
    int tx = r16(d, rec + 6), ty = r16(d, rec + 8), w = r16(d, rec + 10), h = r16(d, rec + 12);
    int sx = (int16_t)r16(d, rec + 14), sy = (int16_t)r16(d, rec + 16);
    if (getenv("FX2_LOG")) { static int once; if (!once++) printf("texscroll n=%d rec %d: dst %d,%d %dx%d src %d,%d\n", n, idx, tx, ty, w, h, sx, sy); }
    if (w <= 0 || h <= 0) return 0;
    int ox = (int)(((unsigned)(ddx + off_x[idx]) % (unsigned)w)), oy = (int)(((unsigned)(off_y[idx] + ddy) % (unsigned)h));
    off_x[idx] = (uint8_t)ox; off_y[idx] = (uint8_t)oy;
    /* FUN_10002d80: four pieces of the source, wrapped by (ox, oy) */
    move_image(L->vram8, sx + ox, sy + oy, w - ox, h - oy, tx, ty);
    move_image(L->vram8, sx + ox, sy, w - ox, oy, tx, ty + h - oy);
    move_image(L->vram8, sx, sy + oy, ox, h - oy, tx + w - ox, ty);
    move_image(L->vram8, sx, sy, ox, oy, tx + w - ox, ty + h - oy);
    return 1;
}
