#include "level.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Per-level parameters from ENG1.DLL's level table (0x1005df00, 0x1c bytes per level):
 * background colour (+8), render mode (+0xb), fog max (+0x10), fog shift (+0x11). */
typedef struct { const char *name; int index, mode, fogmax, fogshift; uint8_t bg[3]; } LevelParams;
static const LevelParams PARAMS[] = {
    {"TRAINING", 1, 0, 30, 5, {0x00, 0x00, 0x00}},
    {"FOREST",   3, 0, 30, 3, {0x00, 0x40, 0x80}},
    {"THEBES",   5, 0, 30, 5, {0x00, 0x00, 0x00}},
    {"HYDRA",    6, 1,  0, 0, {0x3f, 0x53, 0x6f}},
    {"MEDUSA",   7, 2,  0, 0, {0x00, 0x00, 0x00}},
    {"TITANS",   9, 0, 30, 5, {0x00, 0x00, 0x00}},
    {"VORTEX",  11, 0, 30, 5, {0x00, 0x20, 0x00}},
    /* ENG3 levels: ENG3 table 0x10039860 (0x20 bytes per level): bg +0x10, fog max +0x1c, fog shift +0x1d */
    {"GAUNTLET", 2, 0, 29, 5, {0x42, 0x7b, 0x79}},
    {"CYCLOPS",  8, 0, 29, 5, {0x00, 0x00, 0x00}},
    {"TORMENT", 10, 0, 30, 6, {0x00, 0x00, 0x00}},
};
const LevelInfo LEVELS[] = {
    {"TRAINING", 1}, {"GAUNTLET", 2}, {"FOREST", 3}, {"THEBES", 5}, {"HYDRA", 6}, {"MEDUSA", 7},
    {"CYCLOPS", 8}, {"TITANS", 9}, {"TORMENT", 10}, {"VORTEX", 11},
};
const int NLEVELS = sizeof LEVELS / sizeof LEVELS[0];

static inline uint32_t u32(const uint8_t *d, uint32_t a) { return d[a] | d[a+1] << 8 | d[a+2] << 16 | (uint32_t)d[a+3] << 24; }
static inline uint16_t u16(const uint8_t *d, uint32_t a) { return (uint16_t)(d[a] | d[a+1] << 8); }
static inline int16_t s16(const uint8_t *d, uint32_t a) { return (int16_t)u16(d, a); }
static inline uint32_t rel(const uint8_t *d, uint32_t a) { return a + (u32(d, a) & ~3u); }

/* Collision sample resolutions (ENG1 0x1005e0d0): {index multiplier, fraction mask, fraction scale, row stride-1}. */
static const int CT[5][4] = {{4, 63, 4, 1}, {8, 31, 8, 2}, {16, 15, 16, 4}, {32, 7, 32, 8}, {64, 3, 64, 16}};

static void blit_vram(Level *L, int x, int y, int w, int h, uint32_t off)
{
    /* 16-bit VRAM words; the 8-bit texel view is 2 bytes per word. Images may run past the resident data. */
    for (int r = 0; r < h; r++) {
        uint32_t src = off + (uint32_t)r * w * 2;
        if (src >= L->size) break;
        uint32_t n = (uint32_t)w * 2;
        if (src + n > L->size) n = L->size - src;
        memcpy(L->vram8 + (size_t)(y + r) * 2048 + x * 2, L->d + src, n);
    }
}

int level_wrap_col(const Level *L, int col)
{
    if (L->mode) return (col & 63) + 2;     /* arena rings: 64 columns, offset 2 (FUN_1002da90) */
    return col;
}

/* RENDERFIX: map lighting.  The ramp table DAT_1006f4e8 (FUN_10005a60, default ambient 0 / light 0x80 / fade 0x80) has 8 ramps x
 * 64 colour words; ramp k varies channel R when bit 0 of k is set (R = min(255, 4 * i)) and is the constant level colour
 * (DAT_10070cf0 = map header +0x2c) otherwise.  Smooth cells (light word bit 3 clear) interpolate 4 corner bytes of the light
 * table per vertex (FUN_1002ed10); flat cells use one entry (FUN_1002df20).  The face colour word is vertex idx0's colour; the PC
 * rasteriser uses only its R byte, and only semi-transparent faces honour it (FUN_0040e680 -> FUN_0040dd70 modulation; the opaque
 * path FUN_0040d420/0040cc50 ignores it). */
