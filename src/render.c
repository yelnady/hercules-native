#include <stdio.h>
#include "render.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define GEOM_H   320.0f
#define GEOM_OFX 160.0f
#define GEOM_OFY 128.0f
#define NEAR_Z   96.0f      /* FUN_1002e3b0 drops cells with SZ <= 0xbf (world = 2x vertex units) */

/* Ordering table (RENDERFIX, derived from the decompile; no tuned constants):
 *   map face slot  = 0x521 - cellz - otb      (FUN_1002df20; cellz = SZ(cell origin) >> 2, otb = face byte +0xf)
 *   object sprite  = 0x4e1 - depth            (FUN_10030790/10004010/10004330/10004650/100048b0; depth = obj+0x10)
 *   model face     = 0x4e1 - avgSZ(verts)>>2  (FUN_10031540, clamped to 0x4bf)
 * Low slots are drawn first; within a slot the LAST primitive inserted is drawn FIRST (head insertion).
 * Both cellz and depth are z/4 of GTE camera-space z (= 2 x our world z): cell origin SZ = 2*(strip_z - camz) (FUN_1002da90:
 * obj+0x38 = strip_z - d1c, TR = d1c - ca8), object SZ = 2*(objz - ca8) (FUN_10032300).  In face-key units (key = 0x521 - slot)
 * an object at depth d therefore has key d + (0x521 - 0x4e1) = d + 0x40.  Not applied to ENG3 levels (own table). */
#define SPRITE_OT_BIAS 64.0f
enum { ITEM_FACE, ITEM_SPRITE };

typedef struct {
    float key;
    int seq;            /* insertion order: later insertion within one slot is drawn first */
    int kind;
    union {
        struct { const Face *f; float sx[4], sy[4]; int shade, light; } face;
        struct { const Image *img; float x, y, s; int flip, mode, level; float ang; uint32_t tint; float ysc; } spr;   /* COMBAT: mode/level = blend; BOSSES: ang; FX: tint / ysc */
    };
} Item;

void render_init(Renderer *R, int scale)
{
    memset(R, 0, sizeof *R);
    R->scale = scale;
    R->ofy = GEOM_OFY;   /* LEVELS */
    R->w = 320 * scale; R->h = 240 * scale;
    R->fb = calloc((size_t)R->w * R->h, 4);
    R->cap = 8192;
    R->items = malloc(sizeof(Item) * R->cap);
}

void render_begin(Renderer *R, const Level *L, const Camera *cam, uint32_t bg)
{
    R->L = L;
    R->cam = *cam;
    R->nitems = 0;
    R->model_depth = -1;
    uint32_t *p = R->fb;
    for (int i = 0, n = R->w * R->h; i < n; i++) p[i] = bg;
}

static Item *push(Renderer *R)
{
    if (R->nitems == R->cap) { R->cap *= 2; R->items = realloc(R->items, sizeof(Item) * R->cap); }
    Item *it = &((Item *)R->items)[R->nitems];
    it->seq = R->nitems++;
    return it;
}

static inline void cam_space(const Camera *c, float wx, float wy, float wz, float *xc, float *yc, float *zc)
{
    float dx = wx - c->x, dz = wz - c->z;
    *xc = dx * c->rx + dz * c->rz;
    *zc = dx * c->fx + dz * c->fz;
    *yc = wy - c->y;
}

int render_project(const Renderer *R, float wx, float wy, float wz, float *sx, float *sy, float *zc)
{
    float xc, yc, z;
    cam_space(&R->cam, wx, wy, wz, &xc, &yc, &z);
    if (zc) *zc = z;
    if (z < 1.0f) return 0;
    *sx = GEOM_OFX + xc * GEOM_H / z;
    *sy = R->ofy + yc * GEOM_H / z;
    return 1;
}

static void queue_face(Renderer *R, const Face *F, int cull);
static inline float sprite_ot_bias(const Renderer *R) { return R->L && R->L->eng3 ? 0.0f : SPRITE_OT_BIAS; }

/* Callers' depth_bias values (-3.9 .. -9.5, historically tuned) only encode which of several sprites sharing one object
 * position (parent / child / shadow / trail) goes in front; in the original that comes from list order within one slot, never
 * from moving an object across slots.  Map them onto a fraction of a slot (player -8 -> 0, children -9 -> slightly in front,
 * enemies -4 -> slightly behind) so they can reorder ties between sprites but never reorder against map faces or other objects. */
