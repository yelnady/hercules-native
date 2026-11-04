/* TITANS2: Battle of the Titans actors. Each handler below is a port of the ENG1.DLL function named in its
 * comment (level-9 handler table 0x10061f80: {init, -, update} per actor type). Object fields keep the
 * original offsets in the comments:
 *   +0x2a state -> v2a        +0x108 counter -> w[0]    +0x10c table pointer / orbit phase -> w[1]
 *   +0xe0 command timer -> w[2]   +0x3e ordering-table depth -> w[3]   group member count -> w[4]
 *   +0x128 parent -> w[5] (object index + 1)    +0xa8/+0xb0/+0xb8/+0xc0/+0xc8/+0xd0 -> v[0]/v[2]/v[4]/v[6]/v[8]/v[10]
 *   +0x3a scale -> scale   +0x70/+0x72/+0x74 rotation -> rot[]   +0x94 facing -> facing   +0xee hp -> hp
 * flags54 bit 30 (0x40000000) = screen-space object: x/y are 320x240 screen coordinates (FUN_100027a0 list draw
 * path FUN_10003cd0, draw mode +0x23 = 6 = FUN_10004c10 scaled / rotated sprite). */
#include "titans_ai.h"
#include "objects_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SINTAB 0x1005a848u          /* s16[1024], +-0x7fff, 0x400 = full turn */
#define SCREEN 0x40000000u
#define MODEL 0x10000000u

static TitansCtx C;
TitansCtx *titans_ai_ctx(void) { return &C; }

static Objects *gO;                 /* the Objects being ticked (handlers use the ENG1 globals through C) */

static int sinv(int a) { return pe_s16(gO->pe, SINTAB + 2u * (uint32_t)(a & 0x3ff)); }
static int rnd(int n) { return obj_rnd(gO, n); }
static void snd(int id, int32_t x) { obj_sound(gO, id, x); }
static void play(Object *o) { obj_run_script(gO, o); }
static int played(Object *o) { return obj_run_script(gO, o); }
static int words(Object *o) { return obj_run_words(gO, o); }
static void anim(Object *o, int a) { obj_set_anim(gO, o, a); }
static void kill(Object *o) { obj_kill(gO, o, 1); }
static Object *spawn(uint16_t type, int32_t x, int32_t y, int32_t z) { return obj_spawn(gO, type, x, y, z); }

static void next_anim(Object *o)    /* FUN_10029270 */
{
    if (!o->anims) return;
    o->anim++;
    for (int g = 0; g < 64; g++) {
        uint32_t s = pe_u32(gO->pe, o->anims + 4u * (uint32_t)o->anim);
        if (s) { o->script = s; return; }
        o->anim = (int16_t)pe_u16(gO->pe, o->anims + 4u * (uint32_t)(o->anim + 1));
    }
}
static void reset_anim(Object *o) { o->idx = -1; o->timer = 0; }     /* FUN_10029260 */

static Object *parent_of(Object *o)
{
    int i = o->w[5] - 1;
    return i >= 0 && i < OBJ_MAX && gO->obj[i].used ? &gO->obj[i] : NULL;
}
static void set_parent(Object *o, Object *p) { o->w[5] = p ? (int)(p - gO->obj) + 1 : 0; }

/* FUN_10037e30 through the player damage request (titans.c turns it into Pegasus' hurt / death). */
static void hurt(int level, const Object *o)
{
    if (C.pstate == 0x23 || C.pstate == 0x24 || !gO->P) return;
    if (getenv("TITANS_LOG")) printf("[%ld] actor %04x hurts Pegasus (level %d) rel %d,%d state %x v %d,%d\n", C.frame, o->type, level, I(o->x) - I(C.px), I(o->y) - I(C.py), o->v2a, o->v[0] >> 16, o->v[4] >> 16);
    player_damage(gO->P, level, o->x, o->y, o->z, 0);
}

/* FUN_1002a710(DAT_1007217c + 0x88, obj): Pegasus' body box against the object's frame box. */
static int touch_peg(const Object *o)
{
    int b[6];
    if (!C.have_pbody || (o->flags54 & SCREEN)) return 0;
    if (!obj_box(gO, o, 1, b)) {
        int X = I(o->x), Y = I(o->y), Z = I(o->z), r = 12 * (o->scale ? o->scale : 0x100) >> 8;
        b[0] = X - r; b[3] = X + r; b[1] = Y - 2 * r; b[4] = Y; b[2] = Z - 64; b[5] = Z + 64;
    }
    return obj_boxes_overlap(b, C.pbody);
}

/* FUN_1002a710(DAT_10071440, obj) with DAT_1006fec4 set: Pegasus' sword box, or one of the Gift weapon
 * effects (fireball stream 0x4001, lightning 0x4005) the player code spawned. One blow per 8 ticks. */
static int hit_by_player(Object *o)
{
    if (o->inv) return 0;
    int b[6];
    if (!obj_box(gO, o, 1, b)) {
        int X = I(o->x), Y = I(o->y), Z = I(o->z), r = 16 * (o->scale ? o->scale : 0x100) >> 8;
        b[0] = X - r; b[3] = X + r; b[1] = Y - 2 * r; b[4] = Y; b[2] = Z - 64; b[5] = Z + 64;
    }
    int hit = C.sword_on && obj_boxes_overlap(b, C.sword);
    for (int i = 0; i < OBJ_MAX && !hit; i++) {
        const Object *e = &gO->obj[i];
        if (!e->used || e->kind != K_EFFECT || !e->ai) continue;
        int X = I(e->x), Y = I(e->y), Z = I(e->z);
        int eb[6];
        if (e->type == 0x4001) {                      /* TITANS3: fireball stream segment box (FUN_1001a5a0) */
            eb[0] = X - 8; eb[3] = X + 8; eb[1] = Y - 8; eb[4] = Y + 0x18; eb[2] = Z - 0x10; eb[5] = Z + 0x10;
        } else if (!obj_box(gO, e, 1, eb)) {          /* TITANS3: the bolt's own frame box (+0x88 of 0x4005) */
            eb[0] = X - 10; eb[3] = X + 10; eb[1] = Y - 10; eb[4] = Y + 10; eb[2] = Z - 24; eb[5] = Z + 24;
        }
        hit = obj_boxes_overlap(b, eb);
    }
    if (hit) { o->inv = 8; if (getenv("TITANS_LOG")) printf("[%ld] actor %04x hit by Pegasus at %d,%d hp %d\n", C.frame, o->type, I(o->x), I(o->y), o->hp); }
    return hit;
}

/* FUN_1002aa30: off screen by more than (w, h) - here from the camera instead of the last drawn position. */
static int offscreen(const Object *o, int w, int h)
{
    int sx = I(o->x) - C.camx + 160, sy = I(o->y) - C.camy + 128;
    return sx - w > 319 || sx + w < 0 || sy - h > 239 || sy + h < 0;
}

/* FUN_10020ea0 probes used by the harpies: floor below / ceiling above a column (ceiling = top of the sky). */
static int floor_at(int x, int z)
{
    int s;
    if (level_floor(gO->L, x, -64, z, &s) < 0x7fff) return s;
    return 0x400;
}
static int mid_at(int x, int z) { return FX((16 + floor_at(x, z)) / 2); }   /* FUN_1003c1e0 */

/* TITANS3: FUN_1003c410 - the drop direction depends on the object's screen y (+0xc): near the top it is thrown
 * sideways (| 0x20), near the bottom sideways and fast (| 0x240). FUN_100131c0 makes a bouncing pickup 0x4012
 * (the id rules are drop_pickup in enemies.c); FUN_10013570 moves it on level 9: it drifts with the scroll,
 * can be caught as soon as it falls, and expires after +0x97 ticks or 0x100 off screen. */
