#include "eng3.h"
#include "level.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static inline uint32_t u32(const uint8_t *d, uint32_t a) { return d[a] | d[a+1] << 8 | d[a+2] << 16 | (uint32_t)d[a+3] << 24; }
static inline uint16_t u16(const uint8_t *d, uint32_t a) { return (uint16_t)(d[a] | d[a+1] << 8); }
static inline int16_t s16(const uint8_t *d, uint32_t a) { return (int16_t)u16(d, a); }
static inline uint32_t rel(const uint8_t *d, uint32_t a) { return a + (u32(d, a) & ~3u); }

/* rsin-style table: 4096 = 1.0, 4096 angle units per turn (ENG3 DAT_100407b4) */
static inline int isin(int a) { return (int)lrint(4096.0 * sin((a & 0xfff) * (2 * M_PI / 4096.0))); }
static inline float fsin(float a) { return sinf(a * (float)(2 * M_PI / 4096.0)); }
static inline float fcos(float a) { return cosf(a * (float)(2 * M_PI / 4096.0)); }

uint32_t eng3_cell(const Level *L, int g, int row, int col)
{
    const Eng3Map *M = L->e3;
    if (g < 0 || g >= M->ngrids || row < 0 || row >= M->grid[g].h || col < 0 || col >= M->grid[g].w) return 1;
    return u32(L->d, M->grid[g].off + 4u * ((uint32_t)row * M->grid[g].w + col));
}

/* FUN_10022790: per-row anchor/heading records for every path. */
static void build_rows(Level *L, E3Path *P, int pi)
{
    Eng3Map *M = L->e3;
    int n = P->nrows;
    P->rows = calloc((size_t)n + 1, sizeof(E3Row));
    int64_t x = 0, z = 0;
    int h = 0;
    if (P->parent != 0x7f && P->parent < pi) {
        const E3Path *PP = &M->path[P->parent];
        int pr = P->parent_row < PP->nrows ? P->parent_row : PP->nrows - 1;
        const E3Row *r = &PP->rows[pr];
        int k = r->curve ? 0 : pr - r->base;
        x = r->x + (int64_t)isin(r->h - 0x400) * k * 0x80;
        z = r->z + (int64_t)isin(r->h) * k * 0x80;
        h = (r->h + P->angle) & 0xfff;
    }
    P->rows[0] = (E3Row){(int32_t)(x & ~0xfffLL), (int32_t)(z & ~0xfffLL), h, 0, 0, h};
    int base = 0, acc = 0, cnt = 0, step = 0x20, first = 1;
    for (int r = 1; r < n; r++) {
        int c = P->delta[r];
        int next = r + 1 < n ? P->delta[r + 1] : 0;
        int hn = h, na;
        if (c == 0) {
            P->rows[r] = (E3Row){(int32_t)(x & ~0xfffLL), (int32_t)(z & ~0xfffLL), h & 0xfff, 0, base, h & 0xfff};
            first = r + 1;
            x &= ~0xfffLL; z &= ~0xfffLL;
            step = 0x20;
            int big = cnt > 0x20;
            na = acc + 0x80;
            cnt++;
            if (big) {
                x += (int64_t)isin(h - 0x400) * (acc + 0x7e);
                z += (int64_t)isin(h) * (acc + 0x7e);
                acc = 0; cnt = 0; na = 0; base = r;
            }
        } else if (c == next) {
            hn = h + step * c;
            step = 0x40;
            x += (int64_t)isin(h - 0x400) * (acc + 0x7c);
            z += (int64_t)isin(h) * (acc + 0x7c);
            int hs = first >= 1 && first - 1 < r ? P->rows[first - 1].h : h;
            P->rows[r] = (E3Row){(int32_t)(x & ~0xfffLL), (int32_t)(z & ~0xfffLL), hn & 0xfff, c, first, hs};
            na = 4;
        } else {
            hn = h + c * 0x18;
            step = 0x18;
            x += (int64_t)isin(h - 0x400) * (acc + 0x70);
            z += (int64_t)isin(h) * (acc + 0x70);
            P->rows[r] = (E3Row){(int32_t)(x & ~0xfffLL), (int32_t)(z & ~0xfffLL), hn & 0xfff, 0, r, hn & 0xfff};
            na = 0;
            base = r;
        }
        acc = na;
        h = hn;
    }
}