static inline float sprite_tiebreak(const Renderer *R, float depth_bias)
{
    float f = (depth_bias + 8.0f) * (1.0f / 32.0f);
    return f < -0.45f ? -0.45f : f > 0.45f ? 0.45f : f;
}

static void queue_bucket(Renderer *R, int strip, int col)
{
    const Level *L = R->L;
    if (col < 0 || col >= L->ncols) return;
    int bi = strip * L->ncols + col;
    const Face *F = L->faces + L->bstart[bi];
    int n = L->bcount[bi];
    for (int i = 0; i < n; i++, F++) queue_face(R, F, 1);
}

/* ENG3GAME: queue dynamic faces (object models); F must stay valid until render_flush. */
void render_faces(Renderer *R, const Face *F, int n) { for (int i = 0; i < n; i++) queue_face(R, F + i, 1); }

/* SOLIDS: queue one face; cull 0 = none, 1 = back-face on (0,1,2), 2 = visible if either half faces us. */
static void queue_face(Renderer *R, const Face *F, int cull)
{
    const Level *L = R->L;
    const Camera *C = &R->cam;
    float S = (float)R->scale;
    {
        float dx = F->org.x - R->cam.x, dz = F->org.z - R->cam.z;
        float oz = dx * C->fx + dz * C->fz;
        if (oz <= NEAR_Z) return;
        float sx[4], sy[4];
        int ok = 1;
        for (int q = 0; q < 4; q++) {
            float vx = F->p[q].x - R->cam.x, vz = F->p[q].z - R->cam.z;
            float xc = vx * C->rx + vz * C->rz, zc = vx * C->fx + vz * C->fz, yc = F->p[q].y - R->cam.y;
            if (zc < 8.0f) { ok = 0; break; }
            sx[q] = (GEOM_OFX + xc * GEOM_H / zc) * S;
            sy[q] = (R->ofy + yc * GEOM_H / zc) * S;
        }
        if (!ok) return;
        /* Off-screen reject. */
        float mnx = fminf(fminf(sx[0], sx[1]), fminf(sx[2], sx[3])), mxx = fmaxf(fmaxf(sx[0], sx[1]), fmaxf(sx[2], sx[3]));
        float mny = fminf(fminf(sy[0], sy[1]), fminf(sy[2], sy[3])), mxy = fmaxf(fmaxf(sy[0], sy[1]), fmaxf(sy[2], sy[3]));
        if (mxx < 0 || mnx >= R->w || mxy < 0 || mny >= R->h) return;
        /* Back-face test (GTE NCLIP on vertices 0,1,2); mirrored cells invert the winding. */
        float nclip = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (cull == 1 && (!F->mirror ? nclip < 0 : nclip > 0)) return;
        if (cull == 2 && nclip < 0) {
            float n2 = (sx[3] - sx[1]) * (sy[2] - sy[1]) - (sx[2] - sx[1]) * (sy[3] - sy[1]);
            if (n2 < 0) return;
        }
        int cellz = (int)(oz * 0.5f);             /* SZ>>2 in world units */
        int shade = 0;
        if (L->fogmax) {
            shade = (cellz - 0x40) >> L->fogshift;
            if (cellz < 0x40) shade = 0;
            if (L->eng3 && shade < 1) shade = 1;      /* ENG3 FUN_100206f0: clamp(.., 1, max) - the nearest row is always applied */
            if (shade > L->fogmax) shade = L->fogmax;
        }
        Item *it = push(R);
        it->kind = ITEM_FACE;
        it->key = (float)(cellz + F->otb);
        it->face.f = F;
        memcpy(it->face.sx, sx, sizeof sx);
        memcpy(it->face.sy, sy, sizeof sy);
        it->face.shade = shade;
        /* RENDERFIX: ENG1 map faces carry the baked lighting level; dynamic faces are neutral */
        it->face.light = (L->d && !L->eng3 && F >= L->faces && F < L->faces + L->nfaces) ? F->light : 0x80;
    }
}

/* SOLIDS: dynamic faces (3D model objects); F must stay valid until render_flush. */
void render_model_begin(Renderer *R, float wx, float wy, float wz)
{
    float sx, sy, zc;
    R->model_depth = render_project(R, wx, wy, wz, &sx, &sy, &zc) ? (int)(zc * 0.5f) : -1;
}