#define DROP_TAG 0x4012
static void drop(Object *o, int id)
{
    if (!(id & 0xf)) return;
    int sy = I(o->y) - C.camy + 128;
    if (sy > 0xc8) id |= 0x240; else if (sy < 0x29) id |= 0x20;
    Object *p = en_drop_pickup_obj(gO, o, id & 0xf);
    if (!p) return;
    p->kind = K_TITAN; p->w[11] = DROP_TAG;
    p->life = (id >> 16) & 0xff ? (int)((id >> 16) & 0xff) : 0xfa;
    p->count126 = 3;
    if (id & 0x800) { p->vy = -0x60000; p->w[10] = 0x5000; }
    else if (id & 0x400) { p->vy = -0x40000; p->w[10] = 0x3000; }
    else { p->vy = -(((int)rnd(256)) + 0x500) * 0x100; p->w[10] = rnd(0x1000) + 0x4000; }
    p->vx = 0;
    if (id & 0xe0) {
        int dir = (int)((id & 0xe0) >> 6);
        if (dir > 1) dir = rnd(2);
        int sp = (id & 0x200) ? 0x30000 : (id & 0x100) ? 0x10000 : (rnd(0x200) + 0x80) * 0x100;
        p->vx = dir ? -sp : sp;
    }
}

/* TITANS3: hit spark 0x402a (effect pool; init FUN_1003bbe0: scale 0x180, +0x24 = 0x40). It plays its 2-byte
 * script (frames 0x3e..0x45 of the harpy bank) once, semi-transparent additive, then goes. */
#define SPARK_TAG 0x402a
static void spark(int32_t x, int32_t y, int32_t z)
{
    uint32_t sc, an, mv, in; int bk;
    if (!obj_type_info(gO, 0x402a, &sc, &an, &mv, &bk, &in) || !sc) return;
    Object *o = obj_alloc(gO);
    if (!o) return;
    o->type = 0x402a; o->x = o->px = x; o->y = o->py = y; o->z = o->pz = z;
    o->bank = (uint8_t)bk; o->script = sc; o->idx = 0; o->timer = 0; o->pairs = 0;
    o->scale = 0x180; o->life = 0x40;
    o->flags54 |= 5;                                  /* additive */
    o->kind = K_TITAN; o->w[11] = SPARK_TAG;
    words(o);
}

static void falling_pickup(Object *o)  /* FUN_10013570, level 9 */
{
    words(o);
    if (!o->used) return;
    o->x += *C.speed;                              /* DAT_100707e8 */
    o->x += o->vx;
    if (o->vx > 0) { if ((o->vx -= 0x1000) < 0) o->vx = 0; } else if (o->vx < 0 && (o->vx += 0x1000) > 0) o->vx = 0;
    o->vy += o->w[10];
    if (o->vy > 0x80000) o->vy = 0x80000;
    o->y += o->vy;
    if (o->vy > 0) o->count126 = 0;                /* level 9: catchable once it falls */
    int b[6];
    if (!obj_box(gO, o, 1, b)) { int X = I(o->x), Y = I(o->y); b[0] = X - 10; b[3] = X + 10; b[1] = Y - 20; b[4] = Y; b[2] = I(o->z) - 64; b[5] = I(o->z) + 64; }
    if (C.have_pbody && o->count126 <= 2 && obj_boxes_overlap(b, C.pbody)) {
        obj_collect(gO, o->type);
        spawn(0x2011, o->x, o->y - 0x80000, o->z);
        if (getenv("TITANS_LOG")) printf("[%ld] Pegasus caught pickup %04x\n", C.frame, o->type);
        kill(o);
        return;
    }
    if (--o->life <= 0) { spawn(0x2011, o->x, o->y - 0x100000, o->z); kill(o); return; }
    if (offscreen(o, 0x100, 0x100)) kill(o);
}
static void maybe_drop(Object *o)   /* FUN_1003c490 */
{
    int r = rnd(0x8000);
    if (r < 0x3000) drop(o, r > 0x7ff ? 1 : 3);
}

/* ----------------------------------------------------------- background Titans (screen-space, behind) */

static void titan_place(Object *o, int div)      /* x = (start camera - camera) / div + 400 */
{
    o->x = (int32_t)((((int64_t)(C.titan_cam - C.camx)) << 16) / div) + 0x1900000;
}

static void screen_obj(Object *o, int ot)
{
    o->flags54 = 0xc0000000u; o->b23 = 6; o->w[3] = ot;
}

static void titan_init(Object *o, int y, int state_anim)   /* shared part of FUN_10038090 / 10038590 / ... */
{
    screen_obj(o, 0x4bb);
    o->anim = -1; next_anim(o);
    o->v2a = 0x37; anim(o, state_anim); played(o);
    o->x = 0x1900000; o->y = y; o->w[0] = 0;
    C.titan_cam = C.camx ? C.camx : 1;
}

static void rock_next(Object *o)       /* FUN_10037fe0 */
{
    int r = rnd(0x8000);
    if (o->x < 0) { o->v2a = 0x39; anim(o, 1); o->w[0] = 0; }
    else if (r < 0x3000) { o->v2a = 0x2e; anim(o, 1); }
    else if (r < 0x6000) { o->v2a = 0x1a; anim(o, 2); }
    else { o->v2a = 0xd; anim(o, 3); }
    play(o);
}

static void titan_gone(Object *o) { kill(o); C.titan_cam = 0; }   /* FUN_10037fa0 / FUN_10038490 */

static void rock_titan(Object *o)      /* FUN_10038100 (type 0x28, rock Titan) */
{
    if (C.shake_i) {                   /* DAT_1006ffec: camera shake table 0x100638f8 after a rock impact */
        C.shake_i++;
        C.shake = (int32_t)pe_u32(gO->pe, 0x100638f8u + 4u * (uint32_t)(C.shake_i & 0xffff));
        if (!C.shake || C.shake_i > 24) { C.shake_i = 0; C.shake = 0; }
    }
    switch (o->v2a) {
    case 0xd: if (played(o)) { o->v2a = 0xe; anim(o, 4); play(o); } break;
    case 0xe:
        if (!played(o)) {
            if (o->idx == 8 && o->timer == 1) spawn(0x39, o->x + 0x100000, o->y - 0x600000, o->z);
        } else rock_next(o);
        break;
    case 0x1a: case 0x2e: if (played(o)) rock_next(o); break;
    case 0x37:                          /* fade the sky to the rock palette (0x100636f0); not ported */
        if ((o->w[0] += 4) > 0xff) o->w[0] = 0xff;
        if (o->w[0] == 0xff) rock_next(o);
        break;
    case 0x39:
        play(o);
        if (++o->w[0] > 200) { titan_gone(o); return; }
        break;
    }
    titan_place(o, 6);
}

static void rock_throw(Object *o)      /* FUN_10036d70 (0x39 rock thrown at the screen) */
{
    if (o->v2a == 2) {
        o->scale += 0x37;
        o->x += o->facing ? -o->v[0] : o->v[0];
        o->y += o->v[4];
        words(o);
    }
    if (--o->w[0] < 0) {               /* FUN_10036e90: impact - debris over the player, shake */
        snd(0x252, C.px);
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < 7; i++) {
                uint32_t e = 0x100632b8u + 8u * (uint32_t)i;
                int k = pass ? 2 : 1;
                Object *d = spawn(0x3a, FX(pe_s16(gO->pe, e) * k) + C.ax + 0x300000, FX(pe_s16(gO->pe, e + 2) * k) + C.ay + 0x500000,
                                  FX(pe_s16(gO->pe, e + 4) * k) + C.az + 0x17c0000);
                if (!d) continue;
                d->v2a = pass ? 0x2e : 2;
                anim(d, i & 7); play(d);
                d->facing = 0;
                if (!pass) { d->v[0] = (0x200 - rnd(0x400)) << 8; d->v[4] = (-0xc00 - rnd(0x180)) << 8; d->v[8] = -0x20000; }
                else { d->v[0] = (0x600 - rnd(0xc00)) << 8; d->v[4] = (-0x200 - rnd(0x200)) << 8; d->v[8] = -0x10000; }
            }
        C.shake_i = 1;
        kill(o);
    }
}