static int ramp_r(int idx, int hdr)
{
    int k = (idx >> 6) & 7, i = idx & 63;
    if (!(k & 1)) return hdr;
    int v = (i * 0x8000) >> 13;
    return v > 255 ? 255 : v;
}

static int cell_light(const Level *L, int lightword, int vx, int vy, int flip, int hdr)
{
    int k = lightword & 7;
    if (lightword & 8) return ramp_r(k * 0x40 + ((lightword & 0x3fc0) >> 6), hdr);
    uint32_t o = (uint32_t)(lightword >> 4) * 4;
    if (!L->lighttab || (L->lighttab + o + 4) > L->d + L->size) return 0x80;
    int b0 = L->lighttab[o], b1 = L->lighttab[o + 1], b2 = L->lighttab[o + 2], b3 = L->lighttab[o + 3], v;
    if (flip) {
        int t = ((vx * (b0 - b1)) >> 6) + b1;
        v = (((((((b2 - b3) * vx) >> 6) - t + b3) * (0x20 - vy)) >> 6) + t) >> 2;
    } else {
        int a = vx * (b1 - b0), t = ((a + ((a >> 31) & 0x3f)) >> 6) + b0;
        int c = (b3 - b2) * vx;
        int e = ((((c + ((c >> 31) & 0x3f)) >> 6) - t + b2) * (0x20 - vy));
        v = (((e + ((e >> 31) & 0x3f)) >> 6) + t) >> 2;
    }
    return ramp_r(k * 0x40 + v, hdr);
}

static void build_faces(Level *L);