void render_face(Renderer *R, const Face *F, int cull)
{
    /* Model faces use the sprite ordering-table base (FUN_10031540: 0x4e1 - depth), not the map-face base. */
    int n0 = R->nitems;
    queue_face(R, F, cull);
    if (R->nitems > n0) {
        Item *it = &((Item *)R->items)[n0];
        it->key += sprite_ot_bias(R);
        if (R->model_depth >= 0 && R->L && !R->L->eng3 && R->L->fogmax >= 0) {   /* FUN_10031540: shade = depth >> 5, 1..0x1d, whatever the level's fog */
            int sh = R->model_depth >> 5;
            it->face.shade = sh < 1 ? 1 : sh > 0x1d ? 0x1d : sh;
        }
    }
}

void render_level(Renderer *R)
{
    const Level *L = R->L;
    if (L->eng3) {      /* LEVELS: ENG3 paths: every row bucket near and in front of the camera */
        const Camera *C = &R->cam;
        for (int s = 0; s < L->nstrips; s++)
            for (int c = 0; c < L->ncols; c++) {
                int bi = s * L->ncols + c;
                if (!L->bcount[bi]) continue;
                const Face *F = &L->faces[L->bstart[bi]];
                float dx = F->org.x - C->x, dz = F->org.z - C->z;
                float oz = dx * C->fx + dz * C->fz, ox = dx * C->rx + dz * C->rz;
                if (oz < -900 || oz > 5000 || fabsf(ox) > oz * 1.2f + 1400) continue;
                queue_bucket(R, s, c);
            }
        return;
    }
    /* FUN_1002da90 queues strips far to near (the later insertion of a nearer cell is drawn first within one slot). */
    if (L->mode) {
        for (int s = (L->nstrips < 2 ? L->nstrips : 2) - 1; s >= 0; s--)
            for (int c = 2; c < 66 && c < L->ncols; c++) queue_bucket(R, s, c);
        return;
    }
    /* Camera path column: invert the straight-segment mapping near the camera.  The original draws only six strips:
     * ((ca8 + 0x180) >> 7) + 4 down to that - 5 (FUN_1002da90 local_b4/local_b0); farther strips are never queued. */
    int cc = (int)floorf(R->cam.path_x / 64.0f);
    int far = L->nstrips - 1, near = 0;
    if (R->cam.cam_z_set && !getenv("RENDER_ALLSTRIPS")) {
        int top = (((int)R->cam.cam_z + 0x180) >> 7) + 4;
        if (top < 5) top = 5;
        if (top < far) far = top;
        near = top - 5 > 0 ? top - 5 : 0;
    } else if (far > 7) far = 7;
    for (int s = far; s >= near; s--) {
        if (getenv("ONLY_STRIP") && s != atoi(getenv("ONLY_STRIP"))) continue;
        int span = 4 + (L->strip_z[s] + 320) * 3 / 320;
        for (int c = cc - span; c <= cc + span; c++) queue_bucket(R, s, c);
    }
}

int render_sprite(Renderer *R, const Image *img, float wx, float wy, float wz, int flip, float depth_bias)
{
    float sx, sy, zc;
    if (!img || !render_project(R, wx, wy, wz, &sx, &sy, &zc)) return 0;
    if (R->L && !R->L->eng3) {      /* FUN_10032300: object z - camera z < 0x20 is not projected; FUN_100320d0: depth must be 1..0x480 */
        if (zc < 32.0f || (int)(zc * 0.5f) > 0x480) return 0;
    } else if (zc <= NEAR_Z * 0.5f) return 0;
    Item *it = push(R);
    it->kind = ITEM_SPRITE;
    float depth = floorf(zc * 0.5f);                    /* depth = obj+0x10 = SZ>>2, an integer */
    /* ENG3 FUN_100231a0: slot = 0x7fe - (depth & ~3) in table units where a face is 0x7ff - z - otb, i.e. face-key (depth & ~3) + 1 */
    if (R->L && R->L->eng3) depth = (float)((int)depth & ~3) + 1.0f;
    it->key = depth + sprite_ot_bias(R) + sprite_tiebreak(R, depth_bias);
    it->spr.img = img;
    it->spr.x = sx; it->spr.y = sy;
    it->spr.s = GEOM_H / zc;
    it->spr.flip = flip;
    it->spr.mode = -1;
    it->spr.level = 255;
    it->spr.ang = 0;     /* BOSSES */
    it->spr.tint = 0; it->spr.ysc = 1.0f;   /* FX */
    return 1;
}