static void debris(Object *o)          /* FUN_10036e10 (0x3a rock debris) */
{
    int vy = o->v[4]; o->v[4] += 0x8000;
    o->x += o->facing ? -o->v[0] : o->v[0];
    o->y += (vy + 0x8000) * 2;
    o->x += o->v[0];
    o->z += o->v[8] * 2;
    play(o);
    if (o->w[0] < o->y) kill(o);
}

static void ice_next(Object *o)        /* FUN_100384c0 */
{
    if (o->x < 0) { o->v2a = 0x39; anim(o, 1); o->w[0] = 0; play(o); return; }
    rnd(0x8000);
    if (rnd(0x8000) < 0x3000) { o->v2a = 0x2e; anim(o, 1); }
    else if (rnd(0x8000) < 0x4000) { o->v2a = 0x2f; anim(o, 2); }
    else { o->v2a = 0x30; anim(o, 3); }
    play(o);
}

static void ice_titan(Object *o)       /* FUN_10038610 (0x29 ice Titan) */
{
    switch (o->v2a) {
    case 0x2e: case 0x2f: case 0x30: if (played(o)) ice_next(o); break;
    case 0x37:
        play(o);
        if ((o->w[0] += 4) > 0xff) o->w[0] = 0xff;
        if (o->w[0] == 0xff) ice_next(o);
        break;
    case 0x39:
        play(o);
        if (++o->w[0] > 200) { titan_gone(o); return; }
        break;
    }
    titan_place(o, 6);
}

static void lava_next(Object *o)       /* FUN_10038ea0 */
{
    int r = rnd(0x8000);
    if (o->x < 0) { o->v2a = 0x39; anim(o, 1); o->w[0] = 0; }
    else if (r < 0x3000) { o->v2a = 0x2e; anim(o, 1); }
    else if (r < 0x6000) { o->v2a = 0x1a; anim(o, 2); }
    else { o->v2a = 0x31; anim(o, 3); }
}

static void lava_titan(Object *o)      /* FUN_10039090 (0x2a lava Titan) */
{
    switch (o->v2a) {
    case 0x1a: case 0x2e:
        if (played(o)) lava_next(o);
        /* fall through (as the original) */
    case 0x31:
        if (!played(o)) {
            if (o->idx == 0xd)          /* lava spray */
                for (int k = 0; k < 4; k++) { static const int D[4] = {0x2e, 0x32, 0x34, 0x36}; spawn(0x33, o->x, o->y - FX(D[k]), 0); }
        } else lava_next(o);
        break;
    case 0x37:
        play(o);
        if ((o->w[0] += 2) > 0xff) o->w[0] = 0xff;
        if (o->w[0] == 0xff) lava_next(o);
        else if ((o->w[0] & 7) == 0 && o->w[0] < 0x90) {
            spawn(0x56, 0x1400000, (300 - o->w[0]) << 16, C.pz);   /* lava blobs racing across the screen */
            if ((o->w[0] & 1) == 0) snd(rnd(0x8000) < 0x4000 ? 0x6a4c : 0x6a4d, C.px);
        }
        break;
    case 0x39: words(o); o->w[0]++; break;
    }
    titan_place(o, 6);
    if (o->x < -0x280000) { C.titan_cam = 0; kill(o); }
}

static void tornado_next(Object *o)    /* FUN_10039dc0 */
{
    int r = rnd(0x8000);
    if (o->x < 0) { o->v2a = 0x39; anim(o, 1); o->w[0] = 0; return; }
    if (r < 0x2000) { o->v2a = 0x2e; anim(o, 1); return; }
    if (r < 0x7000) {
        o->v2a = 0x32; anim(o, 2);
        static const int S[4] = {599, 600, 0x259, 0x25a};
        int k = rnd(0x40) >> 4;
        snd(S[k & 3], C.px);
        return;
    }
    o->v2a = 0x34; anim(o, 4); play(o);
}

static void tornado_titan(Object *o)   /* FUN_1003a040 (0x2b tornado) */
{
    switch (o->v2a) {
    case 0x2b:
        o->scale += 0x2c; o->y += 0x10000; play(o);
        if (++o->w[0] == 0x50) o->v2a = 0x2c;
        break;
    case 0x2c:
        o->scale -= 0x2c; o->y -= 0x10000; play(o);
        if (--o->w[0] == 0) tornado_next(o);
        break;
    case 0x2e: case 0x32: case 0x33: case 0x34:
        C.spiral++;                     /* FUN_10039ff0: wobble */
        o->y += sinv((C.spiral & 0x3f) * 0x10) * 6 / 2;
        o->scale = (uint16_t)(((sinv((C.spiral & 0x7f) * 8) << 1) >> 11) + 0x120);
        if (played(o)) tornado_next(o);
        break;
    case 0x37:
        play(o);
        if ((o->w[0] += 4) > 0xff) o->w[0] = 0xff;
        if (o->w[0] == 0xff) tornado_next(o);
        else if ((o->w[0] & 0x1f) == 0) {
            Object *p = spawn(0x57, C.ax + 0x12c0000, FX(rnd(0x32) + 0x1e - o->w[0]) + C.ay, C.az);
            if (p) { anim(p, (o->w[0] >> 5) & 3); play(p); }
        }
        break;
    case 0x39:
        play(o);
        o->w[0]++;
        if (o->w[0] > 200) { titan_gone(o); obj_sound_stop(gO, 0x256); return; }   /* LV_TITANS: never leave the loop running */
        if ((o->w[0] & 0xf) == 0 && o->w[0] < 0xa0) {
            Object *p = spawn(0x5b, C.ax - 0x960000, FX(rnd(0x1e) - 300 + o->w[0] * 2) + C.ay, C.az);
            if (p) { anim(p, (o->w[0] >> 4) & 1); play(p); }
        }
        break;
    }
    o->x = (int32_t)((((int64_t)(C.titan_cam - C.camx)) << 16) / 0xc) + 0x1900000;
    if (o->x < -0x280000) {
        C.tornado_lock = -1; C.titan_cam = 0; kill(o);
        /* LV_TITANS: FUN_1003a040 sets DAT_10064474 = -1 here; the wind object 0x38 (FUN_1003a4a0) then calls
         * FUN_10007f8e(0x256, 0) = stops the tornado's looping wind sound that FUN_10039f20 started (it looped for ever) */
        obj_sound_stop(gO, 0x256);
    }
}

/* ----------------------------------------------------------- projectiles */

static void lava_pillar(Object *o)     /* FUN_100393b0 (0x31 lava rising behind the clouds, screen space) */
{
    words(o);
    if (o->v2a == 1) { if (o->w[0]-- < 0) o->v2a = 0x1c; }
    else if (o->v2a == 0x1c) { o->y += 0x10000; if (o->y > 0x12c0000) kill(o); obj_sound_stop(gO, 0x246); }   /* AUDSHELL: FUN_10007f8e(0x246) ends the lava loop */
    else if (o->v2a == 0x36) { o->y -= 0x8000; if (--o->w[0] == 0) { o->v2a = 1; o->w[0] = 0x1e0; } }
}

static void lava_ball(Object *o)       /* FUN_10039530 (0x32 lava ball thrown up from below) */
{
    int vy = o->v[4]; o->v[4] += 0x8000;
    if (o->b95 == 0xff && vy + 0x8000 > 0) { o->b95 = 0; anim(o, 1); }
    o->y += o->v[4];
    o->x += o->facing ? -o->v[0] : o->v[0];
    o->z -= 0x20000;
    if (o->z < C.pz) o->z = C.pz;
    play(o);
    if (touch_peg(o)) { hurt(3, o); kill(o); return; }
    if (offscreen(o, 200, 200)) kill(o);
}