/* Local (row, X lateral, Zrow depth inside the row's anchor frame) -> world xz. */
static void local_world(const E3Path *P, int r, float X, float Zin, float *wx, float *wz)
{
    if (r < 0) r = 0;
    if (r >= P->nrows) r = P->nrows - 1;
    const E3Row *R = &P->rows[r];
    if (!R->curve) {
        const E3Row *A = R;
        float Z = (float)(r - R->base) * 128.0f + Zin;
        float ax = A->x / 4096.0f, az = A->z / 4096.0f, s = fsin((float)R->h), c = fcos((float)R->h);
        *wx = ax + s * X - c * Z;
        *wz = az + c * X + s * Z;
        return;
    }
    /* FUN_10021200 / FUN_10021a10: bend around a centre E3_RADIUS to the side of the bend's first row. */
    const E3Row *A = &P->rows[R->base];
    float sg = R->curve > 0 ? 1.0f : -1.0f;
    float hs = (float)R->hs;
    float ax = A->x / 4096.0f, az = A->z / 4096.0f;
    float cx = ax + sg * E3_RADIUS * fsin(hs), cz = az + sg * E3_RADIUS * fcos(hs);
    float phi = (float)(r - R->base) * 64.0f + Zin * 0.5f;
    float rho = E3_RADIUS - sg * X;
    float a = hs + sg * phi;
    *wx = cx - sg * rho * fsin(a);
    *wz = cz - sg * rho * fcos(a);
}

void eng3_to_world(const Level *L, int path, float depth, float X, float *wx, float *wz, int *heading)
{
    const Eng3Map *M = L->e3;
    if (path < 0 || path >= M->npaths) path = 0;
    const E3Path *P = &M->path[path];
    int r = (int)floorf(depth / 128.0f);
    if (r < 0) r = 0;
    if (r >= P->nrows) r = P->nrows - 1;
    float zin = depth - r * 128.0f;
    local_world(P, r, X, zin, wx, wz);
    if (heading) {
        const E3Row *R = &P->rows[r];
        if (R->curve) *heading = (int)(R->hs + (R->curve > 0 ? 1 : -1) * ((r - R->base) * 64 + zin * 0.5f)) & 0xfff;
        else *heading = R->h;
    }
}

int eng3_floor(const Level *L, int path, float depth, float X, float y, int *above)
{
    const Eng3Map *M = L->e3;
    if (above) *above = 0x7fff;
    if (path < 0 || path >= M->npaths) return 0x7fff;
    const E3Path *P = &M->path[path];
    int D = (int)floorf(depth), lat = (int)floorf(X + 64.0f) + 0x300;
    if (D < 0 || D > 0x7fff) D = 0x80;     /* ENG3B: FUN_1001b2e0 reads row 1 for a negative (u16 > 0x7fff) depth */
    int row = D >> 7, col = lat >> 7;
    int zin = (D >> 1) & 63, xin = (lat >> 1) & 63;
    int best = 0x7fff, up = 0x7fff;
    for (int k = 0; k < P->ngrids; k++) {
        uint32_t cell = eng3_cell(L, P->g[k].grid, row, col);
        if (cell & 1) continue;
        int xi = (cell & 0x1000000) ? 63 - xin : xin;
        int h = level_cell_height(L, cell & 0x7fe, xi, zin);   /* flip already applied */
        if (h < 0) continue;
        float surf = (float)(P->g[k].yoff * 32 - 2 * h - 2 * ((int8_t)(cell >> 24) & ~1));
        int dist = (int)lrintf(surf - y);
        if (dist >= 0) { if (dist < best) best = dist; }
        else if (-dist < up) up = -dist;
    }
    if (above) *above = up;
    return best;
}

static Face *push_face(Level *L, int *cap)
{
    if (L->nfaces == *cap) { *cap *= 2; L->faces = realloc(L->faces, sizeof(Face) * *cap); }
    return &L->faces[L->nfaces++];
}