/* ENEMIES3: sprite at an object scale (0x100 = 1.0). */
int render_sprite_obj(Renderer *R, const Image *img, float wx, float wy, float wz, int flip, float depth_bias, int scale)
{
    if (!render_sprite(R, img, wx, wy, wz, flip, depth_bias)) return 0;
    if (scale > 0 && scale != 0x100) ((Item *)R->items)[R->nitems - 1].spr.s *= scale / 256.0f;
    return 1;
}

/* ---- FX: PSX GPU sprite colour modulation (FUN_100311a0 colour word) ----
 * tint = 0x00RRGGBB, each channel 0x80 = unchanged (texel * c / 128, saturating, as the PSX texture blend);
 * mode -1 = opaque, 0..3 = PSX semi-transparency (code 0x2e, tpage abr = flags54 & 3);
 * ysc = vertical scale about the hotspot (shadows / reflections; negative draws upside down). */
int render_sprite_fx(Renderer *R, const Image *img, float wx, float wy, float wz, int flip, float depth_bias, int scale,
                     uint32_t tint, int mode, float ysc)
{
    if (!render_sprite(R, img, wx, wy, wz, flip, depth_bias)) return 0;
    Item *it = &((Item *)R->items)[R->nitems - 1];
    if (scale > 0 && scale != 0x100) it->spr.s *= scale / 256.0f;
    it->spr.tint = (tint & 0xffffff) == 0x808080 && !(tint & 0x2000000u) ? 0 : (tint | 0x1000000u);   /* FX3: keep the white-CLUT flag */
    it->spr.mode = mode < 0 ? -1 : (mode & 3);
    it->spr.level = 255;
    it->spr.ysc = ysc == 0 ? 0.001f : ysc;
    return 1;
}

void render_last_angle(Renderer *R, float ang)
{
    if (R->nitems) ((Item *)R->items)[R->nitems - 1].spr.ang = ang;
}

static inline uint32_t fx_tint(uint32_t c, uint32_t tint)
{
    if (!tint) return c;
    /* FX3: the PC rasteriser modulates by ONE level, the colour word's first (R) byte (HERCULES.EXE 0x40af03:
     * movb 0x4(%esi) -> DAT_00a90a00), through the CIF's 256 x 256 modulation table (FUN_0040dd70); row k
     * scales luminance by k/128 (checked on THEBES' table: 0x4d -> 0.60, 0x80 -> identity, 0xc0 -> 1.43).
     * 0x2000000 = the white dissolve CLUT (FUN_10005710: DAT_1004d818 = 31 x 0xffff): texel colour ignored. */
    uint32_t lv = (tint >> 16) & 255;
    if (tint & 0x2000000u) { uint32_t w = 248u * lv >> 7; if (w > 255) w = 255; return (c & 0xff000000u) | w << 16 | w << 8 | w; }
    uint32_t r = ((c >> 16) & 255) * lv >> 7, g = ((c >> 8) & 255) * lv >> 7,
             b = (c & 255) * lv >> 7;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (c & 0xff000000u) | r << 16 | g << 8 | b;
}
/* ---- end FX ---- */

/* COMBAT: sprite drawn with a PSX semi-transparency mode (0 average, 1 add, 2 subtract, 3 add 1/4) after
 * scaling its colour by level/255; mode -1 = opaque. */
/* ENG3GAME: sprite drawn at scale (1.0 = normal) */
int render_sprite_scaled(Renderer *R, const Image *img, float wx, float wy, float wz, int flip, float depth_bias, float scale)
{
    if (!render_sprite(R, img, wx, wy, wz, flip, depth_bias)) return 0;
    ((Item *)R->items)[R->nitems - 1].spr.s *= scale;
    return 1;
}