static void lava_spray(Object *o)      /* FUN_10039750 (0x33, screen space) */
{
    int vy = o->v[4];
    o->scale += 0xc;
    o->v[4] += 0x9000;
    o->y += vy + 0x9000;
    o->x += o->facing ? -o->v[0] : o->v[0];
    words(o);
    if (o->y > 0xdc0000 || o->x > 0x1680000 || o->x < -0x280000) kill(o);
}

static void tornado_arm(Object *o)     /* FUN_100398b0 (0x34 spinning arm, screen space, in front of Pegasus) */
{
    o->rot[2] -= 8;
    o->x -= 0x20000;
    o->z = C.pz;
    words(o);
    int a = o->rot[2] & 0x3ff;
    int dx = ((sinv(a) * 0xb400) >> 24) + I(o->x) - (int)C.psx;
    int dy = ((sinv((a + 0x100) & 0x3ff) * -0xb400) >> 24) + I(o->y) - (int)C.psy;
    if (dx * dx + dy * dy < 0x9c4) hurt(2, o);
    if (o->w[0]-- < 0) kill(o);
}

static void lava_blob(Object *o)       /* FUN_10039c50 (0x56, screen space, foreground) */
{
    /* FUN_1003c510: the blob's screen box against Pegasus' box at its screen position */
    int b[6];
    if (C.have_pbody && obj_box(gO, o, 1, b)) {
        int pb[6] = {C.pbody[0] - C.px / 65536 + (int)C.psx, C.pbody[1] - C.py / 65536 + (int)C.psy, -1000,
                     C.pbody[3] - C.px / 65536 + (int)C.psx, C.pbody[4] - C.py / 65536 + (int)C.psy, 1000};
        b[2] = -10; b[5] = 10;
        if (obj_boxes_overlap(b, pb)) hurt(2, o);
    }
    o->x += o->facing ? -o->v[0] : o->v[0];
    words(o);
    if (o->x < -0x320000) kill(o);
}

static void ice_hit(Object *o)         /* FUN_10038880: the ice chunk shatters */
{
    snd(0xe242, o->x);
    int k = rnd(0x8000);
    for (uint32_t e = 0x10063c80u; e < 0x10063ce4u; e += 8) {
        Object *s = spawn(0x68, FX(pe_s16(gO->pe, e)) + o->x, FX(pe_s16(gO->pe, e + 2)) + o->y, FX(pe_s16(gO->pe, e + 4)) + o->z);
        if (s) { anim(s, k & 3); play(s); k++; }
    }
    kill(o);
}

static void ice_chunk(Object *o)       /* FUN_100389c0 (0x52 / 0x53 falling ice) */
{
    if (o->w[0] == 0 && C.py - o->y < 0x200000) {
        o->w[0] = 1;
        int k = rnd(0x30) >> 4;
        snd(k == 0 ? 0x6a43 : k == 1 ? 0x6a44 : 0x6a45, o->x);
    }
    o->rot[2] += (int16_t)o->v[6];
    o->x += o->facing ? -o->v[0] : o->v[0];
    o->y += o->v[4];
    words(o);
    if (hit_by_player(o)) { maybe_drop(o); snd(0x6a43, o->x); ice_hit(o); return; }
    if (touch_peg(o)) { hurt(2, o); ice_hit(o); return; }
    if (o->y - C.py > 0x2000000) kill(o);
}

static void ice_shard(Object *o)       /* FUN_100387f0 (0x68) */
{
    int vy = o->v[4]; o->v[4] += 0x8000;
    o->x += o->facing ? -o->v[0] : o->v[0];
    o->rot[0] += 6; o->rot[1] += 10; o->rot[2] += 14;
    o->y += (vy + 0x8000) * 2;
    o->x += o->v[0];
    o->z += o->v[8] * 2;
    words(o);
    if (o->y - C.ay > 0x640000) kill(o);
}

static void ice_gen(Object *o)         /* FUN_10038b30 (0x51 ice chunk generator) */
{
    static const int32_t T[8][4] = {
        {0x1200000, 0, 0x60000, 0xa0000}, {0xe00000, 0, 0x20000, 0xc0000}, {0x400000, 0, (int32_t)0xfff80000, 0x90000},
        {0xc00000, 0, 0, 0xc0000}, {0xa00000, 0, (int32_t)0xfffe0000, 0xc0000}, {0x800000, 0, (int32_t)0xfffc0000, 0xb0000},
        {0x1000000, 0, 0x40000, 0xb0000}, {0x600000, 0, (int32_t)0xfffa0000, 0xa0000}};
    int d = (o->x - C.ax) >> 16, i = d / 64 + 3;
    if ((C.frame & 0xf) == 0 && i >= 0 && i < 8) {
        Object *p = spawn((C.frame & 0x10) ? 0x52 : 0x53, T[i][0] + o->x, T[i][1] / 4 + o->y, o->z);
        if (p) { p->v[0] = T[i][2]; p->v[4] = T[i][3]; }
    }
    if (C.px - o->x > 0x1300000) kill(o);
}

static void cloud_rock(Object *o)      /* FUN_1003a810 (0x41 rocks hurled up from below) */
{
    o->x += o->facing ? -o->v[0] : o->v[0];
    int vy = o->v[4];
    o->rot[0] += 0xc; o->rot[1] += 2;
    o->v[4] = vy + 0xb000;
    o->rot[2] += 1;
    o->y += vy + 0xb000;
    o->z -= 0xa0000;
    play(o);
    if (hit_by_player(o)) { o->v[4] = 0; maybe_drop(o); snd(0x6a43, o->x); }     /* FUN_10039d40 */
    else if (touch_peg(o)) { hurt(2, o); o->v[4] = 0; snd(0x6a43, o->x); }
    if (o->w[0] < o->y) kill(o);
}

static void storm_rock(Object *o, int dir)   /* FUN_1003aa70 (0x57) / FUN_1003ab50 (0x5b) tornado debris */
{
    if (hit_by_player(o)) { o->v[4] = 0; maybe_drop(o); snd(0x6a43, o->x); }
    else if (touch_peg(o)) { hurt(2, o); o->v[4] = 0; snd(0x6a43, o->x); }
    int vy = o->v[4];
    o->rot[0] += (int16_t)(dir * 0xc);
    o->v[4] = vy + 0xc000;
    o->x += o->facing ? -o->v[0] : o->v[0];
    o->rot[2] += 0x14;
    o->y += vy + 0xc000;
    play(o);
    if (dir < 0 ? C.ax - o->x > 0x12c0000 : o->x - C.ax > 0x12c0000) kill(o);
}

static void column(Object *o)          /* FUN_10039b00 (0x3d lava columns) */
{
    if (touch_peg(o)) hurt(2, o);       /* FUN_1003c4d0 */
    o->x -= 0x20000;
    words(o);
    if (C.ax - o->x > 0x12c0000) kill(o);
}

static void orb(Object *o)             /* FUN_1003a550 (0x3f orbs circling a 0x3e pillar) */
{
    Object *p = parent_of(o);
    if (!p || p->type != 0x3e || p->w[0] != 0x12345675) { kill(o); return; }
    int u = o->w[1] + p->rot[1];
    o->x = sinv(u) * 0x200 + p->x;
    o->rot[1] += 0x22;
    o->z = C.pz + 0x800000 - sinv((u + 0x100) & 0x3ff) * 0x200;
    play(o);
    if (o->v2a == 2 && touch_peg(o)) hurt(2, o);
}

/* ----------------------------------------------------------- harpies (wave 0x61 -> formation 0x62..0x67 -> 0x5c) */