static void build_faces3(Level *L)
{
    Eng3Map *M = L->e3;
    const uint8_t *d = L->d;
    int maxrows = 0;
    for (int p = 0; p < M->npaths; p++) if (M->path[p].nrows > maxrows) maxrows = M->path[p].nrows;
    L->nstrips = M->npaths; L->ncols = maxrows; L->nrows = 1;
    int nb = L->nstrips * L->ncols;
    L->bstart = calloc(nb, sizeof(int));
    L->bcount = calloc(nb, sizeof(int));
    int cap = 1 << 16;
    L->faces = malloc(sizeof(Face) * cap);
    uint32_t texbase = rel(d, 0x2c);
    for (int p = 0; p < M->npaths; p++) {
        const E3Path *P = &M->path[p];
        for (int r = 0; r < P->nrows; r++) {
            int bi = p * L->ncols + r;
            L->bstart[bi] = L->nfaces;
            for (int k = 0; k < P->ngrids; k++)
                for (int c = 0; c < M->grid[P->g[k].grid].w; c++) {
                    uint32_t cell = eng3_cell(L, P->g[k].grid, r, c);
                    if (cell & 1) continue;
                    int b = (cell >> 1) & 0x3ff;
                    if (!b) continue;
                    uint32_t ea = M->blocks - 4 + (uint32_t)b * 4;
                    if (ea + 4 > L->size) continue;
                    uint32_t a = M->blocks - 4 + (uint32_t)(s16(d, ea) + b) * 4;
                    if (a + 8 > L->size) continue;
                    int nv = d[a + 2], nf = d[a + 3];
                    if (!nv || nv > 64 || a + 8 + nv * 4 + nf * 8 > L->size) continue;
                    int flip = (cell >> 24) & 1;
                    float ybase = (float)(P->g[k].yoff * 32 - 2 * ((int8_t)(cell >> 24) & ~1));
                    Vec3 V[64];
                    float cxs = 0, czs = 0;
                    for (int q = 0; q < nv; q++) {
                        uint32_t v = u32(d, a + 4 + 4 * q);
                        int vx = (int8_t)(v & 0xff), vy = (int16_t)((v >> 8) & 0xffff), vz = (int8_t)(v >> 24);
                        float X = (float)(c * 128 - 0x2c0) + (flip ? 2 * vx : -2 * vx);
                        local_world(P, r, X, (float)(2 * vz) , &V[q].x, &V[q].z);
                        V[q].y = ybase + 2 * vy;
                        cxs += V[q].x; czs += V[q].z;
                    }
                    Vec3 org;
                    local_world(P, r, (float)(c * 128 - 0x2c0 - 64), 64.0f, &org.x, &org.z);
                    org.y = ybase;
                    uint32_t fa = a + 8 + nv * 4;
                    for (int f = 0; f < nf; f++) {
                        uint32_t f0 = u32(d, fa + 8 * f), f1 = u32(d, fa + 8 * f + 4);
                        int idx[4] = {f0 & 63, (f0 >> 6) & 63, (f0 >> 12) & 63, (f0 >> 18) & 63};
                        if (idx[0] >= nv || idx[1] >= nv || idx[2] >= nv || idx[3] >= nv) continue;
                        Face *F = push_face(L, &cap);
                        for (int q = 0; q < 4; q++) F->p[q] = V[idx[q]];
                        F->org = org;
                        F->flags = (uint8_t)(f0 >> 24);
                        F->otb = (uint8_t)(f1 >> 24);
                        F->mirror = (uint8_t)flip;
                        if (F->flags & 0x80) {
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
                    (void)cxs; (void)czs;
                }
            L->bcount[bi] = L->nfaces - L->bstart[bi];
        }
    }
}

int eng3_load(Level *L)
{
    const uint8_t *d = L->d;
    Eng3Map *M = calloc(1, sizeof *M);
    L->e3 = M;
    L->eng3 = 1;
    M->ngrids = u16(d, 0xa);
    M->npaths = u16(d, 0xc);
    if (M->ngrids > 64 || M->npaths > E3_MAXPATHS) { fprintf(stderr, "eng3: too many grids/paths\n"); return -1; }
    for (int i = 0; i < M->ngrids; i++) {
        uint32_t a = 0x38 + 4 * i, g = a + (u32(d, a) & ~1u);
        M->grid[i].w = u16(d, g); M->grid[i].h = u16(d, g + 2); M->grid[i].off = g + 4;
    }
    uint32_t p = 0x38 + 4 * (uint32_t)M->ngrids;
    M->attr = rel(d, p + 4);
    M->blocks = p + 16;
    L->blocks = M->blocks;
    uint32_t P0 = rel(d, 0x30);
    for (int i = 0; i < M->npaths; i++) {
        uint32_t e = P0 + 16 * i;
        E3Path *P = &M->path[i];
        P->parent = d[e]; P->parent_row = d[e + 1]; P->angle = u16(d, e + 4);
        uint32_t gl = e + 6 + s16(d, e + 6);
        P->ngrids = d[gl] > 8 ? 8 : d[gl];
        for (int k = 0; k < P->ngrids; k++) { P->g[k].grid = d[gl + 1 + 2 * k]; P->g[k].yoff = (int8_t)d[gl + 2 + 2 * k]; }
        uint32_t cl = e + 8 + s16(d, e + 8);
        while (d[cl] != 0xff && P->nconn < 8) P->conn[P->nconn++] = d[cl++];
        P->delta = (const int8_t *)(d + e + 10 + s16(d, e + 10));
        P->nrows = P->ngrids ? M->grid[P->g[0].grid].h : 0;
        build_rows(L, P, i);
    }
    build_faces3(L);
    L->colseg = calloc(1, sizeof(int));
    return 0;
}

void eng3_free(Level *L)
{
    Eng3Map *M = L->e3;
    if (!M) return;
    for (int i = 0; i < M->npaths; i++) free(M->path[i].rows);
    free(M);
    L->e3 = NULL;
}