int render_sprite_ex(Renderer *R, const Image *img, float wx, float wy, float wz, int flip, float depth_bias,
                     int mode, int level)
{
    if (!render_sprite(R, img, wx, wy, wz, flip, depth_bias)) return 0;
    Item *it = &((Item *)R->items)[R->nitems - 1];
    it->spr.mode = mode;
    it->spr.level = level < 0 ? 0 : level > 255 ? 255 : level;
    return 1;
}

/* BOSSES: sprite with the object's scale (+0x3a, 0x100 = 1) and z rotation (+0x74, 4096 = full turn). */
int render_sprite_xf(Renderer *R, const Image *img, float wx, float wy, float wz, int flip, float depth_bias,
                     int mode, int level, float scale, float angle)
{
    if (!render_sprite_ex(R, img, wx, wy, wz, flip, depth_bias, mode < 0 ? 0 : mode, level)) return 0;
    Item *it = &((Item *)R->items)[R->nitems - 1];
    if (mode < 0) { it->spr.mode = -1; it->spr.level = 255; }
    it->spr.s *= scale;
    it->spr.ang = angle;
    return 1;
}

/* TITANS2: screen-space sprite (ENG1 draw mode 6) */
int render_sprite2d(Renderer *R, const Image *img, int x, int y, float scale, int flip, float key, float angle)
{
    if (!img) return 0;
    Item *it = push(R);
    it->kind = ITEM_SPRITE;
    it->key = key + sprite_ot_bias(R);     /* RENDERFIX: slot 0x4e1 - obj+0x10 (= +0x3e here), i.e. face key + 0x40 like every object sprite */
    it->spr.img = img;
    it->spr.x = (float)x; it->spr.y = (float)y;
    it->spr.s = scale;
    it->spr.flip = flip;
    it->spr.mode = -1; it->spr.level = 255;
    it->spr.ang = angle;
    it->spr.tint = 0; it->spr.ysc = 1.0f;   /* FX */
    return 1;
}

/* TITANS3: render_sprite2d with a PSX semi-transparency mode (0 average, 1 add, 2 subtract, 3 add 1/4) */
int render_sprite2d_ex(Renderer *R, const Image *img, int x, int y, float scale, int flip, float key, float angle, int mode)
{
    if (!render_sprite2d(R, img, x, y, scale, flip, key, angle)) return 0;
    ((Item *)R->items)[R->nitems - 1].spr.mode = mode;
    return 1;
}

static int cmp_items(const void *a, const void *b)
{
    const Item *A = a, *B = b;
    if (A->key != B->key) return A->key < B->key ? 1 : -1;     /* larger key = lower slot = drawn first */
    return A->seq < B->seq ? 1 : A->seq > B->seq ? -1 : 0;     /* same slot: head insertion draws the latest first */
}