static void cmd_step(Object *o)        /* FUN_1003c230: movement command list {delay, cmd, arg} */
{
    if (!o->w[2] || --o->w[2] != 0) return;
    uint32_t p = (uint32_t)o->w[1];
    for (int g = 0; g < 64; g++) {
        int delay = (int)pe_u32(gO->pe, p), cmd = (int)pe_u32(gO->pe, p + 4);
        int32_t arg = (int32_t)pe_u32(gO->pe, p + 8);
        o->w[2] = delay;
        uint32_t np = p + 12;
        switch (cmd) {
        case 0: o->w[2] = 0; return;
        case 1: o->v[0] = arg; break;
        case 2: o->v[4] = arg; break;
        case 3: o->v[8] = arg; break;
        case 5: anim(o, arg); play(o); break;
        case 6: o->v[2] = arg; break;
        case 7: o->v[6] = arg; break;
        case 8: o->v[10] = arg; break;
        case 9: np = (uint32_t)arg; break;
        case 10: o->v2a = (uint16_t)arg; break;
        default: np = p + 8; break;
        }
        p = np;
        if (o->w[2] >= 0) break;
    }
    o->w[1] = (int32_t)p;
}

static void fly(Object *o)             /* FUN_1003c330 */
{
    o->x += *C.speed / 2;
    o->x += o->v[0] + o->v[2];
    o->v[0] += o->v[2];
    o->v[4] += o->v[6];
    o->y += o->v[4];
    o->v[8] += o->v[10];
    o->z += o->v[8];
    if (o->v[4] < 0) { if (o->y < FX(16)) o->y = FX(16); }
    else {
        int f = FX(floor_at(I(o->x), I(o->z)) - 0x40);
        if (f < o->y) o->y = f;
    }
}

static void track_y(Object *o)         /* FUN_1003c5c0 */
{
    int d = o->y - C.py;
    if (d < -0x80000) o->y += 0x20000;
    else if (d > 0x80000) o->y -= 0x20000;
}

static void leave_group(Object *o)
{
    Object *p = parent_of(o);
    if (p && p->w[4] > 0) p->w[4]--;
    o->w[5] = 0;
}

static void harpy_body(Object *o)      /* FUN_1003b200 */
{
    cmd_step(o);
    fly(o);
    if (hit_by_player(o)) {
        snd((rnd(0x8000) & 4) ? 0xe25d : 0xe25e, o->x);
        maybe_drop(o);
        spark(o->x, o->y, o->z);                      /* TITANS3: 0x402a at the harpy */
        o->flags50 &= ~0x200u;
        o->v2a = 0x23; o->v[4] = 0;
        anim(o, 1); play(o);
        Object *p = parent_of(o);
        if (p && p->w[4] == 1) {        /* FUN_1003c460: the last of a formation leaves a reward */
            if (++C.drop > 9) C.drop = 0;
            drop(o, (int)pe_u32(gO->pe, 0x100655a8u + 4u * (uint32_t)C.drop));
        }
        leave_group(o);
        return;
    }
    if (C.pstate != 0x24 && C.pstate != 0x23 && touch_peg(o)) hurt(2, o);   /* FUN_1003c4d0 */
}

static void harpy(Object *o)           /* FUN_1003b310 (0x5c) */
{
    if ((C.frame & 0xf) == 0 && rnd(0x8000) < 600) snd(rnd(0x8000) < 0x4000 ? 0xe25b : 0xe25c, o->x);
    switch (o->v2a) {
    case 9:
        if (played(o)) { anim(o, 0); play(o); o->v2a = 2; }
        harpy_body(o);
        break;
    case 0x10: track_y(o); play(o); harpy_body(o); break;
    case 0x23:
        o->v[4] += 0x8000; o->y += o->v[4];
        if (played(o)) { leave_group(o); kill(o); return; }
        break;
    case 0x40:
        o->x += *C.speed;
        if (--o->w[0] < 0) o->v2a = 2;
        break;
    case 0x4f: {
        Object *p = parent_of(o);
        int old = o->w[0]; o->w[0] += 8;
        if (p) {
            o->x = sinv((old + 0x108) & 0x3ff) * 0x100 + p->x;
            o->y = p->y - sinv(o->w[0] & 0x3ff) * 0x100;
        }
        play(o); harpy_body(o);
        break;
    }
    default: play(o); harpy_body(o); break;
    }
    if (o->used && C.ax - o->x > 0x1900000) { leave_group(o); kill(o); }
}

static Object *member(Object *f, int32_t x, int32_t y, int32_t z)    /* FUN_1002b070 + FUN_1003b190 */
{
    Object *h = spawn(0x5c, x, y, z);
    if (!h) return NULL;
    set_parent(h, f);
    f->w[4]++;
    return h;
}

static void formation_init(Object *f)  /* FUN_1003b7f0 / 1003b530 / 1003b640 / 1003b730 / 1003b8b0 */
{
    int32_t z = C.pz;
    f->w[4] = 0;
    switch (f->type) {
    case 0x62: {
        int32_t x = C.ax + 0x15e0000; f->x = x; f->y = mid_at(I(x), I(z));
        for (int i = 0; i < 8; i++) {
            Object *h = member(f, x, f->y, z);
            if (!h) break;
            h->w[0] = i * 0x80; h->v2a = 0x4f; h->w[1] = 0; h->w[2] = 0;
        }
        break;
    }
    case 0x63: {
        int32_t y = C.ay, x = f->x + 0x12c0000;
        uint32_t tab = 0x10064d80u; int st = 2;
        int v = C.form++ & 3;
        if (v == 0) y -= 0x640000; else if (v == 1) y += 0x640000; else if (v == 2) st = 0x10; else tab = 0x10064db0u;
        for (int t = 1; t < 0x19; t += 4, x += 0x400000) {
            Object *h = member(f, x, y, z);
            if (!h) break;
            h->w[2] = t; h->w[1] = (int32_t)tab; h->v2a = (uint16_t)st;
        }
        break;
    }
    case 0x64: {
        int32_t y = C.py; uint32_t tab = 0x10064e08u;
        if ((C.form++ & 1) == 0) y -= 0x640000; else { tab = 0x10064ea8u; y += 0x640000; }
        for (int t = 1, c = 0; t < 0x19; t += 4, c += 8) {
            Object *h = member(f, f->x + 0x12c0000, y, z);
            if (!h) break;
            h->w[2] = t; h->w[0] = c; h->v2a = 0x40; h->w[1] = (int32_t)tab;
        }
        break;
    }
    case 0x65: {
        int32_t x = f->x + 0x12c0000, y = mid_at(I(x), I(z)) - 0x600000;
        for (int t = 1, c = 0; t < 0x19; t += 4, c += 8) {
            Object *h = member(f, x, y, z + 0x2000000);
            if (!h) break;
            h->w[2] = t; h->w[0] = c; h->v2a = 0x40; h->w[1] = 0x10064f48;
        }
        break;
    }
    default: {                          /* 0x66 / 0x67 */
        for (int t = 1, c = 0; t < 0x19; t += 4, c += 8) {
            Object *h = member(f, f->x + 0x12c0000, C.py - 0x640000, z);
            if (!h) break;
            h->w[2] = t; h->w[0] = c; h->v2a = 0x40; h->w[1] = 0x10065008;
        }
        break;
    }
    }
}

static void formation(Object *f)       /* FUN_1003b980 */
{
    f->x += *C.speed / 2;
    if (f->w[4] <= 0) kill(f);
}

static void wave(Object *o)            /* FUN_1003ba00 (0x61 harpy wave list) */
{
    if (--o->w[0] >= 0) return;
    uint32_t p = (uint32_t)o->w[1];
    int cmd = (int)pe_u32(gO->pe, p);
    p += 4;
    static const uint16_t F[7] = {0, 0x63, 0x62, 0x64, 0x65, 0x66, 0x67};
    if (cmd == 0) { kill(o); return; }
    if (cmd >= 1 && cmd <= 6) spawn(F[cmd], cmd == 1 ? C.ax : C.ax + 0x12c0000, C.ay, C.pz);
    o->w[0] = (int)pe_u32(gO->pe, p);
    o->w[1] = (int32_t)(p + 4);
}

