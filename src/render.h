/* Software renderer reproducing the PC port's PSX-style pipeline: GTE-like perspective (H=320, OFX=160,
 * OFY=128 at 320x240), an ordering table (far-to-near painter's sort), affine texturing from the level's
 * 8-bit VRAM, depth fog through MAP_PSX.CIF shade tables and PSX semi-transparency modes. */
#pragma once
#include <stdint.h>
#include "level.h"

typedef struct {
    float x, y, z;      /* world position */
    float rx, rz;       /* screen-right axis in world xz */
    float fx, fz;       /* view (into-screen) axis in world xz */
    float path_x;       /* camera position along the level path, for column culling */
    float cam_z;        /* RENDERFIX: DAT_10070ca8, the camera's path-z (player plane - 320); selects the 6-strip window of FUN_1002da90 (0 = unset) */
    int cam_z_set;
} Camera;

typedef struct {
    int w, h, scale;    /* framebuffer = 320*scale x 240*scale */
    uint32_t *fb;       /* ARGB8888 */
    void *items; int nitems, cap;
    const Level *L;
    Camera cam;
    float ofy;          /* LEVELS: GTE screen-centre y (ENG1 128, ENG3 94) */
    int model_depth;    /* RENDERFIX: depth (SZ>>2) of the model object being drawn, -1 = none (FUN_10031540 shades by obj depth>>5) */
    int light;          /* FX3: level light byte for world sprites (FUN_100311a0 colour word, R channel; 0 = off) */
} Renderer;

typedef struct {
    int w, h;
    int ox, oy;         /* hotspot (feet) inside the image */
    uint32_t *px;       /* ARGB, alpha 0 = transparent */
} Image;

void render_init(Renderer *R, int scale);
void render_begin(Renderer *R, const Level *L, const Camera *cam, uint32_t bg);
void render_level(Renderer *R);
/* Queue a sprite at world position (wx, wy, wz); mirrored if flip. Returns 0 if culled. */
int render_sprite(Renderer *R, const Image *img, float wx, float wy, float wz, int flip, float depth_bias);
/* COMBAT: as render_sprite, drawn semi-transparent (mode 0..3 = PSX blend modes, colour scaled by level/255). */
int render_sprite_ex(Renderer *R, const Image *img, float wx, float wy, float wz, int flip, float depth_bias,
                     int mode, int level);
/* ENEMIES3: as render_sprite at a size of scale/256 (object +0x3a, FUN_10030e40). */
int render_sprite_obj(Renderer *R, const Image *img, float wx, float wy, float wz, int flip, float depth_bias, int scale);
/* RENDERFIX: call before queueing a model object's faces: its origin's depth fixes the face shade (depth>>5, clamp 1..0x1d). */
void render_model_begin(Renderer *R, float wx, float wy, float wz);
/* SOLIDS: queue a dynamic face (cull 0 none, 1 standard, 2 either half). */
void render_face(Renderer *R, const Face *F, int cull);
/* BOSSES: as render_sprite_ex (mode -1 = opaque) with a scale factor and a z rotation (4096 = full turn). */
int render_sprite_xf(Renderer *R, const Image *img, float wx, float wy, float wz, int flip, float depth_bias,
                     int mode, int level, float scale, float angle);
/* TITANS2: screen-space sprite (x, y in 320x240 units, hotspot there) sorted at ordering-table depth `key`
 * (ENG1 obj+0x3e), scaled and rotated (4096 = full turn): ENG1 draw mode 6 (FUN_10004c10). */
int render_sprite2d(Renderer *R, const Image *img, int x, int y, float scale, int flip, float key, float angle);
int render_sprite2d_ex(Renderer *R, const Image *img, int x, int y, float scale, int flip, float key, float angle, int mode);   /* TITANS3 */
void render_flush(Renderer *R);
/* ENG3GAME: render_sprite at a scale (1.0 = normal size). */
int render_sprite_scaled(Renderer *R, const Image *img, float wx, float wy, float wz, int flip, float depth_bias, float scale);
/* ENG3GAME: queue extra faces (dynamic models); they must stay valid until render_flush. */
void render_faces(Renderer *R, const Face *F, int n);
/* Screen-space 2D helpers (coordinates in 320x240 units). */
void render_image2d(Renderer *R, const Image *img, int x, int y, int flip);
void render_rect(Renderer *R, int x, int y, int w, int h, uint32_t argb);
int render_project(const Renderer *R, float wx, float wy, float wz, float *sx, float *sy, float *zc);

/* RENDERFIX: rotate the sprite queued last about its hotspot (angle in 4096ths of a turn): ENG1 draw functions 10004330 / 100048b0 are
 * used whenever obj+0x74 (rot[2], x4 = GTE angle) is non-zero. */
void render_last_angle(Renderer *R, float ang);
/* FXW: override the ordering-table depth (SZ>>2, integer slots) of the sprite queued last. */
void render_last_depth(Renderer *R, float depth);

/* FX: sprite with PSX colour modulation (tint 0x00RRGGBB, 0x80 per channel = unchanged), blend mode (-1 opaque,
 * 0..3 PSX abr) and vertical scale ysc (negative = upside down, for water reflections). scale 0x100 = 1. */
int render_sprite_fx(Renderer *R, const Image *img, float wx, float wy, float wz, int flip, float depth_bias, int scale,
                     uint32_t tint, int mode, float ysc);