static inline uint32_t blend(uint32_t bgc, uint32_t fg, int mode)
{
    int br = (bgc >> 16) & 255, bgg = (bgc >> 8) & 255, bb = bgc & 255;
    int fr = (fg >> 16) & 255, fgg = (fg >> 8) & 255, fb = fg & 255;
    int r, g, b;
    switch (mode) {
    case 0: r = (br + fr) >> 1; g = (bgg + fgg) >> 1; b = (bb + fb) >> 1; break;
    case 1: r = br + fr; g = bgg + fgg; b = bb + fb; break;
    case 2: r = br - fr; g = bgg - fgg; b = bb - fb; break;
    default: r = br + (fr >> 2); g = bgg + (fgg >> 2); b = bb + (fb >> 2); break;
    }
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return 0xff000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

typedef struct {
    const uint8_t *tex;     /* VRAM row base for the tpage */
    int tx;
    uint32_t lut[256];      /* texel -> ARGB after shading */
    int semi, mode;
} TexCtx;

static void raster_tri(Renderer *R, const TexCtx *t,
                       float x0, float y0, float u0, float v0,
                       float x1, float y1, float u1, float v1,
                       float x2, float y2, float u2, float v2)
{
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (fabsf(area) < 1e-3f) return;
    float ia = 1.0f / area;
    float dudx = ((u1 - u0) * (y2 - y0) - (u2 - u0) * (y1 - y0)) * ia;
    float dudy = ((u2 - u0) * (x1 - x0) - (u1 - u0) * (x2 - x0)) * ia;
    float dvdx = ((v1 - v0) * (y2 - y0) - (v2 - v0) * (y1 - y0)) * ia;
    float dvdy = ((v2 - v0) * (x1 - x0) - (v1 - v0) * (x2 - x0)) * ia;
    int minx = (int)floorf(fminf(x0, fminf(x1, x2))), maxx = (int)ceilf(fmaxf(x0, fmaxf(x1, x2)));
    int miny = (int)floorf(fminf(y0, fminf(y1, y2))), maxy = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > R->w) maxx = R->w;
    if (maxy > R->h) maxy = R->h;
    if (minx >= maxx || miny >= maxy) return;
    float sgn = area > 0 ? 1.0f : -1.0f;
    /* Edge functions E(x,y) = (xb-xa)*(y-ya) - (yb-ya)*(x-xa), oriented to be >= 0 inside. */
    float e0a = -(y2 - y1) * sgn, e0b = (x2 - x1) * sgn, e0c = ((y2 - y1) * x1 - (x2 - x1) * y1) * sgn;
    float e1a = -(y0 - y2) * sgn, e1b = (x0 - x2) * sgn, e1c = ((y0 - y2) * x2 - (x0 - x2) * y2) * sgn;
    float e2a = -(y1 - y0) * sgn, e2b = (x1 - x0) * sgn, e2c = ((y1 - y0) * x0 - (x1 - x0) * y0) * sgn;
    for (int y = miny; y < maxy; y++) {
        float py = y + 0.5f;
        float px = minx + 0.5f;
        float w0 = e0a * px + e0b * py + e0c;
        float w1 = e1a * px + e1b * py + e1c;
        float w2 = e2a * px + e2b * py + e2c;
        float u = u0 + (px - x0) * dudx + (py - y0) * dudy;
        float v = v0 + (px - x0) * dvdx + (py - y0) * dvdy;
        uint32_t *row = R->fb + (size_t)y * R->w;
        for (int x = minx; x < maxx; x++, w0 += e0a, w1 += e1a, w2 += e2a, u += dudx, v += dvdx) {
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            int iu = (int)u, iv = (int)v;
            if (iu < 0) iu = 0;
            if (iv < 0) iv = 0;
            uint8_t texel = t->tex[(size_t)(iv & 255) * 2048 + ((t->tx + (iu & 255)) & 2047)];
            if (!texel) continue;
            uint32_t c = t->lut[texel];
            row[x] = t->semi ? blend(row[x], c, t->mode) : c;
        }
    }
}

static void draw_face(Renderer *R, const Item *it)
{
    const Level *L = R->L;
    const Face *F = it->face.f;
    TexCtx t;
    int tp = F->tpage;
    t.tex = L->vram8 + (size_t)(((tp >> 4) & 1) * 256) * 2048;
    t.tx = (tp & 15) * 128;
    t.semi = (F->flags & 0x20) != 0;
    t.mode = (tp >> 5) & 3;
    const uint8_t *sh = it->face.shade ? L->shade + (it->face.shade - 1) * 256 : NULL;
    const uint8_t *md = NULL;
    if (t.semi && !L->eng3 && L->d) {
        /* ENG1 semi-transparent faces take the PC blend path (FUN_0040e680): never depth-shaded, lit by the colour word's R
         * byte through the modulation table (FUN_0040dd70) unless it is 0x80. */
        sh = NULL;
        if (it->face.light != 0x80 && L->modtab && !getenv("RENDER_NOLIGHT")) md = L->modtab + (size_t)it->face.light * 256;
    }
    for (int i = 0; i < 256; i++) {
        int idx = md ? md[i] : sh ? sh[i] : i;
        t.lut[i] = 0xff000000u | (uint32_t)L->pal[idx][0] << 16 | (uint32_t)L->pal[idx][1] << 8 | L->pal[idx][2];
    }
    const float *sx = it->face.sx, *sy = it->face.sy;
    raster_tri(R, &t, sx[0], sy[0], F->uv[0][0], F->uv[0][1], sx[1], sy[1], F->uv[1][0], F->uv[1][1],
               sx[2], sy[2], F->uv[2][0], F->uv[2][1]);
    raster_tri(R, &t, sx[1], sy[1], F->uv[1][0], F->uv[1][1], sx[3], sy[3], F->uv[3][0], F->uv[3][1],
               sx[2], sy[2], F->uv[2][0], F->uv[2][1]);
}