int level_load(Level *L, const Fs *fs, const char *name)
{
    memset(L, 0, sizeof *L);
    char path[128];
    snprintf(path, sizeof path, "O:\\%s\\MAP_PSX.BIN", name);
    L->d = fs_get(fs, path, &L->size);
    snprintf(path, sizeof path, "O:\\%s\\MAP_PSX.CIF", name);
    uint32_t cifsz = 0;
    const uint8_t *cif = fs_get(fs, path, &cifsz);
    if (!L->d || !cif || cifsz < 768 + 0x50000 + 31 * 256) {
        fprintf(stderr, "level %s: missing map or palette\n", name);
        return -1;
    }
    snprintf(L->name, sizeof L->name, "%s", name);
    for (size_t i = 0; i < sizeof PARAMS / sizeof PARAMS[0]; i++)
        if (!strcmp(PARAMS[i].name, name)) {
            L->index = PARAMS[i].index; L->mode = PARAMS[i].mode;
            L->fogmax = PARAMS[i].fogmax; L->fogshift = PARAMS[i].fogshift;
            memcpy(L->bg, PARAMS[i].bg, 3);
        }
    memcpy(L->pal, cif, 768);
    /* FXL: the PC "clear" is not an RGB fill.  ENG1 FUN_10005e00 / ENG3 copy the level colour (table +8..+10) into the DRAWENV
     * r0,g0,b0; HERCULES.EXE DrawOTag (0x40ac60, isbg = env+0x18) calls 0x406ee0, which maps (r,g,b) to the NEAREST ENTRY OF THE
     * 256-colour palette (0x406850: squared RGB distance, first minimum wins, exact match stops) and memsets the 8-bit frame
     * buffer with that index.  Black (TRAINING, THEBES, MEDUSA, TITANS, CYCLOPS, TORMENT) stays index 0 = black; the coloured
     * skies snap to the palette (FOREST 0,64,128 -> idx 232 = 14,74,111; HYDRA -> 66,82,111; VORTEX -> 0,31,0; GAUNTLET ->
     * 63,116,129).  FXL_NOSNAP=1 keeps the raw table colour. */
    if (!getenv("FXL_NOSNAP")) {
        int bestd = -1, besti = 0;
        for (int i = 0; i < 256; i++) {
            int dr = L->bg[0] - cif[i * 3], dg = L->bg[1] - cif[i * 3 + 1], db = L->bg[2] - cif[i * 3 + 2];
            int dd = dr * dr + dg * dg + db * db;
            if (bestd < 0 || dd < bestd) { bestd = dd; besti = i; }
            if (!dd) break;
        }
        L->bg[0] = cif[besti * 3]; L->bg[1] = cif[besti * 3 + 1]; L->bg[2] = cif[besti * 3 + 2];
    }
    L->shade = cif + 768 + 0x40000 + 0x10000;
    L->modtab = cif + 768 + 0x40000;

    const uint8_t *d = L->d;
    L->vram8 = calloc(512 * 2048, 1);
    if (!memcmp(d, "i1.a", 4)) {            /* LEVELS: ENG3 map, header fields shifted by 4 */
        blit_vram(L, 0, 0, 768, 512, rel(d, 0x18));
        blit_vram(L, 768, 256, 256, 256, rel(d, 0x1c));
        return eng3_load(L);
    }
    blit_vram(L, 0, 0, 768, 512, rel(d, 0x14));
    blit_vram(L, 768, 256, 256, 256, rel(d, 0x18));

    int nlayers = u16(d, 8);
    uint32_t lp = rel(d, 0x30);                 /* layer 0 */
    L->nstrips = d[lp];
    int nsections = d[lp + 2];
    L->ncols = u16(d, lp + 4);
    L->nrows = u16(d, lp + 6);
    if (L->nstrips > 16) L->nstrips = 16;
    for (int i = 0, z = 0; i < L->nstrips; i++) { z += (int)u32(d, lp + 12 + 4 * i); L->strip_z[i] = z; }

    uint32_t p = 0x30 + 4 * (uint32_t)nlayers;
    uint32_t segoff = rel(d, p), attroff = rel(d, p + 4), lightoff = rel(d, p + 8);
    L->segs = (const Segment *)(d + segoff);
    L->nsegs = (int)((attroff - segoff) / 16);
    L->attr = d + attroff;
    L->nattr = (int)((lightoff - attroff) / 4);
    L->lighttab = d + lightoff; L->lighttab_end = rel(d, p + 12);
    L->blocks = p + 16;
    L->attrshapes = L->blocks + 4 * ((uint32_t)u16(d, 0xa) + u16(d, 0xc) + u16(d, 0xe) + u16(d, 0x10) + u16(d, 0x12));
    L->flags[1] = 1;    /* until the game supplies the level's table: id 1 = solid */

    /* Merge every streamed 2048-byte section into one resident grid. */
    size_t gsz = (size_t)L->nstrips * L->ncols * L->nrows;
    L->grid = calloc(gsz, 4);
    uint8_t *filled = calloc((size_t)L->nstrips * L->ncols, 1);
    uint32_t sect = lp + 12 + (uint32_t)d[lp] * 4;
    for (int si = 0; si < nsections; si++) {
        uint32_t b = (uint32_t)u16(d, sect + 12 * si) * 2048;
        if (b + 64 > L->size) continue;
        int start = s16(d, b);
        int widths[16];
        for (int j = 0; j < L->nstrips; j++) widths[j] = s16(d, b + 2 + 2 * j);
        uint32_t off = b + (uint32_t)((d[lp] + 2) & ~1) * 2;
        for (int j = 0; j < L->nstrips; j++) {
            for (int c = 0; c < widths[j]; c++) {
                int col = start + c;
                uint32_t src = off + (uint32_t)c * L->nrows * 4;
                if (col >= 0 && col < L->ncols && !filled[j * L->ncols + col] && src + L->nrows * 4 <= L->size) {
                    filled[j * L->ncols + col] = 1;
                    for (int r = 0; r < L->nrows; r++)
                        L->grid[((size_t)j * L->ncols + col) * L->nrows + r] = u32(d, src + 4 * r);
                }
            }
            off += (uint32_t)widths[j] * L->nrows * 4;
        }
    }
    free(filled);

    /* Segment for each column (used by the camera and actors). */
    L->colseg = calloc(L->ncols, sizeof(int));
    for (int c = 0; c < L->ncols; c++) {
        L->colseg[c] = c > 0 ? L->colseg[c - 1] : 0;
        for (int s = 0; s < L->nstrips; s++) {
            int found = 0;
            for (int r = 0; r < L->nrows; r++) {
                uint32_t cell = level_cell(L, s, c, r);
                if (!(cell & 0x7fe)) continue;
                int a = (cell >> 11) & 0x1fff;
                if (a < L->nattr) { int sg = u16(L->attr, 4 * a); if (sg < L->nsegs) { L->colseg[c] = sg; found = 1; break; } }
            }
            if (found) break;
        }
    }
    build_faces(L);
    return 0;
}

