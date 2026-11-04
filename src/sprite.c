#include "sprite.h"
#include <stdlib.h>
#include <string.h>

static inline uint32_t u32(const uint8_t *d, uint32_t a) { return d[a] | d[a+1] << 8 | d[a+2] << 16 | (uint32_t)d[a+3] << 24; }
static inline uint16_t u16(const uint8_t *d, uint32_t a) { return (uint16_t)(d[a] | d[a+1] << 8); }

/* FX2: level screen palette (CIF) for sprite colours 0x80..0xff; gen invalidates decoded frames on a change. */
static uint8_t lvl_pal[256][3];
static int lvl_pal_set, lvl_pal_gen = 1;

int bank_load(Bank *b, const Fs *fs, const char *path)
{
    memset(b, 0, sizeof *b);
    b->d = fs_get(fs, path, &b->size);
    if (!b->d || b->size < 8 || memcmp(b->d, "i1.4", 4)) return -1;
    b->count = u16(b->d, 4);
    b->gen = lvl_pal_gen;   /* FX2 */
    b->levelpal = path[0] == 'P' || path[0] == 'p';   /* FX2: in-game (P:\\SRC*) banks draw on the level palette */
    b->cache = calloc(b->count + 1, sizeof(Image));
    b->done = calloc(b->count + 1, 1);
    return 0;
}

void bank_free(Bank *b)
{
    if (b->cache) for (int i = 0; i <= b->count; i++) free(b->cache[i].px);
    free(b->cache); free(b->done);
    for (int k = 0; k < 32; k++) {            /* LEFTOVERS / FX2 */
        if (b->pcache[k]) for (int i = 0; i <= b->count; i++) free(b->pcache[k][i].px);
        free(b->pcache[k]); free(b->pdone[k]);
    }
    for (int k = 0; k < 16; k++) {            /* FX */
        if (b->ccache[k]) for (int i = 0; i <= b->count; i++) free(b->ccache[k][i].px);
        free(b->ccache[k]); free(b->cdone[k]);
    }
    memset(b, 0, sizeof *b);
}


void sprite_set_level_palette(const uint8_t pal[256][3])
{
    if (pal) {
        if (lvl_pal_set && !memcmp(lvl_pal, pal, sizeof lvl_pal)) return;
        memcpy(lvl_pal, pal, sizeof lvl_pal); lvl_pal_set = 1;
    } else lvl_pal_set = 0;
    lvl_pal_gen++;
}

static void bank_flush(Bank *b)              /* FX2: drop frames decoded with an older level palette */
{
    if (b->gen == lvl_pal_gen || !b->levelpal) return;
    b->gen = lvl_pal_gen;
    if (b->cache) for (int i = 0; i <= b->count; i++) { free(b->cache[i].px); memset(&b->cache[i], 0, sizeof(Image)); }
    if (b->done) memset(b->done, 0, (size_t)b->count + 1);
    for (int k = 0; k < 32; k++) if (b->pcache[k]) {
        for (int i = 0; i <= b->count; i++) { free(b->pcache[k][i].px); memset(&b->pcache[k][i], 0, sizeof(Image)); }
        memset(b->pdone[k], 0, (size_t)b->count + 1);
    }
    for (int k = 0; k < 16; k++) if (b->ccache[k]) {
        for (int i = 0; i <= b->count; i++) { free(b->ccache[k][i].px); memset(&b->ccache[k][i], 0, sizeof(Image)); }
        memset(b->cdone[k], 0, (size_t)b->count + 1);
    }
}