/* ----------------------------------------------------------- the spy (0x5d) */

static void spy_hit(Object *o)         /* FUN_1003bcb0 */
{
    if (o->v2a == 0x22) { if (touch_peg(o)) hurt(2, o); return; }
    if (!hit_by_player(o)) { if (touch_peg(o)) hurt(2, o); return; }
    snd((rnd(0x8000) & 4) ? 0xe264 : 0xe265, o->x);
    drop(o, 0x241);
    o->v[4] = 0; o->flags50 &= ~0x200u;
    anim(o, 2); play(o);
    o->hp--;
    o->v2a = 0x24;
    if (o->hp < 1) {
        drop(o, 0x24a);
        Object tmp = *o; tmp.x += 0x200000; tmp.y -= 0x280000;
        drop(&tmp, 0x243);
        o->v2a = 0x23; o->v[4] = -0x80000;
    }
}

static void spy(Object *o)             /* FUN_1003bde0 */
{
    if (getenv("TITANS_LOG") && C.frame % 60 == 0) printf("[%ld] spy state %x pos %d,%d,%d hp %d speed %x cmd %x/%d frame %d peg %d,%d\n", C.frame, o->v2a, I(o->x), I(o->y), I(o->z), o->hp, *C.speed, o->w[1], o->w[2], o->frame, I(C.px), I(C.py));
    switch (o->v2a) {
    case 2: track_y(o); /* fall through */
    case 0x22: case 0x51: play(o); spy_hit(o); cmd_step(o); fly(o); return;
    case 0x15:
        o->x = C.ax + 0x1100000; play(o);
        *C.speed -= 0x888;
        if (*C.speed < 0) { *C.speed = 0; anim(o, 0); play(o); o->v2a = 0x50; }
        break;
    case 0x23:
        if (*C.speed < 0x40000) *C.speed += 0x1111;
        o->v[4] += 0x8000; o->y += o->v[4]; play(o);
        if (o->y - C.ay > 0x12c0000) { *C.speed = 0x40000; kill(o); }
        break;
    case 0x24:
        if (played(o)) { o->w[1] = 0x100652e8; o->w[2] = 1; o->v2a = 2; anim(o, 0); play(o); }
        break;
    case 0x37:
        o->x = C.ax + 0x1100000; play(o);
        *C.speed -= 0x888;
        if (*C.speed < 0x20000) { o->v2a = 0x15; snd(0x25f, o->x); }
        break;
    case 0x40: if (--o->w[0] < 0) o->v2a = 2; break;
    case 0x50:
        o->x -= 0x20000; track_y(o);
        if (o->x - C.ax < 0x900000) { o->v2a = 2; anim(o, 0); o->w[1] = 0x100651f8; o->w[2] = 1; }
        play(o);
        break;
    }
}

/* ----------------------------------------------------------- gods (0x2c..0x30) and Zeus (0x45) */

static void god(Object *o)             /* FUN_1003ac80 */
{
    if (o->v2a == 2) {
        o->x += 0x60000; o->y -= 0x60000;
        if (played(o)) maybe_drop(o);
        if ((C.frame & 9) == 0) maybe_drop(o);
        if (C.py - o->y > 0x1900000) { kill(o); return; }
    } else if (o->v2a == 0x2d) {
        if (played(o)) { o->v2a = 2; anim(o, 2); play(o); }
    } else if (o->v2a == 0x35) {
        play(o);
        if (touch_peg(o)) { snd(0x627b, o->x); o->v2a = 0x2d; anim(o, 1); play(o); }
    } else { o->x += 0x60000; o->y -= 0x60000; play(o); }
    if (o->v2a == 2 && offscreen(o, 0x200, 0x200)) kill(o);
}

/* ----------------------------------------------------------- dispatch */