void level_free(Level *L)
{
    eng3_free(L);
    free(L->vram8); free(L->grid); free(L->faces); free(L->bstart); free(L->bcount); free(L->colseg);
    memset(L, 0, sizeof *L);
}

#define RING_R 652.0f   /* 0x28c: arena ring radius (ENG1 FUN_1002e7a0 / FUN_1002f340) */
#define ANG (float)(2 * M_PI / 4096.0)

/* Local cell coordinates -> world xz.
 * lx: distance along the path from the segment's first column; lz: depth into the screen.
 * Straight segments (type 0) translate/rotate; ring segments (type 1) bend onto a cylinder whose
 * radius shrinks with depth (vertex radius = 0x28c - z), one column per 64 angle units. */
static void local_to_world(const Level *L, int segi, float lx, float lz, float *wx, float *wz)
{
    const Segment *sg = &L->segs[segi >= 0 && segi < L->nsegs ? segi : 0];
    float px = sg->x / 4096.0f, pz = sg->z / 4096.0f;
    int type = sg->w3 >> 30;
    float a = (float)(sg->angle & 0xfff) * ANG;
    if (!type) {
        float c = cosf(a), s = sinf(a);
        *wx = px + lx * c + lz * s;
        *wz = pz + lx * s - lz * c;
        return;
    }
    float ts = a - 32 * ANG;                    /* stored angle is mid-column */
    float cx = px - RING_R * sinf(ts), cz = pz + RING_R * cosf(ts);
    float t = ts + lx * ANG;
    float r = type == 1 ? RING_R - lz : RING_R + lz;
    *wx = cx + r * sinf(t);
    *wz = cz - r * cosf(t);
}

static int seg_col0(const Level *L, int segi, int *type)
{
    const Segment *sg = &L->segs[segi >= 0 && segi < L->nsegs ? segi : 0];
    *type = sg->w3 >> 30;
    return (sg->w3 >> 16) & 0x3fff;
}