static int palette(const Bank *b, int idx, uint32_t out[256])
{
    uint32_t po = 4 * (uint32_t)b->count + 16 + 2 * (uint32_t)idx;
    if (po + 2 > b->size) return -1;
    uint32_t x = u16(b->d, po);
    if (4 + x + 4 > b->size) return -1;
    int n = u16(b->d, 4 + x);
    uint32_t src = 4 + x + 4;
    if (n == 0 || n > 256 || src + 2 * (uint32_t)n > b->size) return -1;
    for (int i = 0; i < 256; i++) out[i] = 0;
    for (int i = 1; i < n; i++) {
        uint16_t c = u16(b->d, src + 2 * i);
        uint32_t r = (c & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31, bl = ((c >> 10) & 31) * 255 / 31;
        out[i] = 0xff000000u | r << 16 | g << 8 | bl;
    }
    if (n < 256) for (int i = n; i < 256; i++) out[i] = out[i - n];
    out[0] = 0;
    /* FX2: the PC build draws sprites into an 8-bit screen whose palette is the level CIF palette; a bank's
     * colours only cover entries below 0x80 and 0x80..0xff always show the level palette. Most banks carry a copy
     * of it there (within 5-bit rounding), but stub banks (THUGS, THCHAR, RockT, Spy...) leave it zero, which drew
     * them as black silhouettes. */
    if (lvl_pal_set && b->levelpal) for (int i = 0x80; i < 256; i++)
        out[i] = 0xff000000u | (uint32_t)lvl_pal[i][0] << 16 | (uint32_t)lvl_pal[i][1] << 8 | lvl_pal[i][2];
    return 0;
}

typedef struct { int x, y, w, h; uint8_t px[128 * 128]; } Piece;

static const Image *decode(Bank *b, int i, int palo, Image *img);
static const uint32_t *fx_cpal;   /* FX: palette for decode(palo == -2) */

const Image *bank_frame(Bank *b, int i)
{
    bank_flush(b);   /* FX2 */
    if (i < 1 || i > b->count) return NULL;
    if (b->done[i]) return b->cache[i].px ? &b->cache[i] : NULL;
    b->done[i] = 1;
    return decode(b, i, -1, &b->cache[i]);
}

const Image *bank_frame_pal(Bank *b, int i, int pal)     /* LEFTOVERS */
{
    bank_flush(b);   /* FX2 */
    if (pal < 0 || pal >= 32) return bank_frame(b, i);
    if (i < 1 || i > b->count) return NULL;
    if (!b->pcache[pal]) { b->pcache[pal] = calloc(b->count + 1, sizeof(Image)); b->pdone[pal] = calloc(b->count + 1, 1); }
    if (!b->pcache[pal] || !b->pdone[pal]) return bank_frame(b, i);
    if (b->pdone[pal][i]) return b->pcache[pal][i].px ? &b->pcache[pal][i] : NULL;
    b->pdone[pal][i] = 1;
    return decode(b, i, pal, &b->pcache[pal][i]);
}

static const Image *decode(Bank *b, int i, int palo, Image *img)
{
    const uint8_t *d = b->d;
    uint32_t e = u32(d, 4 + 4 * (uint32_t)i);
    if (!e) return NULL;
    uint32_t f = (e >> 8) + 4;
    if (f + 16 > b->size) return NULL;
    int n = d[f];
    if (!n) return NULL;
    int nhit = (int8_t)d[f + 1];
    uint32_t p = f + 2 + (uint32_t)(nhit < 0 ? 0 : nhit) * 6;
    int footx = (int8_t)d[p + 4], footy = (int8_t)d[p + 5];
    uint32_t desc = p + 6, s = desc + (uint32_t)n * 4;
    uint32_t pal[256];
    if (palo == -2 && fx_cpal) memcpy(pal, fx_cpal, sizeof pal);                     /* FX */
    else if (palette(b, palo >= 0 ? palo : (int)(e & 0x1f), pal) && palette(b, 0, pal)) return NULL;

    static uint8_t slot[32 * 32];
    int sw = 0, sh = 0;
    Piece *pcs = malloc(sizeof(Piece) * n);
    int np = 0;
    for (int k = 0; k < n; k++) {
        if (desc + 4 * k + 4 > b->size) break;
        int b0 = d[desc + 4 * k], b1 = d[desc + 4 * k + 1];
        int x = (int8_t)d[desc + 4 * k + 2], y = (int8_t)d[desc + 4 * k + 3];
        int u = 0, v = 0;
        if (!(b0 & 0x80)) {
            sw = ((b0 & 3) + 1) * 8; sh = (((b0 >> 2) & 3) + 1) * 8;
            int o = 0;
            memset(slot, 0, sizeof slot);
            while (s < b->size) {
                int c = d[s++];
                if (c == 0 || c >= 0x80) { if (o < sw * sh) slot[o] = (uint8_t)c; o++; }
                else if (c == 2) break;
                else { int val = s < b->size ? d[s++] : 0; for (int r = 0; r < c; r++, o++) if (o < sw * sh) slot[o] = (uint8_t)val; }
            }
        } else {
            u = (b0 & 15) * 8; v = ((b0 >> 4) & 7) * 8;
        }
        Piece *P = &pcs[np++];
        P->w = (b1 & 15) * 8; P->h = (b1 >> 4) * 8;
        P->x = footx + x * 2; P->y = footy + y * 2;
        for (int yy = 0; yy < P->h; yy++)
            for (int xx = 0; xx < P->w; xx++) {
                int sx = u + xx, sy = v + yy;
                P->px[yy * P->w + xx] = (sx < sw && sy < sh) ? slot[sy * sw + sx] : 0;
            }
    }
    if (!np) { free(pcs); return NULL; }
    int x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20);
    for (int k = 0; k < np; k++) {
        if (pcs[k].x < x0) x0 = pcs[k].x;
        if (pcs[k].y < y0) y0 = pcs[k].y;
        if (pcs[k].x + pcs[k].w > x1) x1 = pcs[k].x + pcs[k].w;
        if (pcs[k].y + pcs[k].h > y1) y1 = pcs[k].y + pcs[k].h;
    }
    img->w = x1 - x0; img->h = y1 - y0;
    img->ox = -x0; img->oy = -y0;
    if (img->w <= 0 || img->h <= 0) { free(pcs); return NULL; }
    img->px = calloc((size_t)img->w * img->h, 4);
    for (int k = 0; k < np; k++) {
        Piece *P = &pcs[k];
        for (int yy = 0; yy < P->h; yy++)
            for (int xx = 0; xx < P->w; xx++) {
                uint32_t c = pal[P->px[yy * P->w + xx]];
                if (c) img->px[(size_t)(P->y - y0 + yy) * img->w + (P->x - x0 + xx)] = c;
            }
    }
    free(pcs);
    return img;
}

/* FX: see sprite.h */
const Image *bank_frame_cpal(Bank *b, int i, int key, const uint32_t pal[256])
{
    if (key < 0 || key >= 16 || !pal) return bank_frame(b, i);
    if (i < 1 || i > b->count) return NULL;
    if (!b->ccache[key]) { b->ccache[key] = calloc(b->count + 1, sizeof(Image)); b->cdone[key] = calloc(b->count + 1, 1); }
    if (!b->ccache[key] || !b->cdone[key]) return bank_frame(b, i);
    if (b->cdone[key][i]) return b->ccache[key][i].px ? &b->ccache[key][i] : NULL;
    b->cdone[key][i] = 1;
    fx_cpal = pal;
    const Image *r = decode(b, i, -2, &b->ccache[key][i]);
    fx_cpal = NULL;
    return r;
}