int titans_ai_init(Objects *O, Object *o, uint32_t init)
{
    if (O->L->index != 9 || (o->type & 0x7000)) return 0;
    gO = O;
    (void)init;
    o->anim = -1; o->facing = 0;
    switch (o->type) {
    case 0x28: titan_init(o, 0x820000, 1); C.shake_i = 0; break;                            /* FUN_10038090 */
    case 0x29: if (C.titan_cam) return kill(o), 1; titan_init(o, 0x800000, 1); break;       /* FUN_10038590 */
    case 0x2a:                                                                              /* FUN_10038f50 */
        if (C.titan_cam) return kill(o), 1;
        titan_init(o, 0x820000, 3);
        for (int k = 0; k < 3; k++) {
            Object *p = spawn(0x31, o->x, o->y, o->z);
            if (!p) continue;
            p->x = k == 0 ? 0x500000 : k == 1 ? 0xf60000 : 0xa80000;
            if (k == 2) { p->y -= 0xa0000; p->scale = 0x180; }
        }
        snd(0x246, C.px);
        break;
    case 0x2b:                                                                              /* FUN_10039f20 */
        if (C.titan_cam) return kill(o), 1;
        titan_init(o, 0x500000, 1);
        o->scale = 0x120; snd(0x256, C.px);
        break;
    case 0x2c: case 0x2d: case 0x2e: case 0x2f: case 0x30: case 0x45:                       /* FUN_1003abd0 */
        o->z += 0xc0000;
        if (o->type == 0x30 || o->type == 0x45) { o->facing = 0xff; o->flags54 |= 0x10; }
        o->v2a = 0x35; next_anim(o); reset_anim(o);
        break;
    case 0x31: screen_obj(o, 0x4b3); reset_anim(o); o->y = 0x12c0000; o->w[0] = 0x78; o->scale = 0x1c0; o->v2a = 0x36; o->pairs = 0; break;
    case 0x32:                                                                              /* FUN_10039460 */
        next_anim(o); reset_anim(o);
        o->v[4] = (rnd(0x400) - 0x1400) << 8; o->b95 = 0xff;
        o->v[0] = (0x200 - rnd(0x400)) << 8; o->scale = (uint16_t)(rnd(0x80) + 0x100);
        o->facing = 0xff; o->flags54 |= 0x10;
        { int k = rnd(0x80) >> 4; if (k <= 2) snd(0x6a49 + k, C.px); }
        break;
    case 0x33: screen_obj(o, 0x4b7); o->scale = 0x20; reset_anim(o); o->v[4] = (rnd(0x300) - 0x600) << 8; o->v[0] = (0x400 - rnd(0x800)) << 8; o->pairs = 0; break;
    case 0x34: screen_obj(o, -4); reset_anim(o); o->scale = 0x180; o->rot[2] = 0x180; o->w[0] = 0x60; o->pairs = 0; break;
    case 0x35: o->x = 0x12c0000; o->y = 0xaa0000; o->w[0] = 0; snd(0x6a4e, C.px); o->frame = 0; break;   /* FUN_100399c0 */
    case 0x37: o->frame = 0; break;
    case 0x39: screen_obj(o, 0x4b7); o->v2a = 2; reset_anim(o); o->v[4] = 0x2e0000; o->v[0] = (0xa00000 - o->x) / 5; o->w[0] = 5; o->scale = 0xe0; o->pairs = 0; break;
    case 0x3a: reset_anim(o); o->w[0] = o->y; break;                                       /* FUN_10036df0 */
    case 0x3b: case 0x3c: {                                                                 /* FUN_10039bf0 / 10039c00 / 10039b60 */
        int fac = o->type == 0x3c ? 0xff : 0;
        if (fac) o->x -= 0x2700000;
        snd(0x6a4e, C.px);
        int32_t y = o->y;
        for (int k = 0; k < 3; k++, y -= 0x500000) {
            Object *p = spawn(0x3d, o->x, y, o->z);
            if (p) { p->facing = (uint8_t)fac; p->flags54 = (p->flags54 & ~0x10u) | (fac & 0x10); }
        }
        return kill(o), 1;
    }
    case 0x3d: o->v[0] = -0x20000; o->scale = 0x200; reset_anim(o); o->pairs = 0; break;
    case 0x3e: {                                                                            /* FUN_1003a610 */
        o->w[0] = 0x12345675; o->frame = 0;
        int ph = 0;
        for (uint32_t e = 0x100644f8u; e < 0x10064510u; e += 4, ph += 0x400) {
            Object *p = spawn(0x3f, o->x, FX(pe_s16(gO->pe, e)) + o->y, o->z);
            if (!p) continue;
            anim(p, pe_s16(gO->pe, e + 2)); play(p);
            set_parent(p, o); p->w[1] = ph / 6;
        }
        spawn(0x40, o->x, o->y, o->z);                 /* TITANS3: the pillar's spinning sprite mesh */
        break;
    }
    case 0x3f: next_anim(o); o->flags54 |= MODEL; break;
    case 0x40: case 0x42: case 0x43:              /* TITANS3: FUN_1003a730 / 1003a8a0 / 1003a910 + FUN_10026700 */
        o->flags50 |= 0x800; o->flags54 |= 0x20000600u; o->b23 = 4;
        o->w[7] = o->type == 0x40 ? 0x10064510 : o->type == 0x42 ? 0x100646e0 : 0x100647d0;   /* +0x5c mesh */
        o->frame = 0;
        break;
    case 0x41:                                                                              /* FUN_1003a7a0 */
        o->w[0] = o->y; o->flags54 |= MODEL;
        o->v[0] = (0x400 - rnd(0x800)) * 0x100 + *C.speed; o->v[4] = -0x150000;
        next_anim(o); anim(o, rnd(6)); play(o);
        break;
    case 0x44: o->frame = 0; break;
    case 0x46: case 0x47: case 0x48: case 0x49: case 0x4a: case 0x4b:                       /* FUN_1003b050..1003b140 */
        if (o->x < C.ax) return kill(o), 1;
        *C.zpush += o->type == 0x46 ? 0x80 : o->type == 0x48 ? 0x20 : o->type == 0x4b ? -0x80 : 0;
        o->frame = 0;
        break;
    case 0x51: if (o->x - C.ax < 0xc00000) return kill(o), 1; o->w[0] = 0; o->frame = 0; break;   /* FUN_10038b00 */
    case 0x52: case 0x53:                                                                   /* FUN_10038950 */
        o->y = (rnd(0xc00) - 0x20000) * 0x100 + C.ay;
        o->flags54 |= MODEL; o->scale = 0x200; o->rot[0] = 0x280; o->rot[1] = 0x20;
        o->v[6] = rnd(0x20); reset_anim(o); o->w[0] = 0; o->pairs = 0;
        break;
    case 0x56: screen_obj(o, -1000); o->scale = 0x1c0; o->v[0] = -0x280000; o->flags54 |= 0x80005; reset_anim(o); o->pairs = 0; break;
    case 0x57: case 0x5b:                                                                   /* FUN_1003aa10 / 1003aaf0 */
        o->scale = 0x200; o->flags54 |= MODEL;
        if (o->type == 0x57) { o->rot[0] = 0x3c0; o->rot[1] = 0x300; o->rot[2] = (int16_t)rnd(0x400); o->v[0] = -0xc0000; }
        else { o->rot[0] = (int16_t)rnd(0x400); o->rot[1] = (int16_t)rnd(0x400); o->v[0] = 0xc0000; }
        o->v[4] = 0; next_anim(o);
        break;
    case 0x5c:                                                                              /* FUN_1003b190 */
        o->flags50 = 0x10000202; o->z += 0x80000; o->v2a = 2;
        memset(o->v, 0, sizeof o->v);
        next_anim(o); anim(o, 0); play(o); o->w[2] = 0;
        break;
    case 0x5d:                                                                              /* FUN_1003bc20 */
        o->z = C.pz + 0x80000;
        if (o->x < C.ax) return kill(o), 1;
        o->hp = 4; o->flags50 = 0x10000002; memset(o->v, 0, sizeof o->v);
        next_anim(o); anim(o, 0); play(o); o->w[2] = 0; o->v2a = 0x37;
        break;
    case 0x61:                                                                              /* FUN_1003b9d0 */
        o->frame = 0; o->w[0] = 0;
        o->w[1] = (int32_t)pe_u32(gO->pe, 0x10064d28u + 4u * (uint32_t)(C.wave & 7)); C.wave++;
        break;
    case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67: o->frame = 0; formation_init(o); break;
    case 0x68:                                                                              /* FUN_10038740 */
        o->flags54 |= MODEL; o->scale = (uint16_t)(rnd(0x180) + 0x40);
        o->v[0] = *C.speed - rnd(0x800) * 0x100; o->v[4] = -rnd(0x100) * 0x100 - 0x50000; o->v[8] = -rnd(0x400) * 0x100 + 0x8000;
        o->rot[0] = o->rot[1] = o->rot[2] = (int16_t)rnd(0x400);
        break;
    default: return 0;
    }
    /* TITANS3: the semi-transparency bits the inits set in +0x54 (FUN_100311a0: bit 2 = semi-transparent prim,
     * bits 0-1 = PSX blend mode): lava pillars 0x31 / lava balls 0x32 / tornado arms 0x34 / lava Titan |5,
     * lava spray 0x33 |7, tornado |4 */
    if (o->used) switch (o->type) {
    case 0x31: case 0x32: case 0x34: case 0x2a: o->flags54 |= 5; break;
    case 0x33: o->flags54 |= 7; break;
    case 0x2b: o->flags54 |= 4; break;
    }
    return 1;
}

void titans_ai_tick(Objects *O, Object *o)
{
    gO = O;
    if (o->w[11] == DROP_TAG) { falling_pickup(o); return; }   /* TITANS3 */
    if (o->w[11] == SPARK_TAG) { if (words(o) || --o->life <= 0) kill(o); return; }
    switch (o->type) {
    case 0x28: rock_titan(o); break;
    case 0x29: ice_titan(o); break;
    case 0x2a: lava_titan(o); break;
    case 0x2b: tornado_titan(o); break;
    case 0x2c: case 0x2d: case 0x2e: case 0x2f: case 0x30: god(o); break;
    case 0x45: play(o); break;                                  /* Zeus: the touch test is in titans.c */
    case 0x31: lava_pillar(o); break;
    case 0x32: lava_ball(o); break;
    case 0x33: lava_spray(o); break;
    case 0x34: tornado_arm(o); break;
    case 0x35:                                                  /* FUN_100399f0: tornado arm launcher */
        if (++o->w[0] > 800) { kill(o); break; }
        if (o->w[0] < 100 && (o->w[0] & 7) == 0) spawn(0x34, o->x + 0x2e0000, o->y, o->z);
        o->x -= 0x20000;
        break;
    case 0x37:                                                  /* FUN_10039a70: lava balls from below */
        if ((C.frame & 0x1f) == 0) spawn(0x32, C.px + 0x400000, C.py + 0x800000, C.pz + 0x800000);
        if (C.px - o->x > 0x1000000) kill(o);
        break;
    case 0x39: rock_throw(o); break;
    case 0x3a: debris(o); break;
    case 0x3d: column(o); break;
    case 0x3e:                                                  /* FUN_1003a700 */
        if (C.ax - o->x > 0x1400000) { o->w[0] = 0; kill(o); break; }
        o->rot[1] += 0x17;
        break;
    case 0x3f: orb(o); break;
    case 0x40: case 0x42: case 0x43:              /* TITANS3: FUN_1003a770 / 1003a8e0 / 1003a950: spin, expire */
        o->rot[1] += o->type == 0x40 ? -0x17 : o->type == 0x42 ? -0xf : -8;
        if (C.ax - o->x > 0x1400000) kill(o);
        break;
    case 0x41: cloud_rock(o); break;
    case 0x44:                                                  /* FUN_1003a980 */
        if ((C.frame & 0xd) == 0 && rnd(0x8000) < 0x3000)
            spawn(0x41, rnd(0x6000) * 0x100 + o->x, C.ay + 0x960000, C.az + 0x2000000);
        if (C.ax - o->x > 0xa00000) kill(o);
        break;
    case 0x46: case 0x47: case 0x48: case 0x49: case 0x4a: case 0x4b: if (o->x < C.ax) kill(o); break;
    case 0x51: ice_gen(o); break;
    case 0x52: case 0x53: ice_chunk(o); break;
    case 0x56: lava_blob(o); break;
    case 0x57: storm_rock(o, -1); break;
    case 0x5b: storm_rock(o, 1); break;
    case 0x5c: harpy(o); break;
    case 0x5d: spy(o); break;
    case 0x61: wave(o); break;
    case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67: formation(o); break;
    case 0x68: ice_shard(o); break;
    default: break;
    }
}