static void build_faces(Level *L)
{
    const uint8_t *d = L->d;
    int nb = L->nstrips * L->ncols;
    L->bstart = calloc(nb, sizeof(int));
    L->bcount = calloc(nb, sizeof(int));
    int cap = 1 << 16;
    L->faces = malloc(sizeof(Face) * cap);
    uint32_t texbase = rel(d, 0x28);
    int hdrlight = d[0x2c];     /* DAT_10070cf0 base level colour */
    uint32_t bt = L->blocks;
    for (int s = 0; s < L->nstrips; s++)
        for (int col = 0; col < L->ncols; col++) {
            int bi = s * L->ncols + col;
            L->bstart[bi] = L->nfaces;
            for (int r = 0; r < L->nrows; r++) {
                uint32_t cell = level_cell(L, s, col, r);
                int b = (cell >> 1) & 0x3ff;
                if (!b) continue;
                uint32_t ea = bt - 4 + (uint32_t)b * 4;
                if (ea + 4 > L->size) continue;
                int i = s16(d, ea) + b;
                uint32_t a = bt - 4 + (uint32_t)i * 4;
                if (a + 8 > L->size) continue;
                int nv = d[a + 2], nf = d[a + 3];
                int big = s16(d, a) == 1;
                if (!nv || a + 8 + nv * 4 + nf * 8 > L->size) continue;
                int flip = cell & 1;
                int attr = (cell >> 11) & 0x1fff;
                int segi = attr < L->nattr ? u16(L->attr, 4 * attr) : 0;
                int type, col0 = seg_col0(L, segi, &type);
                float lxbase = type ? 0.0f : (float)(col - col0) * 64.0f;
                Vec3 V[64]; int VX[64], VY[64];
                for (int k = 0; k < nv && k < 64; k++) {
                    uint32_t v = u32(d, a + 4 + 4 * k);
                    int vx = (int8_t)(v & 0xff), vy = (int16_t)((v >> 8) & 0xffff), vz = (int8_t)(v >> 24);
                    VX[k] = vx; VY[k] = vy;
                    float lx = lxbase + (flip ? vx : 64 - vx);
                    float lz = (float)(L->strip_z[s] + vz);
                    float ly = (float)(r * 64 + vy);
                    if (type && big) {
                        /* Ring backdrop panels (FUN_1002f120): double width in angle and double height. */
                        lx = (float)(64 - vx) * 2;
                        ly = (float)(r * 64 + 2 * vy + 32);
                    }
                    local_to_world(L, segi, lx, lz, &V[k].x, &V[k].z);
                    V[k].y = ly;
                }
                Vec3 org;
                /* The ordering-table depth comes from the cell ORIGIN (FUN_1002e3b0/1002e7a0 project vertex (0,0,strip_z) of the cell frame,
                 * the cell edge where original x = 0, i.e. lx = 64 in our mirrored axis), not the cell centre. */
                local_to_world(L, segi, type && big ? 128.0f : lxbase + 64, (float)L->strip_z[s], &org.x, &org.z);
                org.y = (float)(r * 64 + 32);
                uint32_t fa = a + 8 + nv * 4;
                for (int k = 0; k < nf; k++) {
                    uint32_t f0 = u32(d, fa + 8 * k), f1 = u32(d, fa + 8 * k + 4);
                    int idx[4] = {f0 & 63, (f0 >> 6) & 63, (f0 >> 12) & 63, (f0 >> 18) & 63};
                    if (idx[0] >= nv || idx[1] >= nv || idx[2] >= nv || idx[3] >= nv) continue;
                    if (L->nfaces == cap) { cap *= 2; L->faces = realloc(L->faces, sizeof(Face) * cap); }
                    Face *F = &L->faces[L->nfaces++];
                    for (int q = 0; q < 4; q++) F->p[q] = V[idx[q]];
                    F->org = org;
                    F->flags = (uint8_t)(f0 >> 24);
                    F->otb = (uint8_t)(f1 >> 24);
                    F->mirror = (uint8_t)flip;
                    F->light = (uint8_t)cell_light(L, attr < L->nattr ? u16(L->attr, 4 * attr + 2) : 0x38, VX[idx[0]], VY[idx[0]], flip, hdrlight);
                    if (F->flags & 0x80) {
                        /* Untextured: the PC port samples a palette strip in VRAM at (colour, 255) of tpage 0x9c. */
                        for (int q = 0; q < 4; q++) { F->uv[q][0] = (uint8_t)f1; F->uv[q][1] = 0xff; }
                        F->tpage = 0x9c;
                    } else {
                        uint32_t t = texbase + (f1 & 0xffff);
                        if (t + 12 > L->size) { L->nfaces--; continue; }
                        uint32_t e0 = u32(d, t), e1 = u32(d, t + 4), e2 = u32(d, t + 8);
                        F->uv[0][0] = e0 & 0xff; F->uv[0][1] = (e0 >> 8) & 0xff;
                        F->uv[1][0] = e1 & 0xff; F->uv[1][1] = (e1 >> 8) & 0xff;
                        F->uv[2][0] = e2 & 0xff; F->uv[2][1] = (e2 >> 8) & 0xff;
                        F->uv[3][0] = (e2 >> 16) & 0xff; F->uv[3][1] = (e2 >> 24) & 0xff;
                        F->tpage = (uint16_t)(e1 >> 16);
                    }
                }
            }
            L->bcount[bi] = L->nfaces - L->bstart[bi];
        }
}

static void path_local(const Level *L, float x, int *segi, float *lx)
{
    int col = (int)floorf(x / 64.0f);
    float frac = x - col * 64.0f;
    int gc = level_wrap_col(L, col);
    if (gc < 0) gc = 0;
    if (gc >= L->ncols) gc = L->ncols - 1;
    *segi = L->colseg[gc];
    int type, col0 = seg_col0(L, *segi, &type);
    *lx = type ? frac : (float)(gc - col0) * 64.0f + frac;
}

