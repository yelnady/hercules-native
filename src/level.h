/* Eurocom MAP_PSX.BIN side-scrolling levels (ENG1.DLL engine).
 * Ported from ENG1 FUN_10020670 (header), FUN_100208f0 (sections), FUN_1002df20/FUN_1002ed10 (block meshes),
 * FUN_1002e3b0 (path segment transform), FUN_10020990/FUN_10020b50 (collision heightfields). */
#pragma once
#include <stdint.h>
#include "fs.h"
#include "eng3.h"

typedef struct { float x, y, z; } Vec3;

typedef struct {
    Vec3 p[4];          /* world-space quad corners (PSX Z-order: 0 1 / 2 3) */
    Vec3 org;           /* cell origin, used for ordering-table depth and fog */
    uint8_t uv[4][2];
    uint16_t tpage;
    uint8_t flags;      /* 0x20 semi-transparent, 0x80 flat colour */
    uint8_t otb;        /* per-face ordering-table bias */
    uint8_t mirror;     /* cell is flipped: back-face test inverts */
    uint8_t light;      /* RENDERFIX: ENG1 map faces only: PC lighting level = R byte of the face colour word (opaque faces ignore it) */
} Face;

typedef struct {
    int32_t x, z;       /* fixed point, 4096 = one vertex unit */
    uint32_t angle;     /* 4096 = full turn */
    uint32_t w3;        /* bits 16-29 first column, 30-31 type */
} Segment;

typedef struct Level {
    const uint8_t *d;
    uint32_t size;
    char name[16];
    int index;          /* engine level number (ENG1 level table) */
    int mode;           /* 0 side-scroller, 1/2 arena rings */
    int fogmax, fogshift;
    uint8_t bg[3];

    uint8_t *vram8;     /* 2048 x 512 bytes (PSX VRAM viewed as 8-bit texels) */
    uint8_t pal[256][3];
    const uint8_t *shade;   /* 31 x 256 shade remaps (index 0 = level 1) */
    const uint8_t *modtab;  /* RENDERFIX: 256 x 256 colour modulation (lighting) table: [level][texel] -> palette index, level 0x80 = identity */

    int nstrips, ncols, nrows;
    int strip_z[16];
    uint32_t *grid;     /* [strip][col][row] cell words, merged from all streamed sections */

    const Segment *segs; int nsegs;
    const uint8_t *attr; int nattr;   /* 4 bytes: u16 segment, u16 lighting */
    const uint8_t *lighttab;          /* RENDERFIX: DAT_10070d50, map header rel(p+8): 4 corner bytes per light index */
    uint32_t lighttab_end;
    uint32_t blocks;    /* file offset of the block table */
    int nblocks;

    uint32_t attrshapes;    /* file offset of the collision-attribute shape table (ENG1 DAT_10070d40) */
    uint16_t flags[128];    /* attribute id -> behaviour bits (ENG1 per-level table): 1 solid, 0x3000 ledge */

    Face *faces; int nfaces;
    int *bstart, *bcount;   /* per (strip, column) face buckets */
    int *colseg;            /* segment index for each column */
    int eng3;               /* ENG3 map (strips = paths, cols = rows) */
    Eng3Map *e3;
} Level;

typedef struct { const char *name; int index; } LevelInfo;
extern const LevelInfo LEVELS[];
extern const int NLEVELS;

int level_load(Level *L, const Fs *fs, const char *name);
void level_free(Level *L);
static inline uint32_t level_cell(const Level *L, int strip, int col, int row)
{
    if (strip < 0 || strip >= L->nstrips || col < 0 || col >= L->ncols || row < 0 || row >= L->nrows) return 0;
    return L->grid[((size_t)strip * L->ncols + col) * L->nrows + row];
}
/* Path position (vertex units along the level, depth) -> world xz and the camera heading (radians). */
void level_path_to_world(const Level *L, float px, float pz, float *wx, float *wz, float *yaw);
/* Collision: distance from (x,y,z) down to the next floor surface, or a large value if none.
 * Mirrors FUN_10020b50's downward scan. Returns the surface y in *surf. */
int level_floor(const Level *L, int x, int y, int z, int *surf);
/* Height of the floor surface in column-local coords, -1 for no surface (FUN_10020990). */
int level_cell_height(const Level *L, uint32_t cell, int xin, int zin);
int level_wrap_col(const Level *L, int col);
/* Collision attribute id at a point (FUN_10020d30) and the solid test built on it (FUN_10030010).
 * Coordinates are integer vertex units. */
int level_attr(const Level *L, int x, int y, int z);
int level_solid(const Level *L, int x, int y, int z, int mask);
/* Full probe: distance down to the next floor, distance up to the surface above the point, and whether
 * the point is embedded in solid ground (all non-air cells up to that surface). 0x7fff = none. */
void level_probe(const Level *L, int x, int y, int z, int *up, int *down, int *inside);
/* Unit vectors of the local frame at a path position: right = along the path, forward = into the screen. */
void level_path_frame(const Level *L, float x, float z, float *rx, float *rz, float *fx, float *fz);