void titans_ai_level_start(int checkpoint)     /* FUN_1003c170 (+ DAT_1006fff4 from table 0x10065580) */
{
    static const int W[6] = {0, 4, 8, 12, 12, 12};
    int32_t *speed = C.speed; int *zpush = C.zpush;
    memset(&C, 0, sizeof C);
    C.speed = speed; C.zpush = zpush;
    C.wave = W[checkpoint < 0 ? 0 : checkpoint > 5 ? 5 : checkpoint];
    C.drop = 0;
}

/* TITANS3: the PC build draws sprites into an 8-bit screen whose upper half (0x80..0xff) is the level's sprite
 * palette, so a bank's own palette only matters for the colours it actually fills. The spy bank (64) carries a
 * 33-entry stub palette with nothing in 0x80..0xff, so drawn with its own palette it is a black silhouette.
 * Banks whose palette 0 has no colours in 0x80..0xff are drawn with Pegasus' palette (the level-9 sprite
 * palette; the harpy bank's is identical) copied over their own. */
static struct { const Bank *src; Bank fixed; uint8_t *buf; } PFIX[8];
static Bank *pal_fixed(Bank *b, const Bank *ref)
{
    if (!b || !b->d || !ref || !ref->d) return b;
    for (int i = 0; i < 8; i++) if (PFIX[i].src == b) return PFIX[i].buf ? &PFIX[i].fixed : b;
    int slot = -1;
    for (int i = 0; i < 8; i++) if (!PFIX[i].src) { slot = i; break; }
    if (slot < 0) return b;
    PFIX[slot].src = b;
    #define B16(d, a) ((uint32_t)((d)[a] | (d)[(a) + 1] << 8))
    uint32_t po = 4u * (uint32_t)b->count + 16, rpo = 4u * (uint32_t)ref->count + 16;
    if (po + 2 > b->size || rpo + 2 > ref->size) return b;
    uint32_t x = B16(b->d, po), rx = B16(ref->d, rpo);
    if (4 + x + 4 + 512 > b->size || 4 + rx + 4 + 512 > ref->size) return b;
    if (B16(b->d, 4 + x) != 256 || B16(ref->d, 4 + rx) != 256) return b;
    for (uint32_t k = 0x80; k < 0x100; k++) if (B16(b->d, 4 + x + 4 + 2 * k) & 0x7fff) return b;   /* has its own colours */
    uint8_t *buf = malloc(b->size);
    if (!buf) return b;
    memcpy(buf, b->d, b->size);
    memcpy(buf + 4 + x + 4 + 0x100, ref->d + 4 + rx + 4 + 0x100, 0x100);   /* entries 0x80..0xff */
    #undef B16
    PFIX[slot].buf = buf;
    memset(&PFIX[slot].fixed, 0, sizeof PFIX[slot].fixed);   /* MERGE: Bank gained pcache (palette cache) */
    PFIX[slot].fixed.d = buf; PFIX[slot].fixed.size = b->size; PFIX[slot].fixed.count = b->count;
    PFIX[slot].fixed.cache = calloc((size_t)b->count + 1, sizeof(Image)); PFIX[slot].fixed.done = calloc((size_t)b->count + 1, 1);
    return &PFIX[slot].fixed;
}

/* TITANS3: draw mode 4 (+0x23 = 4) - a "sprite mesh": FUN_10026700 points +0x5c at 16-byte records
 * {s32 x, s32 y, s32 z (16.16 offsets), s16 frame, s16 end (-1 terminates)}; each record is one sprite of the
 * object's bank, placed at the offset turned about the vertical axis by +0x72 (0x400 = full turn). */
static void draw_mesh(Objects *O, Renderer *R, Object *o)
{
    Bank *b = pal_fixed(obj_bank(O, o->bank), C.peg_bank);
    if (!b || !o->w[7]) return;
    int a = o->rot[1] & 0x3ff;
    float c = sinv(a + 0x100) / 32767.0f, sn = sinv(a) / 32767.0f;
    float s = (o->scale ? o->scale : 0x100) / 256.0f;
    for (uint32_t r = (uint32_t)o->w[7], k = 0; k < 64; k++, r += 16) {
        if (pe_s16(O->pe, r + 14) == -1) break;           /* the terminator record is not drawn */
        int fr = pe_s16(O->pe, r + 12);
        const Image *img = bank_frame(b, fr);
        float ox = (int32_t)pe_u32(O->pe, r) / 65536.0f, oy = (int32_t)pe_u32(O->pe, r + 4) / 65536.0f, oz = (int32_t)pe_u32(O->pe, r + 8) / 65536.0f;
        float px = o->x / 65536.0f + (ox * c - oz * sn) * s, pz = o->z / 65536.0f + (ox * sn + oz * c) * s, wx, wz;
        if (img) {
            level_path_to_world(O->L, px, pz, &wx, &wz, NULL);
            render_sprite_xf(R, img, wx, o->y / 65536.0f + oy * s, wz, 0, 0, -1, 255, s, 0);
        }
    }
}

void titans_ai_draw(Objects *O, Renderer *R, Object *o)
{
    gO = O;
    if (o->b23 == 4 && o->w[7]) { draw_mesh(O, R, o); return; }   /* TITANS3 */
    if (o->frame <= 0) return;
    float s = (o->scale ? o->scale : 0x100) / 256.0f;
    int flip = (o->flags54 & 0x10) != 0;
    if (o->bank == 0) { if (o->flags54 & MODEL) solids_draw(O, R, o); return; }
    Bank *b = pal_fixed(obj_bank(O, o->bank), C.peg_bank);       /* TITANS3: level sprite palette */
    const Image *img = b ? bank_frame(b, o->frame) : NULL;
    if (!img) return;
    float ang = (float)(o->rot[2] & 0x3ff) * 4.0f;
    if (o->flags54 & SCREEN) {
        float key = (float)o->w[3];
        if (o->type == 0x34 && C.have_pbody) key = -2;          /* +0x3e = Pegasus' depth - 4 */
        if (o->flags54 & 4) render_sprite2d_ex(R, img, I(o->x), I(o->y), s, flip, key, ang, (int)(o->flags54 & 3));   /* TITANS3 */
        else render_sprite2d(R, img, I(o->x), I(o->y), s, flip, key, ang);
        return;
    }
    float wx, wz;
    level_path_to_world(O->L, o->x / 65536.0f, o->z / 65536.0f, &wx, &wz, NULL);
    render_sprite_xf(R, img, wx, o->y / 65536.0f, wz, flip, 0, (o->flags54 & 4) ? (int)(o->flags54 & 3) : -1, 255, s, ang);   /* TITANS3: semi */
}