void level_path_to_world(const Level *L, float x, float z, float *wx, float *wz, float *yaw)
{
    int segi; float lx;
    path_local(L, x, &segi, &lx);
    local_to_world(L, segi, lx, z, wx, wz);
    if (yaw) *yaw = (float)(L->segs[segi].angle & 0xfff) * ANG;
}

void level_path_frame(const Level *L, float x, float z, float *rx, float *rz, float *fx, float *fz)
{
    int segi; float lx, x0, z0, x1, z1, x2, z2;
    path_local(L, x, &segi, &lx);
    local_to_world(L, segi, lx, z, &x0, &z0);
    local_to_world(L, segi, lx + 1, z, &x1, &z1);
    local_to_world(L, segi, lx, z + 1, &x2, &z2);
    float a = hypotf(x1 - x0, z1 - z0), b = hypotf(x2 - x0, z2 - z0);
    *rx = (x1 - x0) / a; *rz = (z1 - z0) / a;
    *fx = (x2 - x0) / b; *fz = (z2 - z0) / b;
}

int level_cell_height(const Level *L, uint32_t cell, int xin, int zin)
{
    int b = (cell >> 1) & 0x3ff;
    if (!b) return -1;
    uint32_t ea = L->blocks - 2 + (uint32_t)b * 4;
    if (ea + 2 > L->size) return -1;
    int o = s16(L->d, ea);
    if (!o) return -1;
    const int8_t *p = (const int8_t *)(L->d + ea + o);
    if ((const uint8_t *)p + 40 > L->d + L->size) return -1;
    if (cell & 1) xin = 63 - xin;
    unsigned n = (uint8_t)p[0];
    const int8_t *q = p + 1;
    #define INTERP(v0, v1, fr, ix) ((((((v1) & 0x3f) * 2 - (v0) * 2) * (((fr) & CT[ix][1]) * CT[ix][2])) >> 8) + (v0) * 2)
    if (n == 0) {
        int v0 = q[0];
        if (v0 & 0x80) return -1;
        if (v0 & 0x40) return (v0 & 0x3f) << 1;
        return INTERP(v0 & 0x3f, q[1], xin, 0);
    }
    int xi = n & 15;
    if (xi > 4) return -1;
    if (n & 0xf0) {
        int zi = (int)(n >> 4) - 1;
        if (zi < 0 || zi > 4) return -1;
        q += ((CT[zi][0] * zin) >> 8) * (CT[xi][3] + 1) + ((CT[xi][0] * xin) >> 8);
        int v0 = (uint8_t)q[0];
        if (v0 & 0x80) return -1;
        if (v0 & 0x40) return (v0 & 0x3f) << 1;
        int a = INTERP(v0 & 0x3f, q[1], xin, xi);
        int w0 = q[1 + CT[xi][3]] & 0x3f;
        int bb = INTERP(w0, q[2 + CT[xi][3]], xin, xi);
        return ((((zin & CT[zi][1]) * CT[zi][2]) * (bb - a)) >> 8) + a;
    }
    q += (CT[xi][0] * xin) >> 8;
    int v0 = (uint8_t)q[0];
    if (v0 & 0x80) return -1;
    if (v0 & 0x40) return (v0 & 0x3f) << 1;
    return INTERP(v0 & 0x3f, q[1], xin, xi);
    #undef INTERP
}

int level_floor(const Level *L, int x, int y, int z, int *surf)
{
    int strip = (z + 0x3f) >> 7;
    if (strip < 0) strip = 0;
    if (strip >= L->nstrips) strip = L->nstrips - 1;
    int col = x >> 6;
    int gc = level_wrap_col(L, col);
    if (gc < 0 || gc >= L->ncols) return 0x7fff;
    int xin = x & 63;
    int zin = 0x3f - (((z + 0x3f) >> 1) & 0x3f);
    int yy = y - 32;
    int row = yy >> 6;
    if (row < 0) row = 0;
    for (; row < L->nrows; row++) {
        uint32_t cell = level_cell(L, strip, gc, row);
        if (!(cell & 0x7fe)) continue;
        int h = level_cell_height(L, cell, xin, zin);
        if (h < 0) continue;
        int dist = row * 64 - h - yy;
        if (dist >= 0) { if (surf) *surf = row * 64 - h + 32; return dist; }
    }
    return 0x7fff;
}