static void draw_image(Renderer *R, const Image *img, float x, float y, float s, int flip, int mode, int level,
                       uint32_t tint, float ysc)   /* FX: tint / ysc */
{
    /* x,y = hotspot in framebuffer pixels, s = framebuffer pixels per sprite pixel */
    float x0 = flip ? x - (img->w - img->ox) * s : x - img->ox * s;
    float sv = s * ysc;                                     /* FX: vertical scale (negative = upside down) */
    float y0 = sv >= 0 ? y - img->oy * sv : y + img->oy * -sv;
    float ya = sv >= 0 ? y0 : y - (img->h - img->oy) * -sv, yb = ya + img->h * fabsf(sv);
    int ix0 = (int)floorf(x0), iy0 = (int)floorf(ya);
    int ix1 = (int)ceilf(x0 + img->w * s), iy1 = (int)ceilf(yb);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > R->w) ix1 = R->w;
    if (iy1 > R->h) iy1 = R->h;
    float inv = 1.0f / s, invv = 1.0f / fabsf(sv);
    for (int py = iy0; py < iy1; py++) {
        int sy = sv >= 0 ? (int)((py + 0.5f - y0) * invv) : img->h - 1 - (int)((py + 0.5f - ya) * invv);   /* FX */
        if (sy < 0 || sy >= img->h) continue;
        const uint32_t *src = img->px + (size_t)sy * img->w;
        uint32_t *dst = R->fb + (size_t)py * R->w;
        for (int px = ix0; px < ix1; px++) {
            int sx = (int)((px + 0.5f - x0) * inv);
            if (sx < 0 || sx >= img->w) continue;
            if (flip) sx = img->w - 1 - sx;
            uint32_t c = src[sx];
            uint32_t a = c >> 24;
            if (!a) continue;
            c = fx_tint(c, tint);                                   /* FX */
            if (mode >= 0) {                                            /* COMBAT */
                uint32_t sc = ((((c >> 16) & 255) * level / 255) << 16) | ((((c >> 8) & 255) * level / 255) << 8) |
                              ((c & 255) * level / 255);
                dst[px] = blend(dst[px], sc, mode);
            } else if (a == 255) dst[px] = c;
            else {
                uint32_t d = dst[px];
                uint32_t r = (((c >> 16) & 255) * a + ((d >> 16) & 255) * (255 - a)) / 255;
                uint32_t g = (((c >> 8) & 255) * a + ((d >> 8) & 255) * (255 - a)) / 255;
                uint32_t b = ((c & 255) * a + (d & 255) * (255 - a)) / 255;
                dst[px] = 0xff000000u | r << 16 | g << 8 | b;
            }
        }
    }
}

/* BOSSES: rotated sprite (angle in 4096ths of a turn, about the hotspot). */
static void draw_image_rot(Renderer *R, const Image *img, float x, float y, float s, int flip, int mode, int level, float ang,
                           uint32_t tint)   /* FX: tint */
{
    float a = ang * (6.2831853f / 4096.0f), c = cosf(a), sn = sinf(a);
    float rx = fmaxf(fabsf((float)img->ox), fabsf((float)(img->w - img->ox)));
    float ry = fmaxf(fabsf((float)img->oy), fabsf((float)(img->h - img->oy)));
    float rad = sqrtf(rx * rx + ry * ry) * s + 2;
    int ix0 = (int)floorf(x - rad), iy0 = (int)floorf(y - rad), ix1 = (int)ceilf(x + rad), iy1 = (int)ceilf(y + rad);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > R->w) ix1 = R->w;
    if (iy1 > R->h) iy1 = R->h;
    float inv = 1.0f / s;
    for (int py = iy0; py < iy1; py++) {
        uint32_t *dst = R->fb + (size_t)py * R->w;
        for (int px = ix0; px < ix1; px++) {
            float dx = (px + 0.5f - x) * inv, dy = (py + 0.5f - y) * inv;
            float ux = dx * c + dy * sn, uy = -dx * sn + dy * c;
            int sx = (int)floorf(flip ? img->ox - ux : img->ox + ux), sy = (int)floorf(img->oy + uy);
            if (flip) sx = img->w - 1 - (int)floorf((float)(img->w - img->ox) + ux);
            if (sx < 0 || sx >= img->w || sy < 0 || sy >= img->h) continue;
            uint32_t col = img->px[(size_t)sy * img->w + sx];
            if (!(col >> 24)) continue;
            col = fx_tint(col, tint);                               /* FX */
            if (mode >= 0) {
                uint32_t sc = ((((col >> 16) & 255) * level / 255) << 16) | ((((col >> 8) & 255) * level / 255) << 8) |
                              ((col & 255) * level / 255);
                dst[px] = blend(dst[px], sc, mode);
            } else dst[px] = col | 0xff000000u;
        }
    }
}

