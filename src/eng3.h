/* ENG3.DLL levels (Gauntlet, Cyclops chase, Passageways of Torment): MAP_PSX.BIN 'i1.a' maps.
 * The map is a set of horizontal cell grids laid along "paths": each path is a sequence of 128-unit rows
 * that run into the screen and bend left/right, with branch paths hanging off a parent row.
 * Ported from ENG3 FUN_1001af10 (header), FUN_10022790/10022690/10022710 (row records),
 * FUN_100204c0/FUN_100206f0 (cell drawing), FUN_10020ed0/FUN_10021a10 (straight/bent vertices),
 * FUN_10021200 (bend centre), FUN_1001b2e0/FUN_1001b100 (floor heights), FUN_10022070 (camera). */
#pragma once
#include <stdint.h>

#define E3_MAXPATHS 16
#define E3_RADIUS 1304          /* 0x518: bend radius (the camera centre line) */

typedef struct {
    int32_t x, z;               /* anchor, 4096 = one unit (FUN_10022690 stores it rounded down) */
    int h;                      /* heading, 4096 = full turn; forward = (-cos h, sin h), right = (sin h, cos h) */
    int curve;                  /* signed bend direction (0 = straight) */
    int base;                   /* straight: anchor row; bent: first row of the bend */
    int hs;                     /* bent: heading before the bend */
} E3Row;

typedef struct {
    int parent, parent_row, angle;
    int ngrids;
    struct { int grid, yoff; } g[8];
    int conn[8], nconn;
    int nrows;
    const int8_t *delta;
    E3Row *rows;
} E3Path;

typedef struct {
    int ngrids;
    struct { uint32_t off; int w, h; } grid[64];
    int npaths;
    E3Path path[E3_MAXPATHS];
    uint32_t blocks;            /* block table (same format as ENG1) */
    uint32_t attr;              /* 4-byte attribute entries (u16 flags at +2) */
} Eng3Map;

struct Level;
int eng3_load(struct Level *L);         /* called by level_load for 'i1.a' maps */
void eng3_free(struct Level *L);
uint32_t eng3_cell(const struct Level *L, int grid, int row, int col);
/* Path position (depth along the path, lateral X with 0 = centre line) -> world xz and heading. */
void eng3_to_world(const struct Level *L, int path, float depth, float X, float *wx, float *wz, int *heading);
/* FUN_1001b2e0: distance down to the floor below (y down), 0x7fff = none; *above = distance to the floor above. */
int eng3_floor(const struct Level *L, int path, float depth, float X, float y, int *above);