/* FUN_10020b50: scan down from the point for the next floor (down = distance to it), then scan up from
 * there for the nearest surface above the point (up = how far the point is below it). *inside is set when
 * every cell between that surface and the point is solid (no air bit), i.e. the point is embedded. */
void level_probe(const Level *L, int x, int y, int z, int *up, int *down, int *inside)
{
    *up = *down = 0x7fff;
    if (inside) *inside = 0;
    int strip = (z + 0x3f) >> 7;
    if (strip < 0) strip = 0;
    if (strip >= L->nstrips) strip = L->nstrips - 1;
    int gc = level_wrap_col(L, x >> 6);
    if (gc < 0 || gc >= L->ncols) return;
    int xin = x & 63;
    int zin = 0x3f - (((z + 0x3f) >> 1) & 0x3f);
    int yy = y - 32;
    int row = yy >> 6;
    if (row < 0) row = 0;
    if (row >= L->nrows) row = L->nrows - 1;
    int r = row;
    for (; r < L->nrows; r++) {
        uint32_t cell = level_cell(L, strip, gc, r);
        if (!(cell & 0x7fe)) continue;
        int h = level_cell_height(L, cell, xin, zin);
        if (h < 0) continue;
        int dist = r * 64 - h - yy;
        if (dist >= 0) { *down = dist; break; }
    }
    if (r >= L->nrows) r = L->nrows - 1;
    int solid = 1;
    for (; r >= 0; r--) {
        uint32_t cell = level_cell(L, strip, gc, r);
        int h = (cell & 0x7fe) ? level_cell_height(L, cell, xin, zin) : -1;
        if (h >= 0 && h + yy - r * 64 >= 0) {
            *up = h + yy - r * 64;
            if (inside) *inside = solid;
            return;
        }
        if (r <= row && (cell & 0x80000000u)) solid = 0;
    }
}

int level_attr(const Level *L, int x, int y, int z)
{
    int strip = (z + 0x40) >> 7;
    if (strip < 0) strip = 0;
    if (strip >= L->nstrips) strip = L->nstrips - 1;
    int gc = level_wrap_col(L, x >> 6);
    if (gc < 0 || gc >= L->ncols) return 0;
    int yy = y + 0x20;
    int row = yy >> 6;
    if (yy < 0 || row >= L->nrows) return 0;
    uint32_t cell = level_cell(L, strip, gc, row);
    int a = (cell >> 24) & 0x7f;
    if (!a) return 0;
    uint32_t e = L->attrshapes - 4 + (uint32_t)a * 4;
    if (e + 4 > L->size) return 0;
    uint32_t p = e + u32(L->d, e);
    if (p + 2 > L->size) return 0;
    unsigned hdr = u16(L->d, p);
    p += 2;
    unsigned xr = hdr & 15, yr = (hdr & 0xff) >> 4, zr = hdr >> 8;
    if (xr > 4 || yr > 4 || zr > 4) return 0;
    uint32_t off = 0;
    if (hdr) {
        if (xr) off += (uint32_t)((CT[xr][0] * (x & 63)) >> 8);
        if (yr) off += (uint32_t)(((CT[yr][0] * (yy & 63)) >> 8) * CT[xr][3]);
        if (zr) off += (uint32_t)(((CT[zr][0] * (((z + 0x3f) >> 1) & 0x3f)) >> 8) * CT[xr][3] * CT[yr][3]);
    }
    if (p + off >= L->size) return 0;
    return (int8_t)L->d[p + off];
}

int level_solid(const Level *L, int x, int y, int z, int mask)
{
    int a = level_attr(L, x, y, z);
    if (a < 0 || a > 127) return 0;
    uint16_t f = L->flags[a];
    if (f & 0x8000) return 0;
    return f & mask;
}