void render_flush(Renderer *R)
{
    Item *items = R->items;
    qsort(items, R->nitems, sizeof(Item), cmp_items);
    float S = (float)R->scale;
    if (getenv("ZDBG")) {
        int zx = 0, zy = 0; sscanf(getenv("ZDBG"), "%d,%d", &zx, &zy);
        for (int i = 0; i < R->nitems; i++) {
            Item *it = &items[i];
            if (it->kind == ITEM_FACE) {
                const float *X = it->face.sx, *Y = it->face.sy;
                float mnx = fminf(fminf(X[0], X[1]), fminf(X[2], X[3])) / S, mxx = fmaxf(fmaxf(X[0], X[1]), fmaxf(X[2], X[3])) / S;
                float mny = fminf(fminf(Y[0], Y[1]), fminf(Y[2], Y[3])) / S, mxy = fmaxf(fmaxf(Y[0], Y[1]), fmaxf(Y[2], Y[3])) / S;
                if (zx >= mnx && zx <= mxx && zy >= mny && zy <= mxy)
                    fprintf(stderr, "ZDBG %d face key %.1f otb %d tp %x orgz %.0f\n", i, it->key, it->face.f->otb, it->face.f->tpage, it->face.f->org.z);
            } else fprintf(stderr, "ZDBG %d sprite key %.1f\n", i, it->key);
        }
    }
    for (int i = 0; i < R->nitems; i++) {
        Item *it = &items[i];
        if (it->kind == ITEM_FACE) { draw_face(R, it); continue; }
        if (R->light && R->light != 0x80) {     /* FX3: FUN_100311a0 base colour (level light) times +0x4a */
            uint32_t lv = it->spr.tint ? (it->spr.tint >> 16) & 255 : 0x80;
            lv = (uint32_t)R->light * lv >> 7 & 0xff;
            it->spr.tint = lv == 0x80 && !(it->spr.tint & 0x2000000u) ? 0 : (it->spr.tint & 0x2000000u) | 0x1000000u | lv * 0x010101u;
        }
        if (0) {}
        else if (it->spr.ang != 0)      /* BOSSES */
            draw_image_rot(R, it->spr.img, it->spr.x * S, it->spr.y * S, it->spr.s * S, it->spr.flip, it->spr.mode,
                           it->spr.level, it->spr.ang, it->spr.tint);
        else draw_image(R, it->spr.img, it->spr.x * S, it->spr.y * S, it->spr.s * S, it->spr.flip,
                         it->spr.mode, it->spr.level, it->spr.tint, it->spr.ysc);
    }
    R->nitems = 0;
}

void render_image2d(Renderer *R, const Image *img, int x, int y, int flip)
{
    if (img) draw_image(R, img, (float)x * R->scale, (float)y * R->scale, (float)R->scale, flip, -1, 255, 0, 1.0f);
}

void render_rect(Renderer *R, int x, int y, int w, int h, uint32_t argb)
{
    int S = R->scale;
    for (int py = y * S; py < (y + h) * S; py++) {
        if (py < 0 || py >= R->h) continue;
        for (int px = x * S; px < (x + w) * S; px++) {
            if (px < 0 || px >= R->w) continue;
            uint32_t a = argb >> 24;
            if (a == 255) R->fb[(size_t)py * R->w + px] = argb;
            else {
                uint32_t d = R->fb[(size_t)py * R->w + px];
                uint32_t r = (((argb >> 16) & 255) * a + ((d >> 16) & 255) * (255 - a)) / 255;
                uint32_t g = (((argb >> 8) & 255) * a + ((d >> 8) & 255) * (255 - a)) / 255;
                uint32_t b = ((argb & 255) * a + (d & 255) * (255 - a)) / 255;
                R->fb[(size_t)py * R->w + px] = 0xff000000u | r << 16 | g << 8 | b;
            }
        }
    }
}
