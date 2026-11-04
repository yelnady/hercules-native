/* GAUNTLET: the remaining Training Gauntlet (ENG3 level 2) object types, ported from the Gauntlet handler table
 * (ENG3 0x1003b900 +0x1c: {init, update, update2} per actor type; +0x24 child classes 0x40xx; +0x2c effects 0x20xx).
 * Field offsets are the original 0x134-byte object layout (see e3world.h). ENG3 helpers used by these handlers:
 *   FUN_1001fab0 / FUN_10001010 random (e3_rand)       FUN_1001f690 3-axis speeds (phys)
 *   FUN_10001600 / FUN_10001460 fall + bounce         FUN_10001420 relative position (rel)
 *   FUN_1001f410 child list (e3_child)                FUN_1001d110 / d670 / cb00 class 0x4000 / 0x2000 / 0 spawn
 *   FUN_1000dae0 / FUN_10011b90 chain piece placement FUN_10001190 herd separation
 *   FUN_10015340 floor distance under an object       FUN_1001faf0 Phil's speech (host callback DAT_10040850)
 * Not ported: the 0x2002 / 0x2017 / 0x2018 shadow effects (FUN_1001d600), FUN_10008120 texture animation used by 0x46,
 * the per-kind behaviour callbacks FUN_100013e0 / FUN_100013a0 of 0x2d's runners (lists DAT_10040e40 / DAT_10040fa0). */
#include "e3world.h"
#include "eng3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HI(v) ((int16_t)((uint32_t)(v) >> 16))
#define PL (&W->obj[0])

int e3g_speech = -1;            /* FUN_1001faf0 request, played by e3game.c (sfx_speech) */

static int32_t rnd(int n) { return (int32_t)e3_rand(n); }           /* FUN_1001fab0 / FUN_10001010 */
static void rel(E3Obj *a, E3Obj *b, int16_t r[3])                   /* FUN_10001420 */
{
    r[0] = HI(ODEP(a) - ODEP(b)); r[1] = HI(OY(a) - OY(b)); r[2] = HI(OLAT(a) - OLAT(b));
}
static int same_path(E3World *W, E3Obj *o) { return OW(o, 0x38) == OW(PL, 0x38); }
static void hit(E3World *W, E3Obj *by, int kind)
{
    OI(PL, 0x110) = (int32_t)(by - W->obj) + 1;
    OS(PL, 0x116) = (int16_t)kind;
    if (W->state != 5) e3_hurt(W, kind, by);
}
static E3Spawn *home(E3World *W, E3Obj *o) { return o->spath >= 0 && o->sidx >= 0 ? &W->sp[o->spath][o->sidx] : NULL; }
static int32_t home_y(E3World *W, E3Obj *o) { E3Spawn *s = home(W, o); return s ? s->y : OI(o, 0xa0); }
static int32_t home_d(E3World *W, E3Obj *o) { E3Spawn *s = home(W, o); return s ? s->d : OI(o, 0x9c); }
static int32_t home_l(E3World *W, E3Obj *o) { E3Spawn *s = home(W, o); return s ? s->l : OI(o, 0xa4); }
static E3Obj *ref(E3World *W, E3Obj *o, int off)
{
    int k = OI(o, off);
    return k > 0 && k < E3_MAXOBJ && W->obj[k].used ? &W->obj[k] : NULL;
}
static void set_ref(E3World *W, E3Obj *o, int off, E3Obj *t) { OI(o, off) = t ? (int32_t)(t - W->obj) : 0; }
static E3Obj *kid(E3World *W, E3Obj *o, int k) { return k < o->nchild && W->obj[o->child[k]].used ? &W->obj[o->child[k]] : NULL; }
static void unlist(E3Obj *o, E3Obj *c, E3World *W)         /* FUN_1001f480 */
{
    int ci = (int)(c - W->obj);
    for (int k = 0; k < o->nchild; k++) if (o->child[k] == ci) {
        for (int j = k; j + 1 < o->nchild; j++) o->child[j] = o->child[j + 1];
        o->nchild--;
        break;
    }
}
static void remove_all(E3World *W, E3Obj *o)               /* FUN_1001f4d0 + FUN_1001cb90 */
{
    for (int k = 0; k < o->nchild; k++) { E3Obj *c = &W->obj[o->child[k]]; if (c->used) c->used = 0; }
    o->nchild = 0;
    e3_remove(W, o);
}

/* FUN_1001f690: speeds on all three axes (accel +0xb0/+0xc0/+0xd0, decel +0xb4/+0xc4/+0xd4, max +0xac/+0xbc/+0xcc) */
static void phys(E3Obj *o)
{
    int32_t v = OI(o, 0xa8) + (OI(o, 0xb0) - OI(o, 0xb4)); if (v < 0) v = 0; if (OI(o, 0xac) < v) v = OI(o, 0xac); OI(o, 0xa8) = v;
    v = OI(o, 0xb8) + (OI(o, 0xc0) - OI(o, 0xc4)); if (v < 0) v = 0; if (OI(o, 0xbc) < v) v = OI(o, 0xbc); OI(o, 0xb8) = v;
    v = OI(o, 0xc8) + (OI(o, 0xd0) - OI(o, 0xd4)); if (v < 0) v = 0; if (OI(o, 0xcc) < v) v = OI(o, 0xcc); OI(o, 0xc8) = v;
    ODEP(o) += OB(o, 0x94) ? -OI(o, 0xa8) : OI(o, 0xa8);
    OY(o) += OB(o, 0x95) ? -OI(o, 0xb8) : OI(o, 0xb8);
    if (!OB(o, 0x96)) OLAT(o) += OI(o, 0xc8); else OLAT(o) -= OI(o, 0xc8);
}
static void bounce(E3Obj *o)
{
    int32_t v = OI(o, 0xb8);
    OI(o, 0xc4) = OI(o, 0xc0); OI(o, 0xb8) = v >> 1; OI(o, 0xc0) = 0;
    OI(o, 0xac) >>= 1; OB(o, 0x95) = 0xff;
    OI(o, 0xb0) = OI(o, 0xb4); OI(o, 0xb4) = 0;
    OW(o, 0x9a) = 1; OI(o, 0xcc) >>= 1;
}
static void bounce_top(E3Obj *o)
{
    if (OI(o, 0xb8) == 0) {
        OI(o, 0xc0) = OI(o, 0xc4); OI(o, 0xc4) = 0; OB(o, 0x95) = 0;
        OI(o, 0xb4) = OI(o, 0xb0); OI(o, 0xb0) = 0;
    }
}
/* FUN_10001600: fall and bounce; the Gauntlet's ground is y 0x1c00000. 1 = came to rest. */
static int fall_bounce(E3World *W, E3Obj *o)
{
    phys(o);
    OW(o, 0x9a) = 0;
    bounce_top(o);
    if (OB(o, 0x95)) return 0;
    int half = 0;
    if ((OU(o, 0x54) & 0x10000000) && OW(o, 0x34)) { int16_t b[6]; if (e3_model_box(W, o, b)) half = b[4] * 0x8000; }
    OY(o) += half;
    int32_t floor = W->level == 10 ? 0x400000 : 0x1c00000;
    int landed = W->level == 2 ? floor <= OY(o) : e3_landed(W, o);
    if (landed) {
        int32_t v = OI(o, 0xb8);
        bounce(o);
        if ((v >> 1) == 0) { OY(o) -= half; return 1; }
    }
    OY(o) -= half;
    return 0;
}
/* FUN_10001460: the same with the map floor (Gauntlet / Torment) */
static int fall_bounce2(E3World *W, E3Obj *o)
{
    phys(o);
    OW(o, 0x9a) = 0;
    bounce_top(o);
    if (OB(o, 0x95)) return 0;
    if (e3_landed(W, o)) {
        int32_t v = OI(o, 0xb8);
        bounce(o);
        if ((v >> 1) == 0) return 1;
    }
    return 0;
}
/* FUN_10015340: floor distance under the object (+0x28, +0x54 bit 3; +0x25 bit 1 = no floor) */
static void floor_info(E3World *W, E3Obj *o)
{
    int up, dn = e3_floor(W, HI(ODEP(o)), HI(OY(o)), HI(OLAT(o)), OW(o, 0x38), &up);
    if (dn == 0x7fff) { OB(o, 0x25) |= 2; return; }
    OB(o, 0x25) &= 0xfd;
    if (dn > 0x100) { OU(o, 0x54) &= ~8u; return; }
    OS(o, 0x28) = (int16_t)dn; OU(o, 0x54) |= 8;
}
static void fx(E3World *W, E3Obj *o, int type) { (void)W; if (!(OU(o, 0x50) & 0x20000)) o->fx2_shadow = type; }   /* FX2: FUN_1001d600 shadow child, drawn by e3game.c */

/* class spawns: FUN_1001d110 (0x4000), FUN_1001d670 (0x2000), FUN_1001cb00 (0); the caller runs the init */
static E3Obj *spawn(E3World *W, int type, int32_t d, int32_t y, int32_t l, int path)
{
    E3Obj *c = e3_spawn(W, type, d, y, l, path);
    if (c) e3_type_init(W, c);
    return c;
}
static void sparkle(E3World *W, int32_t d, int32_t y, int32_t l) { spawn(W, 0x2016, d, y, l, OW(PL, 0x38)); }

/* ---------------------------------------------------------------- effects and child classes */

static void fx2016_init(E3World *W, E3Obj *o)      /* FUN_10014b30: sparkle (Gauntlet frames 0x25c..0x268, bank 3) */
{
    OB(o, 0x22) = 3; OW(o, 0x34) = 0x25c;
    OU(o, 0x54) = 0x80000007; OU(o, 0x50) = 2; OW(o, 0x3a) = 0x180; OS(o, 0x30) = 1; OW(o, 0x4a) = 200;
    OW(o, 0x38) = OW(PL, 0x38);
}
static void fx2016_update(E3World *W, E3Obj *o)    /* FUN_10014bb0 */
{
    int16_t t = OS(o, 0x30); OS(o, 0x30) = t - 1;
    if (t > 0) {
        if (OW(o, 0x34) > 0x268) { e3_remove(W, o); return; }
        OW(o, 0x34)++; OS(o, 0x30) = 1;
    }
}
static void deb_common(E3World *W, E3Obj *o)
{
    OI(o, 0xd8) = 0; OW(o, 0x98) = 0; OU(o, 0x50) = 2; OW(o, 0x38) = OW(PL, 0x38);
    int32_t v = rnd(0x200000) + 0x200000; OI(o, 0xbc) = v; OI(o, 0xb8) = v;
    OI(o, 0xc0) = rnd(0x10000) + 0x10000; OI(o, 0xc4) = rnd(0x80) + 0x100;
    OI(o, 0xac) = OI(o, 0xa8) = OI(o, 0xbc) >> 1;
    OI(o, 0xb4) = 0x100 - rnd(0x20);
}
static void d4002_init(E3World *W, E3Obj *o)       /* FUN_10014d90: model debris */
{
    OU(o, 0x54) |= 0x10000040; OB(o, 0x22) = 0;
    deb_common(W, o);
    OI(o, 0xb0) = 0x100 - rnd(0x20);
    OI(o, 0xcc) = OI(o, 0xc8) = OI(o, 0xac) >> 2;
    OI(o, 0xd0) = rnd(0x100); OI(o, 0xd4) = rnd(0x100);
    OB(o, 0x95) = 0;
    OS(o, 0x30) = 1; OW(o, 0x3a) = (uint16_t)(600 - rnd(400));
    OW(o, 0x34) = 4;
}
static void d4002_update(E3World *W, E3Obj *o)     /* FUN_10014f00 */
{
    if (!fall_bounce(W, o)) { OS(o, 0x70) += (int16_t)rnd(0x40); OS(o, 0x72) += (int16_t)rnd(0x40); OS(o, 0x74) += (int16_t)rnd(0x40); }
    else { OS(o, 0x70) = OS(o, 0x72) = OS(o, 0x74) = 0; }
    if (OW(o, 0x9a)) {
        if (OI(o, 0xe0)) { e3_remove(W, o); return; }
        if (OW(o, 0x3a) > 0x60) OW(o, 0x3a) >>= 1;
    }
    if (ODEP(o) < ODEP(PL) - 0x800000) e3_remove(W, o);
}
static void d4001_init(E3World *W, E3Obj *o)       /* FUN_10014c30: sprite debris (the caller sets bank 3 + frame) */
{
    OI(o, 0xd8) = 0; OU(o, 0x50) = 2; OW(o, 0x38) = OW(PL, 0x38);
    int32_t v = rnd(0x200000) + 0x200000; OI(o, 0xbc) = OI(o, 0xb8) = v;
    OI(o, 0xc0) = rnd(0x10000) + 0x10000; OI(o, 0xc4) = rnd(0x80) + 0x100;
    OI(o, 0xac) = OI(o, 0xa8) = v >> 1;
    OI(o, 0xb4) = 0x100 - rnd(0x20); OI(o, 0xb0) = 0x100 - rnd(0x20);
    OI(o, 0xcc) = OI(o, 0xc8) = OI(o, 0xac) >> 2;
    OI(o, 0xd0) = rnd(0x100); OI(o, 0xd4) = rnd(0x100);
    OB(o, 0x95) = 0; OS(o, 0x30) = (int16_t)e3_rand(0x20);
    fx(W, o, 0x2017);
}
static void d4001_update(E3World *W, E3Obj *o)     /* FUN_10014d40 */
{
    OS(o, 0x74) += OS(o, 0x30);
    if (fall_bounce2(W, o)) { OS(o, 0x30) >>= 1; OI(o, 0xd8) = 0; }
    if (ODEP(o) < ODEP(PL) - 0x200000) e3_remove(W, o);
}
static void d4004_init(E3World *W, E3Obj *o)       /* FUN_10015170: model debris (the caller sets the model) */
{
    OU(o, 0x54) = 0x90000040; OB(o, 0x22) = 0;
    deb_common(W, o);
    OI(o, 0xcc) = OI(o, 0xac); OI(o, 0xb0) = 0x100 - rnd(0x20); OI(o, 0xc8) = OI(o, 0xac) >> 1;
    OI(o, 0xd0) = rnd(0x100); OI(o, 0xd4) = rnd(0x100);
    OB(o, 0x95) = 0xff;
    OS(o, 0x30) = 1; OW(o, 0x3a) = (uint16_t)(600 - rnd(400));
    OI(o, 0xdc) = (int32_t)e3_rand(0x20) + 0x10;
    OW(o, 0x34) = 0x25;
}
static void d4004_update(E3World *W, E3Obj *o)     /* FUN_100152c0 */
{
    if (!fall_bounce(W, o)) { int16_t s = OS(o, 0xdc); OS(o, 0x72) += s; OS(o, 0x74) += s; OS(o, 0x70) += s; }
    else { OS(o, 0x72) >>= 1; OS(o, 0x74) >>= 1; OS(o, 0x70) >>= 1; OI(o, 0xd8) = 0; }
    if (OW(o, 0x9a) && OW(o, 0x3a) > 0x60) OW(o, 0x3a) >>= 1;
    if (ODEP(o) < ODEP(PL) - 0x200000) e3_remove(W, o);
}
static void c4029_init(E3World *W, E3Obj *o)       /* FUN_1000f3a0 */
{
    (void)W; OU(o, 0x50) = 2; OB(o, 0x22) = 0; OU(o, 0x54) |= 0x10000040; OW(o, 0x34) = 1; OS(o, 0x72) = 0x200;
}
static void c4029_update(E3World *W, E3Obj *o) { OW(o, 0x34) = 1; if (e3_offscreen(W, o, 0x100)) e3_remove(W, o); }   /* FUN_1000f3d0 */

/* debris bursts used by several types */
static void burst4001(E3World *W, E3Obj *o, const int fr[8], int path)
{
    for (int i = 0; i < 8; i++) {
        int32_t l = rnd(0x20) * 0x10000 - rnd(0x20) * 0x10000 + OLAT(o);
        E3Obj *c = spawn(W, 0x4001, ODEP(o), OY(o) - rnd(0x80) * 0x10000, l, path);
        if (!c) continue;
        OB(c, 0x22) = 3;
        int32_t v = ((int32_t)e3_rand(4) + 0x10) * 0x10000; OI(c, 0xbc) = OI(c, 0xb8) = v; OI(c, 0xc4) = 0x28000;
        v = ((int32_t)e3_rand(8) + 0x1e) * 0x10000; OI(c, 0xac) = v; OI(c, 0xa8) = v >> 2;
        OI(c, 0xb0) = 0; OI(c, 0xb4) = 0x100; OB(c, 0x95) = 0xff;
        OI(c, 0xd0) = (int32_t)e3_rand(0x20); OI(c, 0xcc) = 0x1e0000;
        OW(c, 0x38) = (uint16_t)path;
        OW(c, 0x34) = (uint16_t)fr[i];
        OB(c, 0x94) = 0; OB(c, 0x96) = (i & 3) >= 2 ? 0xff : 0;
    }
}
static const int FR26B[8] = {0x26b, 0x26c, 0x26e, 0x270, 0x26c, 0x26d, 0x26f, 0x271};
static const int FR274[8] = {0x274, 0x275, 0x277, 0x279, 0x275, 0x276, 0x278, 0x27a};

/* ---------------------------------------------------------------- type 8: bonus pickup (FUN_10015e90 / FUN_10015f30) */

static void t08_init(E3World *W, E3Obj *o)
{
    OI(o, 0xdc) = 0; OU(o, 0x50) = 2; OU(o, 0x54) = 0x80000000; OW(o, 0x2a) = 8;
    fx(W, o, 0x2017);
    int32_t v = ((int32_t)e3_rand(4) + 0x10) * 0x10000; OI(o, 0xbc) = OI(o, 0xb8) = v;
    OI(o, 0xb0) = 0; OI(o, 0xcc) = 0; OI(o, 0xc8) = 0; OI(o, 0xd0) = 0; OI(o, 0xd4) = 0;
    OI(o, 0xc4) = 0x10000; OI(o, 0xac) = OI(o, 0xa8) = 0x1e0000; OI(o, 0xb4) = 0x80;
    OB(o, 0x95) = 0xff;
}
static void t08_update(E3World *W, E3Obj *o)
{
    if (OW(o, 0x2a) & 8) {
        uint32_t s = OU(o, 0x64);
        if (fall_bounce2(W, o) && e3_overlap(W, o, PL)) {
            if (s == 0x100375f0) {          /* password vase: the bit follows the path (Gauntlet) */
                OW(o, 0x2a) = 2; e3_sound(W, 0x603c, PL);
                int p = OW(PL, 0x38), bit = 0;
                if (p == 0) bit = 1; else if (p <= 2) bit = 2; else if (p <= 6) bit = 4; else if (p <= 10) bit = 8;
                W->vases |= bit;
            }
            if (s == 0x10037618) { W->lives++; e3_sound(W, 0x603d, PL); OW(o, 0x2a) = 2; }   /* extra life */
        }
        if (s == 0x10037678 && (OW(o, 0x2a) & 8) && e3_overlap(W, o, PL)) {             /* health */
            W->health += 0x20; if (W->health > W->maxhealth) W->health = W->maxhealth;
            e3_sound(W, 0x603b, PL); OW(o, 0x2a) = 2;
        }
        e3_anim(W, o);
        if (!o->used) return;
    }
    if (OW(o, 0x2a) & 4) {
        OU(o, 0x54) = 0x84000005; OW(o, 0x4a) = 0x20;
        if (e3_anim(W, o)) { OW(o, 0x2a) |= 0x8000; e3_remove(W, o); return; }     /* FUN_1001cd40: gone for good */
        if (!o->used) return;
    }
    if (OW(o, 0x2a) & 2) {
        OY(o) -= 0x80000; OW(o, 0x3a) = 0x100; OB(o, 0x22) = 1;
        e3_set_anim(W, o, 7); OW(o, 0x2a) = 4; e3_anim(W, o);
        if (!o->used) return;
    }
    if (e3_offscreen(W, o, 0x100)) e3_remove(W, o);
}

/* ---------------------------------------------------------------- 0x29: crushing stack (FUN_1000d280 / FUN_1000d410) */

static void t29_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) = 2; OW(o, 0x2a) = 8; OU(o, 0x54) |= 0x10000040; OS(o, 0x116) = 0; OW(o, 0x3a) = 0x200;
    OI(o, 0xbc) = rnd(0x100000) + 0xffffff; OI(o, 0xac) = 0; OI(o, 0xcc) = 0; OI(o, 0xb8) = 0;
    OI(o, 0xc0) = rnd(0x20000) + 0x20000;
    int32_t y0 = OY(o);
    OI(o, 0xe0) = 1; OW(o, 0x98) = 1; OI(o, 0xd8) = 1;
    OY(o) = y0 - 0x800000;
    OI(o, 0xc4) = 0xffff;
    E3Obj *c = e3_child(W, o, ODEP(o), y0 - 0x1000000, OLAT(o), 0x4029);
    if (c) OW(c, 0x3a) = 600;
    e3_child(W, o, ODEP(o), OY(o) - 0x1000000, OLAT(o), 0x4029);
    OB(o, 0x95) = 0; OW(o, 0x9a) = 0;
    fx(W, o, 0x2002);
    OW(o, 0x34) = 2; OB(o, 0x22) = 0;
}
static void t29_place(E3World *W, E3Obj *o)
{
    for (int k = 0; k < 2; k++) {
        E3Obj *c = kid(W, o, k); if (!c) continue;
        ODEP(c) = ODEP(o); OY(c) = OY(o) - (k ? 0x800000 : 0x400000); OLAT(c) = OLAT(o);
    }
}
static int t29_touch(E3World *W, E3Obj *o, int16_t r[3])
{
    if (same_path(W, o) && r[2] > -0x80 && r[2] < 0x80 && r[0] > -0x80 && r[0] < 0x80 && r[1] > -0xc0 && r[1] < 0xc0) { hit(W, o, 0x21); return 1; }
    return 0;
}
static void t29_update(E3World *W, E3Obj *o)
{
    OW(o, 0x34) = 2;
    int16_t r[3]; rel(o, PL, r);
    if (r[0] < 0x4c8 && OS(o, 0x116) == 0) { OW(o, 0x2a) = 4; OS(o, 0x116) = 1; }
    if (OW(o, 0x2a) & 4) {
        if (OW(o, 0x98)) { e3_sound(W, 0x609d, o); OW(o, 0x98) = 0; }
        OS(o, 0x72) += 4;
        if (OW(o, 0x9a)) {
            if (OI(o, 0xd8)) { e3_sound(W, 0xe09e, o); OI(o, 0xd8) = 0; }
            W->shake = 2;                               /* FUN_1000d3d0(2) */
            if (OI(o, 0xe0)) {
                for (int i = 0; i < 3; i++) {
                    E3Obj *s = e3_spawn(W, 0x2016, ODEP(o) - 0xc40000, OY(o) + 0x800000, OLAT(o) + (i - 1) * 0x400000, OW(PL, 0x38));
                    if (s) { e3_type_init(W, s); OW(s, 0x3a) = 0x200; }
                }
                OI(o, 0xe0) = 0;
            }
        }
        if (fall_bounce(W, o)) OW(o, 0x2a) = 2;
        t29_place(W, o);
        rel(o, PL, r);
        t29_touch(W, o, r);
        if (ODEP(o) < ODEP(PL) - 0x400000) { remove_all(W, o); return; }
    }
    if (OW(o, 0x2a) & 2) {
        if (OW(o, 0x98) == 0) { e3_sound(W, 0x609c, o); OW(o, 0x98) = 1; }
        OI(o, 0xd8) = 1; OW(o, 0x9a) = 0;
        if (home_y(W, o) < OY(o)) OY(o) -= 0x80000;
        else {
            OW(o, 0x2a) = 4;
            rel(o, PL, r);
            if (r[0] < 0) { OW(o, 0x2a) = 8; return; }
        }
        t29_place(W, o);
        t29_touch(W, o, r);
        if (e3_offscreen(W, o, 0x100)) { remove_all(W, o); return; }
    }
}

/* ---------------------------------------------------------------- 0x2a / 0x35: toppling column of four stones
 * (FUN_1000d7d0 / FUN_1000dc20 falls toward +angle, FUN_1000e020 / FUN_1000e2f0 the mirror; FUN_1000dae0 places) */

static int col_place(E3Obj *o, E3Obj *c, const int rad[4], int32_t yofs)
{
    int a = OS(o, 0x116);
    OS(c, 0x74) = (int16_t)(a >> 2);
    int cs = e3_cos(a & 0xfff), sn = e3_cos((a - 0x400) & 0xfff);
    int r = rad[OS(o, 0x30) & 3];
    OLAT(c) = ((-r * sn) >> 12) * 0x10000 + OLAT(o);
    ODEP(c) = ODEP(o);
    OY(c) = ((r * cs) >> 12) * 0x10000 + OY(o) + yofs;
    OS(o, 0x30) = (int16_t)((uint8_t)(OS(o, 0x30) + 1) & 3);
    if (OI(o, 0xe0) == 0 && a > 0xbff) return 1;
    if (OI(o, 0xe0) == 1 && a < 0x401) return 1;
    return 0;
}
static const int COLRAD[4] = {0, 0x40, 0x80, 0xc0};
static void col_init(E3World *W, E3Obj *o, int dir)
{
    OS(o, 0x116) = 0x800; OS(o, 0x126) = 0x800; OW(o, 0x2a) = 0; OS(o, 0x11e) = 0; OW(o, 0x98) = 0;
    OI(o, 0xdc) = 4; OI(o, 0xe0) = dir; OS(o, 0x30) = 0;
    OI(o, 0xcc) = 1; OI(o, 0xac) = 1; OI(o, 0xbc) = 1; OU(o, 0x50) = 2;
    for (int k = 0; k < 4; k++) {
        E3Obj *c = e3_child(W, o, ODEP(o), OY(o) - k * 0x400000, OLAT(o), 0x402a);
        if (!c) continue;
        OB(c, 0x22) = 0; OW(c, 0x34) = 3; OB(c, 0x94) = 0; OU(c, 0x54) |= 0x10000040; OB(c, 0x96) = 0;
        OI(c, 0xbc) = OI(c, 0xac) = (k & 1) ? 0xf00000 : 0xc00000;
        OI(c, 0xcc) = 0; OI(c, 0xb8) = 0x100; OI(c, 0xc0) = 0x30000; OI(c, 0xc4) = 0; OI(c, 0xb0) = 0; OI(c, 0xb4) = 0;
        OB(c, 0x95) = 0;
        if (!dir) { OI(c, 0xe0) = k == 0; OS(c, 0x30) = (int16_t)(4 * (k + 1)); if (k == 3) OI(c, 0xd8) = 1; }
        if (k) fx(W, c, 0x2002);
    }
}
static void t2a_init(E3World *W, E3Obj *o) { col_init(W, o, 0); }
static void t35_init(E3World *W, E3Obj *o) { col_init(W, o, 1); }
static void col_update(E3World *W, E3Obj *o, int dir)
{
    int n = o->nchild;
    for (int k = 0; k < n; k++) {
        E3Obj *c = kid(W, o, k);
        if (!c) continue;
        int i = k + 1;
        uint16_t st = OW(o, 0x2a);
        int16_t r[3];
        if (st == 0x20 || st == 8) {
            if (!(st & 0x20)) continue;
            int rest = fall_bounce(W, c);
            if (i == 4 && rest && OI(o, 0xbc)) { e3_sound(W, 0x6099, c); OI(o, 0xbc) = 0; }
            rel(c, PL, r);
            if (same_path(W, o) && r[2] > -0x24 && r[2] < 0x24 && r[0] > 0 && r[0] < 0x20 && r[1] > -0x20 && r[1] < 0x18) hit(W, o, 2);
            if (OW(c, 0x9a) && !rest) {
                if (OW(c, 0x34) != 6) OW(c, 0x34) = 6;
                OS(c, 0x72) += 0x10;
                if (OI(c, 0xe0) != 1) {
                    sparkle(W, ODEP(c) - 0x800000, OY(c), OLAT(c));
                    for (int j = 0; j < 8; j++) {
                        int32_t l = rnd(0x200000) + OLAT(c);
                        E3Obj *dd = spawn(W, 0x4002, ODEP(c), rnd(0x200000) + OY(c), l, OW(PL, 0x38));
                        if (!dd) continue;
                        int32_t v = rnd(0x200000) + 0x300000;
                        OI(dd, 0xbc) = v; OI(dd, 0xac) = v; OI(dd, 0xcc) = v >> 1;
                        OB(dd, 0x94) = (j & 1) ? 0 : 0xff;
                        OB(dd, 0x96) = (j & 3) >= 2 ? 0xff : 0;
                        if ((j & 3) == 0) OB(dd, 0x94) = 0xff;
                        if ((j & 3) == 1) OB(dd, 0x94) = 0;
                        if ((j & 3) == 2) OB(dd, 0x94) = 0xff;
                        if ((j & 3) == 3) OB(dd, 0x94) = 0;
                    }
                    OI(c, 0xe0) = 1;
                }
            }
            if (ODEP(c) < ODEP(PL) - 0xc00000 && OS(o, 0x11e) == 3) OW(o, 0x2a) = 8;
            continue;
        }
        rel(c, PL, r);
        if (r[0] < 0x280) OW(o, 0x2a) = 4;
        else {
            int16_t na = (int16_t)(((e3_cos((OW(o, 0x126) - 0x400) & 0xfff) * 0x30) >> 12) + 0x800);
            if (!dir) OS(o, 0x116) = na;
            if (i == 4) {
                if ((OW(o, 0x116) & 0xfff) > 0x800 && OI(o, 0xcc)) { OI(o, 0xcc) = 0; e3_sound(W, 0x6096, c); }
                if ((OW(o, 0x116) & 0xfff) < 0x800 && !OI(o, 0xcc)) { OI(o, 0xcc) = 1; e3_sound(W, 0x6097, c); }
            }
            if (dir) OS(o, 0x116) = na;
            col_place(o, c, COLRAD, 0);
            OS(o, 0x126) += 0x20;
        }
        if (OW(o, 0x2a) & 4) {
            if (i == 4 && OI(o, 0xac)) { e3_sound(W, 0x6098, c); OI(o, 0xac) = 0; }
            int32_t v = OI(o, 0xdc);
            OS(o, 0x116) = (int16_t)(OS(o, 0x116) + (dir ? -HI(v) : HI(v)));
            if (v < 0x140000) OI(o, 0xdc) = v + 0x4000;
            if (col_place(o, c, COLRAD, 0)) OW(o, 0x2a) = 0x20;
        }
    }
    if (e3_offscreen(W, o, 0x100)) remove_all(W, o);
}
static void t2a_update(E3World *W, E3Obj *o) { col_update(W, o, 0); }
static void t35_update(E3World *W, E3Obj *o) { col_update(W, o, 1); }

/* ---------------------------------------------------------------- 0x37: swinging chain with a blade
 * (FUN_10011760 / FUN_10011a80, pieces placed by FUN_10011b90) */

static const int CHAINRAD[4] = {0, -0x78, -0xf4, -0x1b4};
static void t37_init(E3World *W, E3Obj *o)
{
    OW(o, 0x2a) = 0; OS(o, 0x116) = 0x800; OS(o, 0x11e) = 0; OS(o, 0x126) = 0x800; OW(o, 0x98) = 0;
    OI(o, 0xdc) = 4; OI(o, 0xe0) = 0; OS(o, 0x30) = 0;
    OI(o, 0xd8) = rnd(6) + 0xc;
    OI(o, 0xcc) = 1; OI(o, 0xac) = 1; OU(o, 0x50) = 2; OW(o, 0x2a) = 8;
    static const int dy[4] = {0, 0x780000, 0xf40000, 0x1b80000};
    for (int k = 0; k < 4; k++) {
        E3Obj *c = e3_child(W, o, ODEP(o), OY(o) + dy[k], OLAT(o), 0x402a);
        if (!c) continue;
        OU(c, 0x54) |= 0x10000040; OB(c, 0x22) = 0; OW(c, 0x34) = k == 3 ? 0xb : 0xc;
        OB(c, 0x94) = 0; OB(c, 0x96) = 0;
        OI(c, 0xbc) = OI(c, 0xac) = (k & 1) ? 0xf00000 : 0xc00000;
        OI(c, 0xcc) = 0; OI(c, 0xb8) = 0x100; OI(c, 0xc0) = 0x30000; OI(c, 0xc4) = 0; OI(c, 0xb0) = 0; OI(c, 0xb4) = 0;
        OI(c, 0xe0) = k == 0; OS(c, 0x30) = (int16_t)(4 * (k + 1)); OB(c, 0x95) = 0;
        fx(W, c, 0x2002);
    }
}
static void t37_update(E3World *W, E3Obj *o)
{
    E3Obj *last = NULL;
    for (int k = 0; k < o->nchild; k++) {
        E3Obj *c = kid(W, o, k);
        if (!c) continue;
        last = c;
        OS(o, 0x116) = (int16_t)(((e3_cos((OW(o, 0x126) - 0x400) & 0xfff) << 9) >> 12) + 0x800);
        if (OW(c, 0x34) == 0xb) {                       /* FUN_10011b90: whoosh at the bottom of the swing */
            if (OS(o, 0x116) < 0x800 && OI(o, 0xcc)) { OI(o, 0xcc) = 0; OI(o, 0xac) = 1; e3_sound(W, 0x6094, c); }
            if (OS(o, 0x116) > 0x800 && OI(o, 0xac)) { OI(o, 0xcc) = 1; OI(o, 0xac) = 0; e3_sound(W, 0x6095, c); }
        }
        col_place(o, c, CHAINRAD, -0x800000);
        OS(o, 0x126) += OS(o, 0xd8);
    }
    if (last && OW(last, 0x34) == 0xb && same_path(W, o) &&
        OLAT(last) - 0x820000 < OLAT(PL) && OLAT(PL) < OLAT(last) + 0x820000 &&
        ODEP(last) - 0x80000 < ODEP(PL) && ODEP(PL) < ODEP(last) + 0x80000) hit(W, o, 0x2c);
    if (e3_offscreen(W, o, 0x100)) remove_all(W, o);
}

/* ---------------------------------------------------------------- 0x2b: stampede (FUN_10010370 / FUN_10010590) */

static void t2b_init(E3World *W, E3Obj *o)
{
    int n = o->variant;
    OI(o, 0xdc) = n;
    if (OW(o, 0x2a) == 0) OW(o, 0x2a) = 8;
    OS(o, 0x116) = 8; OW(o, 0x34) = 0; OU(o, 0x50) = 2; OU(o, 0x54) |= 0x10000040; OB(o, 0x22) = 0;
    if (n) OS(o, 0x11e) = 0x20;
    OS(o, 0x30) = 0x1e; OW(o, 0x98) = 1;
    for (int i = 0; i < n && i < 8; i++) {
        E3Obj *c = e3_child(W, o, ODEP(o) - i * 0x800000, OY(o), OLAT(o), 0x402b);
        if (!c) continue;
        OB(c, 0x22) = 0; OW(c, 0x34) = 0xd; OU(c, 0x50) = 2; OU(c, 0x54) |= 0x10000040;
        OI(c, 0xd8) = rnd(0x14) + 0x14; OI(c, 0xdc) = 0;
        OS(c, 0x30) = (int16_t)(e3_rand(0x10) + 0x14);
        OW(c, 0x2a) = 2;
        fx(W, c, 0x2002);
        OB(c, 0x94) = 0xff; OB(c, 0x96) = 0;
        OW(c, 0x3a) = n == 1 ? 0x200 : 0x100;
        if (i & 1) { OB(c, 0x96) = 0xff; ODEP(c) -= 0x400000; }
        OI(c, 0xbc) = 0x40000;
        OI(c, 0xac) = (int32_t)e3_rand(0x40000) + 0x60000;
        OI(c, 0xcc) = rnd(0x40000) + 0x40000;
        OI(c, 0xb0) = rnd(0x4000) + 0x2000;
        OI(c, 0xd0) = rnd(0x4000) + 0x2000;
        OI(c, 0xa8) = 0; OI(c, 0xc8) = 0;
    }
}
/* FUN_10001190: a herd member that comes close to a later one turns away from it */
static void separate(E3World *W, E3Obj *o, int i, int n, int dist)
{
    E3Obj *cur = kid(W, o, i - 1);
    if (!cur) return;
    for (int j = i; j < n && j < o->nchild; j++) {
        E3Obj *oth = kid(W, o, j);
        if (!oth) continue;
        int16_t r[3]; rel(cur, oth, r);
        OI(cur, 0xe0) = 0;
        if (-dist < r[2] && r[2] < dist && -dist < r[0] && r[0] < dist) {
            OI(cur, 0xe0) = 0x18;
            if (OB(cur, 0x96) == 0) { OB(oth, 0x96) = 0; OB(cur, 0x96) = 0xff; }
        }
    }
}
static void t2b_update(E3World *W, E3Obj *o)
{
    int n = OI(o, 0xdc);
    int32_t pd = ODEP(PL), py = OY(PL), pl = OLAT(PL);
    for (int k = 0; k < o->nchild; k++) {
        E3Obj *c = kid(W, o, k);
        if (!c) continue;
        int i = k + 1;
        if (OW(o, 0x98)) { e3_sound(W, n == 1 ? 0x2892 : 0x2893, c); OW(o, 0x98) = 0; }
        OS(c, 0x70) += OS(c, 0x30);
        if (OW(c, 0x2a) == 0x20) { int32_t t = OI(c, 0xd8); OI(c, 0xd8) = t - 1; if (t < 0) OW(c, 0x2a) = 2; }
        else {
            if (OW(c, 0x2a) & 2) { OI(c, 0xdc) -= 8; if ((uint32_t)OI(c, 0xdc) <= (uint32_t)(rnd(0x10) - 0x20)) OW(c, 0x2a) = 4; }
            if (OW(c, 0x2a) & 4) { OI(c, 0xdc) += 8; if (OI(c, 0xdc) >= 0) { OI(c, 0xd8) = rnd(0xa0); OW(c, 0x2a) = 0x20; OI(c, 0xdc) = 0; } }
        }
        separate(W, o, i, n, 0x30);
        int lat = HI(OLAT(c));
        lat = OLAT(c) < 1 ? lat - OS(o, 0x11e) : lat + OS(o, 0x11e);
        int a = e3_attr_at(W, HI(ODEP(c)), HI(OY(c)), lat, OW(o, 0x38));
        if (a == 0x5e) OB(c, 0x96) = 0; else if (a == 0x5d) OB(c, 0x96) = 0xff;
        int16_t r[3]; rel(c, PL, r);
        if (same_path(W, o)) {
            if (n == 1) {
                if (r[2] > -0x40 && r[2] < 0x40 && r[0] > -0x60 && r[0] < 0x60 && r[1] > -0x50 && r[1] < 0x50) {
                    hit(W, o, 0x26);
                    if (W->boost) {             /* the power-up run smashes it */
                        sparkle(W, ODEP(o) - 0x400000, OY(o), OLAT(o));
                        if (!(W->vases & 8) && OW(o, 0x38) != 0) {
                            E3Obj *b = e3_spawn(W, 8, pd, py, pl, OW(o, 0x38));
                            if (b) { e3_type_init(W, b); e3_set_anim(W, b, 5); }
                        }
                        e3_sound(W, 0xe0a9, c);
                        for (int j = 0; j < 16; j++) {
                            int32_t l = (rnd(0x100) - rnd(0x100)) * 0x10000 + pl;
                            E3Obj *dd = spawn(W, 0x4004, pd + 0x800000, py - (int32_t)e3_rand(0x80) * 0x10000, l, OW(PL, 0x38));
                            if (!dd) continue;
                            int32_t v = ((int32_t)e3_rand(4) + 0x10) * 0x10000; OI(dd, 0xbc) = OI(dd, 0xb8) = v; OI(dd, 0xc4) = 0x28000;
                            v = ((int32_t)e3_rand(8) + 0x1e) * 0x10000; OI(dd, 0xac) = OI(dd, 0xa8) = v;
                            OI(dd, 0xb0) = 0; OI(dd, 0xb4) = 0x100; OI(dd, 0xd0) = (int32_t)e3_rand(0x20); OI(dd, 0xcc) = 0x1e0000;
                            OW(dd, 0x34) = (j & 3) == 0 ? 0x1a : 0x1b;
                            OB(dd, 0x94) = 0; OB(dd, 0x96) = (j & 3) >= 2 ? 0xff : 0;
                        }
                        OW(o, 0x2a) |= 0x8000;
                        remove_all(W, o);
                        return;
                    }
                }
            } else {
                int dd = HI(ODEP(c) - pd), dy = HI(OY(c) - py), dl = HI(OLAT(c) - pl);
                if ((uint32_t)(dd * dd + dy * dy + dl * dl) < 0x40 * 0x40) hit(W, o, 0x27);   /* FUN_10001140 */
            }
        }
        e3_drop(W, c);
        OY(c) += n == 1 ? -0x280000 : -0x40000;
        e3_move_lat(c);
        e3_move(c);
        if (e3_offscreen(W, c, 0)) { unlist(o, c, W); c->used = 0; k--; }
    }
    if (e3_offscreen(W, o, 0)) remove_all(W, o);
}

/* ---------------------------------------------------------------- 0x4a: rope posts carried by 0x2c / 0x2d / 0x44 */

static void t4a_init(E3World *W, E3Obj *o)            /* FUN_10013d00 */
{
    OU(o, 0x50) = 0x202; OW(o, 0x2a) = 8; OI(o, 0xe0) = 0; OU(o, 0x54) = 0x80000004;
    e3_set_anim(W, o, 0);
}
static void t4a_update(E3World *W, E3Obj *o)          /* FUN_10013d30 */
{
    e3_anim(W, o);
    if (!o->used) return;
    if (OI(o, 0xe0) || e3_offscreen(W, o, 0x100)) e3_remove(W, o);
}
static void posts(E3World *W, E3Obj *o, int offa, int offb, int path)
{
    E3Obj *a = e3_spawn(W, 0x4a, ODEP(o), OY(o), OLAT(o) - 0x100000, path);
    if (a) { t4a_init(W, a); OU(a, 0x54) |= 0x10; }
    set_ref(W, o, offa, a);
    E3Obj *b = e3_spawn(W, 0x4a, ODEP(o), OY(o), OLAT(o) + 0x100000, path);
    if (b) t4a_init(W, b);
    set_ref(W, o, offb, b);
}
static void posts_follow(E3World *W, E3Obj *o, int offa, int offb)
{
    E3Obj *a = ref(W, o, offa), *b = ref(W, o, offb);
    if (!a || !b) return;
    ODEP(a) = ODEP(o); OY(a) = OY(o); OLAT(a) = OLAT(o) - 0x100000;
    ODEP(b) = ODEP(o); OY(b) = OY(o); OLAT(b) = OLAT(o) + 0x100000;
}
static void posts_drop(E3World *W, E3Obj *o, int offa, int offb)
{
    E3Obj *a = ref(W, o, offa), *b = ref(W, o, offb);
    if (a) OI(a, 0xe0) = 1;
    if (b) OI(b, 0xe0) = 1;
    OI(o, offa) = OI(o, offb) = 0;
}

/* ---------------------------------------------------------------- 0x2c: target dummy sliding across on a rope
 * (FUN_1000ee40 / FUN_1000eeb0) */

static void t2c_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) = 2; OW(o, 0x2a) = 2; OS(o, 0x30) = 1; OS(o, 0x11e) = 1; OS(o, 0x116) = 0; OS(o, 0x126) = 8;
    OI(o, 0xdc) = 1; OW(o, 0x98) = 0;
    e3_set_anim(W, o, 4); e3_anim(W, o);
    OY(o) = 0x1c00000;
    e3_drop(W, o);
    floor_info(W, o);
}
static void t2c_update(E3World *W, E3Obj *o)
{
    OW(o, 0x98) = 1;
    int16_t r[3]; rel(o, PL, r);
    if (OS(o, 0x116) && OS(o, 0x11e)) { e3_set_anim(W, o, 3); OS(o, 0x11e) = 0; }
    e3_anim(W, o);
    if (!o->used) return;
    if (OI(o, 0xdc)) {
        e3_sound(W, 0x28a6, o);
        posts(W, o, 0x118, 0x120, OW(o, 0x38));
        OI(o, 0xdc) = 0;
    }
    if (OW(o, 0x2a) & 2) {
        if (OS(o, 0x11e)) OLAT(o) += OS(o, 0x126) * 0x10000;
        if (OLAT(o) > 0xc1ffff) OW(o, 0x2a) = 4;
        uint32_t m = ((uint32_t)(e3_cos((HI(OLAT(o)) - 0x400) & 0xfff) * 5) & 0x7fff800u) * 0x20u;
        ODEP(o) += (int32_t)m;
    }
    if (OW(o, 0x2a) & 4) {
        if (OS(o, 0x11e)) OLAT(o) -= OS(o, 0x126) * 0x10000;
        if (OLAT(o) < -0xc1ffff) OW(o, 0x2a) = 2;
        uint32_t m = ((uint32_t)(e3_cos((HI(-OLAT(o)) - 0x400) & 0xfff) * 5) & 0x7fff800u) * 0x20u;
        ODEP(o) -= (int32_t)m;
    }
    if (OS(o, 0x11e) && same_path(W, o) && r[2] > -0x20 && r[2] < 0x20 && r[0] > -0x20 && r[0] < 0x20) {
        hit(W, o, 0x22);
        e3_sound(W, 0xe0a7, o);
        OS(o, 0x116) = 1; OS(o, 0x126) = 0;
        posts_drop(W, o, 0x118, 0x120);
        E3Obj *s = e3_spawn(W, 0x2016, ODEP(o) - 0x400000, OY(o) + 0x80000, OLAT(o), OW(PL, 0x38));
        if (s) e3_type_init(W, s);
        burst4001(W, o, FR26B, OW(o, 0x38));
        OW(o, 0x2a) |= 0x8000;      /* smashed: don't bring it back while its spawn point is still in the window */
        e3_remove(W, o);
        return;
    }
    posts_follow(W, o, 0x118, 0x120);
    if (e3_offscreen(W, o, 0x100)) { OW(o, 0x2a) = 8; e3_remove(W, o); }
}

/* ---------------------------------------------------------------- 0x44: dummy that charges at Hercules
 * (FUN_10013330 / FUN_100133b0) */

static void t44_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) = 0x202; fx(W, o, 0x2017);
    OW(o, 0x2a) = 8; OS(o, 0x30) = 1; OS(o, 0x11e) = 1; OS(o, 0x116) = 10;
    OI(o, 0xac) = ODEP(o); OW(o, 0x98) = 1; OS(o, 0x126) = 8; OI(o, 0xcc) = 0;
    e3_set_anim(W, o, 4);
    e3_drop(W, o);
}
static void t44_update(E3World *W, E3Obj *o)
{
    if (OW(o, 0x98)) { e3_sound(W, 0x28a6, o); posts(W, o, 0x118, 0x120, OW(o, 0x38)); OW(o, 0x98) = 0; }
    int16_t r[3]; rel(o, PL, r);
    if (OW(o, 0x2a) & 8) {
        OS(o, 0x116)--;
        if (OS(o, 0x116) < 0) { OI(o, 0xd8) = ODEP(PL); OI(o, 0xdc) = OY(PL); OI(o, 0xe0) = OLAT(PL); OS(o, 0x116) = 5; }
        if (OI(o, 0xcc) < OI(o, 0xe0)) OI(o, 0xcc) += 0x30000;
        if (OI(o, 0xe0) < OI(o, 0xcc)) OI(o, 0xcc) -= 0x30000;
        ODEP(o) = OI(o, 0xac); OLAT(o) = OI(o, 0xcc);
        OS(o, 0x11e) = 0x40;
        OI(o, 0xac) -= 0x80000;
        if (same_path(W, o) && r[2] > -0x20 && r[2] < 0x20 && r[0] > -0x20 && r[0] < 0x20) {
            hit(W, o, 0x22); e3_sound(W, 0xe0a7, o); OW(o, 0x2a) = 2;
        }
        e3_anim(W, o);
        if (!o->used) return;
    }
    if (!(OW(o, 0x2a) & 2)) {
        e3_drop(W, o);
        posts_follow(W, o, 0x118, 0x120);
        if (e3_offscreen(W, o, 0x100)) e3_remove(W, o);
        return;
    }
    OW(o, 0x2a) = 4;
    posts_drop(W, o, 0x118, 0x120);
    E3Obj *s = e3_spawn(W, 0x2016, ODEP(o) - 0x400000, OY(o) + 0x80000, OLAT(o), OW(PL, 0x38));
    if (s) e3_type_init(W, s);
    burst4001(W, o, FR26B, OW(o, 0x38));
    OW(o, 0x2a) |= 0x8000;          /* smashed for good */
    e3_remove(W, o);
}

/* ---------------------------------------------------------------- 0x2d: four dummies wandering across the path
 * (FUN_10010b10 / FUN_10010df0) */

static void t2d_init(E3World *W, E3Obj *o)
{
    static const int LATM[4] = {0, 2, 4, 5}, DEPM[4] = {0, 0, 0, 0};
    if (OW(o, 0x2a) == 0) OW(o, 0x2a) = 8;
    OI(o, 0xdc) = 4; OW(o, 0x34) = 0; OU(o, 0x50) = 2; OS(o, 0x116) = 0; OS(o, 0x11e) = 0x180; OS(o, 0x30) = 1;
    OW(o, 0x98) = 0x40;
    for (int i = 0; i < 4; i++) {
        E3Obj *c = e3_child(W, o, DEPM[i] * OS(o, 0x11e) * 0x10000 + ODEP(o), OY(o),
                            LATM[i] * OS(o, 0x98) * 0x10000 + OLAT(o), 0x402c);
        if (!c) continue;
        OS(c, 0x30) = 1; OU(c, 0x50) = 2; OW(c, 0x2a) = 2;
        fx(W, c, 0x2017);
        OB(c, 0x97) = 0; OB(c, 0x94) = 0xff; OB(c, 0x96) = 0xff;
        OI(c, 0xd8) = 0; OI(c, 0xdc) = 1;
        e3_set_anim(W, o, 1);
        OU(c, 0x64) = OU(o, 0x64); OS(c, 0x32) = -1; OS(c, 0x30) = 0;
        OB(c, 0x22) = 3;
        OB(c, 0x96) = 0xff; OW(c, 0x98) = 0;
        OI(c, 0xbc) = 0x40000; OI(c, 0xac) = rnd(0x80000) + 0x50000; OI(c, 0xcc) = 0;
        OI(c, 0xb0) = rnd(0x4000) + 0x2000; OI(c, 0xd0) = rnd(0x2000) + 0x2000;
        OI(c, 0xa8) = 0; OI(c, 0xc8) = 0;
    }
}
static void t2d_update(E3World *W, E3Obj *o)
{
    for (int k = 0; k < o->nchild; k++) {
        E3Obj *c = kid(W, o, k);
        if (!c) continue;
        OI(c, 0xcc) = rnd(0x10000) + 0x40000;
        if (OW(c, 0x2a) != 0) {
            e3_sound(W, 0x28a6, c);
            posts(W, c, 0xdc, 0xd8, OW(o, 0x38));
            OW(c, 0x2a) = 0;
        }
        int16_t r[3]; rel(c, PL, r);
        if (!same_path(W, o)) continue;
        int smashed = 0;
        if (!OB(c, 0x97) && r[2] > -0x10 && r[2] < 0x10 && r[0] > -0x10 && r[0] < 0x20) {
            hit(W, o, 0x22);
            OB(c, 0x97) = 1;
            posts_drop(W, c, 0xdc, 0xd8);
            E3Obj *s = e3_spawn(W, 0x2016, ODEP(c) - 0x400000, OY(c) + 0x80000, OLAT(c), OW(PL, 0x38));
            if (s) e3_type_init(W, s);
            burst4001(W, c, FR274, OW(o, 0x38));
            e3_sound(W, 0xe0a7, c);
            OW(c, 0x34) = 0;
            unlist(o, c, W); k--; smashed = 1;
        }
        int lat = HI(OLAT(c)); if (OLAT(c) >= 0) lat += 0x20;
        int a = e3_attr_at(W, HI(ODEP(c)), HI(OY(c)), lat, OW(c, 0x38));
        if (a == 0x5e) { OB(c, 0x96) = 0; OI(c, 0xcc) = rnd(0x10000) + 0x60000; }
        if (a == 0x5d) { OB(c, 0x96) = 0xff; OI(c, 0xcc) = rnd(0x10000) + 0x60000; }
        if (OB(c, 0x97)) { OI(c, 0xcc) = 0; OI(c, 0xac) = 0; }
        e3_drop(W, c);
        e3_move_lat(c);
        e3_move(c);
        if (!OB(c, 0x97)) e3_anim(W, c);
        if (OB(c, 0x97)) OW(c, 0x34) = 0;
        if (ref(W, c, 0xdc) && ref(W, c, 0xd8)) posts_follow(W, c, 0xdc, 0xd8);
        if (smashed) c->used = 0;      /* unlisted and invisible from here on in the original */
        if (e3_offscreen(W, o, 0x100)) { remove_all(W, o); return; }
    }
}

/* ---------------------------------------------------------------- 0x32: leaping fish (FUN_100113f0 / FUN_10011470) */

static void t32_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) = 0x202; OW(o, 0x2a) = 2; OS(o, 0x30) = 1;
    OS(o, 0x116) = (int16_t)(rnd(2) - rnd(2));
    fx(W, o, 0x2018);
    OS(o, 0x126) = 0;
    E3Spawn *s = home(W, o); if (s) s->y = 0x1e80000;
    OY(o) = 0x1e80000;
    e3_set_anim(W, o, 0);
}
static void t32_update(E3World *W, E3Obj *o)
{
    int32_t hd = home_d(W, o), hy = home_y(W, o), hl = home_l(W, o);
    int s = OS(o, 0x32);
    if (s < 6 || s > 0x11) OY(o) = hy;
    else {
        ODEP(o) -= 0x40000;
        OLAT(o) += OS(o, 0x116) * 0x10000;
        OY(o) = hy + (int32_t)(((uint32_t)(e3_cos((OW(o, 0x126) - 0x400) & 0xfff) * 0x29) & 0x3fffc00u) * (uint32_t)-0x40);
        OW(o, 0x126) += 0x46;
    }
    if (s == 0x1a) {
        ODEP(o) = hd; OLAT(o) = hl; OY(o) = hy;
        OS(o, 0x116) = (int16_t)(rnd(2) - rnd(2)); OS(o, 0x126) = 0;
    }
    if (e3_overlap(W, o, PL)) hit(W, o, 0x2a);
    e3_anim(W, o);
    if (!o->used) return;
    if (e3_offscreen(W, o, 0x100)) e3_remove(W, o);
}

/* ---------------------------------------------------------------- 0x3a: Phil cheering by the path (FUN_10011ef0 / FUN_10011f90) */

static void t3a_init(E3World *W, E3Obj *o)
{
    OB(o, 0x22) = 3; OI(o, 0xdc) = o->variant;
    OU(o, 0x50) = 0x202; OS(o, 0x30) = 1;
    OI(o, 0xac) = OI(o, 0xa8) = OI(o, 0xb0) = 0; OS(o, 0x126) = 0;
    OI(o, 0xcc) = OI(o, 0xc8) = OI(o, 0xd0) = 0; OW(o, 0x98) = 0; OI(o, 0xd8) = 0; OI(o, 0xe0) = 1;
    OW(o, 0x2a) = 2;
    e3_set_anim(W, o, 0);
    e3_drop(W, o);
    floor_info(W, o);
}
static void t3a_update(E3World *W, E3Obj *o)
{
    OB(o, 0x22) = 3;
    int16_t r[3]; rel(o, PL, r);
    if (r[0] < 0x280 && OI(o, 0xe0)) {
        int p = OW(o, 0x38), h = W->health, mh = W->maxhealth;
        if (p > 6) { e3g_speech = 0x1b; OW(o, 0x2a) = 0x100; }
        if (p == 0) { e3g_speech = 0x19; OW(o, 0x2a) = 0x10; }
        if (p != 0 && p < 7) {
            if (h == mh) { e3g_speech = 0x1b; OW(o, 0x2a) = 0x10; }
            if (h < mh && (mh >> 1) < h) { e3g_speech = 0x1a; OW(o, 0x2a) = 8; }
            if (h <= (mh >> 1) && (mh >> 2) < h) { e3g_speech = 0x16; OW(o, 0x2a) = 8; }
            if (h <= (mh >> 2) && 0 < h) { e3g_speech = 0x1c; OW(o, 0x2a) = 4; }
        }
        OI(o, 0xe0) = 0;
    }
    if (OW(o, 0x2a) & 2) e3_anim(W, o);
    if (OW(o, 0x2a) & 4) { e3_set_anim(W, o, 1); OW(o, 0x2a) = 0x20; }
    if (OW(o, 0x2a) & 8) { e3_set_anim(W, o, 2); OW(o, 0x2a) = 0x20; }
    if (OW(o, 0x2a) & 0x10) { e3_set_anim(W, o, 3); OW(o, 0x2a) = 0x20; }
    if (OW(o, 0x2a) & 0x100) { e3_set_anim(W, o, 4); OW(o, 0x2a) = 0x20; }
    if (OW(o, 0x2a) & 0x20) { if (e3_anim(W, o)) OW(o, 0x2a) = 0x40; }
    if (!o->used) return;
    if (OW(o, 0x2a) & 0x40) { e3_set_anim(W, o, 0); OW(o, 0x2a) = 0x80; }
    if (OW(o, 0x2a) & 0x80) e3_anim(W, o);
    if (!o->used) return;
    if (e3_offscreen(W, o, 0x100)) e3_remove(W, o);
}

/* ---------------------------------------------------------------- 0x3c: hoop to jump through, with a prize
 * (FUN_10012200 / FUN_10012250; prize 0x38 FUN_10011d30 / FUN_10011d80, frame 0x41 FUN_1000ee00) */

static void t38_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) = 0x202; OW(o, 0x2a) = 8; OS(o, 0x30) = 1; OS(o, 0x126) = 0; OS(o, 0x116) = 0x1e;
    fx(W, o, 0x2017);
    OY(o) -= 0x200000;
    e3_set_anim(W, o, 0);
}
static void t38_update(E3World *W, E3Obj *o)
{
    if (OW(o, 0x2a) & 2) {
        e3_set_anim(W, o, 1); OS(o, 0x32) = 0; OU(o, 0x54) = 0x80000004;
        e3_sound(W, 0x603e, PL);
        E3Obj *b = e3_spawn(W, 8, ODEP(PL), OY(PL), OLAT(PL), OW(o, 0x38));
        if (b) { t08_init(W, b); OI(b, 0xac) = 0x160000; OI(b, 0xbc) = 0x100000; e3_set_anim(W, b, 1); }
        OW(o, 0x2a) = 4;
    }
    if (!(OW(o, 0x2a) & 4) || OS(o, 0x32) != 0x14) {
        e3_anim(W, o);
        if (!o->used) return;
        if (!e3_offscreen(W, o, 0x100)) return;
    }
    e3_remove(W, o);
}
static void t41_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) = 2; OS(o, 0x116) = 0; OU(o, 0x54) |= 0x80000; OB(o, 0x22) = 3; OW(o, 0x34) = 0x27d;
    fx(W, o, 0x2017); OW(o, 0x3a) = 400; OU(o, 0x64) = 0;
}
static void t3c_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) = 0x202; OW(o, 0x2a) = 8; OS(o, 0x30) = 1; OS(o, 0x126) = 0; OS(o, 0x116) = 0;
    OU(o, 0x54) = 0x80080005; OW(o, 0x4a) = 0xfe; OW(o, 0x98) = 1; OI(o, 0xe0) = 1;
    e3_set_anim(W, o, 2);
}
static void t3c_update(E3World *W, E3Obj *o)
{
    if (OW(o, 0x98)) { e3_sound(W, 0x289a, o); OW(o, 0x98) = 0; }
    int32_t hy = home_y(W, o);
    if (OW(o, 0x2a) & 8) {
        E3Obj *a = e3_spawn(W, 0x38, ODEP(o), OY(o), OLAT(o), OW(o, 0x38));
        if (a) t38_init(W, a);
        set_ref(W, o, 0x118, a);
        E3Obj *b = e3_spawn(W, 0x41, ODEP(o), OY(o) - 0x12e0000, OLAT(o), OW(o, 0x38));
        if (b) t41_init(W, b);
        set_ref(W, o, 0x120, b);
        OW(o, 0x2a) = 2;
    }
    if (OW(o, 0x2a) & 2) OY(o) -= 0x20000;
    if (OW(o, 0x2a) & 4) OY(o) += 0x20000;
    if (OY(o) < hy - 0x200000) OW(o, 0x2a) = 4;
    if (hy + 0x200000 < OY(o)) OW(o, 0x2a) = 2;
    E3Obj *a = ref(W, o, 0x118), *b = ref(W, o, 0x120);
    if (a) { OY(a) = OY(o) - 0x1360000; if (b) OY(b) = OY(o) - 0x14a0000; }
    int16_t v = (int16_t)(((e3_cos((OW(o, 0x126) - 0x400) & 0xfff) * 0x30) >> 12) + 0xc4);
    OW(o, 0x126) += 0x20;
    OS(o, 0x116) = v; OW(o, 0x4a) = (uint16_t)v;
    e3_anim(W, o);
    if (!o->used) return;
    int16_t r[3]; rel(o, PL, r);
    if (same_path(W, o) && r[2] > -0x38 && r[2] < 0x38 && r[0] > -0x10 && r[0] < 0x10 && r[1] > -0x46 && r[1] < 0x5f) {
        if (r[2] < -0x27 || r[2] > 0x27 || r[1] < 8 || r[1] > 0x4b) hit(W, o, 0x40);     /* the rim */
        else if (OI(o, 0xe0)) {                                                           /* through the middle */
            if (a) OW(a, 0x2a) = 2;
            OI(o, 0xe0) = 0;
        }
    }
    if (e3_offscreen(W, o, 0x100)) { if (a) e3_remove(W, a); if (b) e3_remove(W, b); e3_remove(W, o); }
}

/* ---------------------------------------------------------------- 0x43: villagers walking to and fro
 * (FUN_10013010 / FUN_100130e0); 0x47: villagers standing (FUN_10013b30 / FUN_10013c10) */

static void t43_init(E3World *W, E3Obj *o)
{
    int v = (int)e3_rand(3);
    OW(o, 0x98) = (uint16_t)v; OB(o, 0x1c) = (uint8_t)(0xc + v);
    OW(o, 0x4a) += (uint16_t)e3_rand(0x20);
    OU(o, 0x50) = 0x202; OW(o, 0x2a) = 2; OS(o, 0x30) = 1; OS(o, 0x126) = 0x14; OS(o, 0x11e) = 0;
    OS(o, 0x116) = (int16_t)e3_rand(0x78);
    OI(o, 0xd8) = (int32_t)e3_rand(0x80) + 0x40;
    OI(o, 0xe0) = (int32_t)e3_rand(1) + 1;
    e3_drop(W, o); floor_info(W, o);
    e3_set_anim(W, o, 0);
}
static void t43_update(E3World *W, E3Obj *o)
{
    int16_t r[3]; rel(o, PL, r);
    e3_drop(W, o);
    int32_t hl = home_l(W, o);
    if (OW(o, 0x2a) & 2) {
        if (--OS(o, 0x126) < 0) { OS(o, 0x11e) = 2; OW(o, 0x2a) = 0x20; e3_set_anim(W, o, 2); }
        if (hl + OI(o, 0xd8) * 0x10000 < OLAT(o)) OW(o, 0x2a) = 4;
        e3_anim(W, o); if (!o->used) return;
        OLAT(o) += OI(o, 0xe0) * 0x10000;
    }
    if (OW(o, 0x2a) & 0x20) {
        if (e3_anim(W, o)) {
            OS(o, 0x126) = (int16_t)e3_rand(0x78);
            uint16_t u = OW(o, 0x11e);
            if (u & 2) { e3_set_anim(W, o, 0); u = OW(o, 0x11e); OW(o, 0x2a) = u; }
            if (u & 8) { e3_set_anim(W, o, 0); OW(o, 0x2a) = OW(o, 0x11e); }
        }
        if (!o->used) return;
    }
    if (OW(o, 0x2a) & 4) { e3_set_anim(W, o, 1); OW(o, 0x2a) = 0x10; }
    if (OW(o, 0x2a) & 0x10) {
        if (e3_anim(W, o)) {
            if (hl < OLAT(o)) { OW(o, 0x2a) = 8; OU(o, 0x54) |= 0x10; }
            if (OLAT(o) < hl) { OW(o, 0x2a) = 2; OU(o, 0x54) &= ~0x10u; }
            e3_set_anim(W, o, 0);
        }
        if (!o->used) return;
    }
    if (OW(o, 0x2a) & 8) {
        if (--OS(o, 0x126) < 0) { OS(o, 0x11e) = 8; OW(o, 0x2a) = 0x20; e3_set_anim(W, o, 2); }
        if (OLAT(o) < hl - 0x200000) OW(o, 0x2a) = 4;
        e3_anim(W, o); if (!o->used) return;
        OLAT(o) -= OI(o, 0xe0) * 0x10000;
    }
    if (OW(o, 0x2a) & 0x40) {
        e3_anim(W, o); if (!o->used) return;
        if (OU(o, 0x64) == 0x100355b0) ODEP(o) += 0x30000;
    }
    if (r[0] < 0x200 && OW(o, 0x2a) != 0x40) {      /* Hercules runs past: they jump aside / cheer */
        OW(o, 0x2a) = 0x40;
        if (e3_rand(0x100) & 5) e3_sound(W, 0x6083, o);
        e3_set_anim(W, o, 3);
    }
    if (e3_offscreen(W, o, 0x100)) e3_remove(W, o);
}
static void t47_init(E3World *W, E3Obj *o)
{
    OI(o, 0xdc) = o->variant;
    if (OI(o, 0xdc)) OU(o, 0x54) |= 0x10;
    int v = (int)e3_rand(3);
    OS(o, 0x126) = (int16_t)v; OB(o, 0x1c) = (uint8_t)(0xc + v);
    OW(o, 0x4a) += (uint16_t)e3_rand(0x40);
    OW(o, 0x98) = (uint16_t)(e3_rand(0x28) + 200);
    OU(o, 0x50) = 0x202; OW(o, 0x2a) = 8;
    e3_drop(W, o);
    e3_set_anim(W, o, 2);
    floor_info(W, o);
    OS(o, 0x32) += (int16_t)e3_rand(5);
    OS(o, 0x11e) = (int16_t)e3_rand(0x14);
}
static void t47_update(E3World *W, E3Obj *o)
{
    if (OS(o, 0x126) == 0) {
        int16_t t = OS(o, 0x98); OS(o, 0x98) = t - 1;
        if (t < 0) { e3_sound(W, 0x6083, o); OW(o, 0x98) = (uint16_t)(e3_rand(0x28) + 200); }
    }
    int16_t t = OS(o, 0x11e); OS(o, 0x11e) = t - 1;
    if (t < 0) { if (e3_anim(W, o)) OS(o, 0x11e) = (int16_t)e3_rand(0x1e); if (!o->used) return; }
    if (e3_offscreen(W, o, 0x100)) e3_remove(W, o);
}

/* ---------------------------------------------------------------- 0x46: flock of birds taking off (FUN_10013840 /
 * FUN_10013860; birds 0x45 FUN_10013a40 / FUN_10013a90) */

static void t45_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) = 0x202; OW(o, 0x2a) = 2; OW(o, 0x34) = 1;
    e3_drop(W, o);
    OW(o, 0x3a) = 0x200; OY(o) += 0x400000;
    OS(o, 0x11e) = (int16_t)(e3_rand(0x40) + 0x40);
    OU(o, 0x64) = 0;
}
static void t45_update(E3World *W, E3Obj *o)
{
    OLAT(o) -= 0x20000; OY(o) -= 0x10000;
    if (OU(o, 0x64) == 0x100355d0) { e3_anim(W, o); if (!o->used) return; }
    int16_t v = (int16_t)((e3_cos((OW(o, 0x126) - 0x400) & 0xfff) * 0x4c) >> 12);
    OS(o, 0x116) = v; OS(o, 0x74) = v;
    OW(o, 0x126) += OW(o, 0x11e);
    if (OLAT(o) < -0x2000000) {
        E3Obj *p = ref(W, o, 0x118);
        if (p && OW(p, 0x2e) == 0x46) OW(p, 0x2a) = 4;
        e3_remove(W, o);
    }
}
static void t46_init(E3World *W, E3Obj *o) { (void)W; OU(o, 0x50) = 0x202; OW(o, 0x2a) = 8; }
static E3Obj *bird(E3World *W, E3Obj *o, int32_t dl, int32_t dy)
{
    E3Obj *b = e3_spawn(W, 0x45, ODEP(o), OY(o), OLAT(o), OW(o, 0x38));
    if (!b) return NULL;
    t45_init(W, b);
    set_ref(W, b, 0x118, o);
    OLAT(b) += dl; OY(b) += dy;
    return b;
}
static void t46_update(E3World *W, E3Obj *o)
{
    if (OW(o, 0x2a) & 8) {
        E3Obj *b = bird(W, o, 0, 0);
        if (b) { OU(b, 0x64) = 0x100355d0; OS(b, 0x32) = 0; OS(b, 0x30) = 0; }
        bird(W, o, -0x800000, -0x400000);
        bird(W, o, -0x1000000, -0x800000);
        bird(W, o, -0x1800000, -0xc40000);
        bird(W, o, -0x2000000, -0x1000000);
        OW(o, 0x2a) = 2;
    }
    if (OW(o, 0x2a) & 4) { bird(W, o, 0, 0); OW(o, 0x2a) = 2; }
    if (e3_offscreen(W, o, 0x100)) e3_remove(W, o);
}

/* ---------------------------------------------------------------- dispatch */

typedef struct { int type; void (*init)(E3World *, E3Obj *); void (*update)(E3World *, E3Obj *); } GType;
static void nop(E3World *W, E3Obj *o) { (void)W; (void)o; }
static const GType GT[] = {
    {0x29, t29_init, t29_update}, {0x2a, t2a_init, t2a_update}, {0x2b, t2b_init, t2b_update},
    {0x2c, t2c_init, t2c_update}, {0x2d, t2d_init, t2d_update}, {0x32, t32_init, t32_update},
    {0x35, t35_init, t35_update}, {0x37, t37_init, t37_update}, {0x3a, t3a_init, t3a_update},
    {0x3c, t3c_init, t3c_update}, {0x43, t43_init, t43_update}, {0x44, t44_init, t44_update},
    {0x46, t46_init, t46_update}, {0x47, t47_init, t47_update},
    {0x08, t08_init, t08_update}, {0x38, t38_init, t38_update}, {0x41, t41_init, nop},
    {0x45, t45_init, t45_update}, {0x4a, t4a_init, t4a_update},
    {0x4029, c4029_init, c4029_update}, {0x402a, nop, nop}, {0x402b, nop, nop}, {0x402c, nop, nop},
    {0x4001, d4001_init, d4001_update}, {0x4002, d4002_init, d4002_update}, {0x4004, d4004_init, d4004_update},
    {0x2016, fx2016_init, fx2016_update},
};
static const GType *gfind(E3World *W, E3Obj *o)
{
    if (W->level != 2) return NULL;
    int t = OW(o, 0x2e);
    for (size_t i = 0; i < sizeof GT / sizeof GT[0]; i++) if (GT[i].type == t) return &GT[i];
    return NULL;
}
int e3g_type_init(E3World *W, E3Obj *o) { const GType *g = gfind(W, o); if (!g) return 0; g->init(W, o); return 1; }
int e3g_type_update(E3World *W, E3Obj *o) { const GType *g = gfind(W, o); if (!g) return 0; g->update(W, o); return 1; }

/* ---------------------------------------------------------------- test pilot: hazard avoidance (E3_AUTO)
 * Predicts where each dangerous object ahead will be when Hercules reaches its depth and steers to the lateral
 * position with the most clearance (that still has floor), slowing down when nothing is clear. */
static int dangerous(int t)
{
    switch (t) {
    case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f: case 0x30: case 0x32: case 0x35:
    case 0x37: case 0x3e: case 0x40: case 0x44: case 0x48: case 0x49:
    case 0x4029: case 0x402a: case 0x402b: case 0x402c: case 0x402d: case 0x402f: case 0x403e:
        return 1;
    }
    return 0;
}
static int32_t plat[E3_MAXOBJ], pdep[E3_MAXOBJ]; static long pt[E3_MAXOBJ];
/* ram head lateral (16.16) after n more ticks: the 0x2f / 0x30 state machine (shoot out fast, pull back slowly) */
static int32_t ram_lat(E3World *W, E3Obj *o, int n)
{
    int32_t lat = OLAT(o), home = home_l(W, o), c = OI(o, 0xc8);
    int sgn = OW(o, 0x2e) == 0x2f ? 1 : -1, big = OI(o, 0xdc) != 0;
    for (int t = 0; t < n; t++) {
        int32_t rel = (lat - home) * sgn;
        int st = 0;
        if (rel >= 0xac0000) st = 4;
        if (rel <= 0x200000) st = 2;
        int32_t acc, cap;
        if (st == 2) { acc = big ? 0x48000 : 0x24000; cap = big ? 0x480000 : 0x240000; }
        else if (st == 4) { acc = big ? -0x4000 : -0x2000; cap = big ? -0x80000 : -0x40000; }
        else { acc = OI(o, 0xd0); cap = OI(o, 0xcc); }
        c += acc;
        if (cap >= 0) { if (c < 0) c = 0; if (c > cap) c = cap; } else { if (c > 0) c = 0; if (c < cap) c = cap; }
        lat += c * sgn;
    }
    return lat;
}
/* 1 if running at depth speed spd with lateral l would get Hercules hit by one of the rams */
static int ram_hit(E3World *W, E3Obj **rams, int nram, int d, int l, int spd)
{
    for (int k = 0; k < nram; k++) {
        E3Obj *o = rams[k];
        int od = HI(ODEP(o)) - d;
        int t0 = (od - 0x4e - 8) / spd, t1 = (od + 0x35 + 8) / spd;
        if (t0 < 0) t0 = 0;
        for (int t = t0; t <= t1 && t < 90; t++) if (abs(HI(ram_lat(W, o, t)) - l) < 0x54 + 0x14) return 1;
    }
    return 0;
}
void e3g_pilot(E3World *W, int *target, int *jump, int *slow)
{
    E3Obj *P = PL;
    int d = HI(ODEP(P)), l = HI(OLAT(P)), y = HI(OY(P)), path = OW(P, 0x38);
    struct { int lo, hi, ok; } hz[64]; int nh = 0;
    E3Obj *rams[8]; int nram = 0;
    for (int i = 1; i < E3_MAXOBJ; i++) {
        E3Obj *o = &W->obj[i];
        if (!o->used) continue;
        int32_t ol = OLAT(o), od0 = ODEP(o);
        int vl = 0, vd = 0;
        if (pt[i] == W->ticks - 1) { vl = HI(ol - plat[i]); vd = HI(od0 - pdep[i]); }
        plat[i] = ol; pdep[i] = od0; pt[i] = W->ticks;
        if (OW(o, 0x38) == path && OW(o, 0x2e) == 0x3c && OI(o, 0xe0)) {     /* hoop: run through the middle */
            int od = HI(od0) - d;
            if (od > 0 && od < 0x180 && nh < 62) {
                hz[nh].lo = HI(ol) - 0x60; hz[nh].hi = HI(ol) - 0x22; hz[nh].ok = od; nh++;
                hz[nh].lo = HI(ol) + 0x22; hz[nh].hi = HI(ol) + 0x60; hz[nh].ok = od; nh++;
            }
            continue;
        }
        if (OW(o, 0x38) != path || !dangerous(OW(o, 0x2e))) continue;
        int od = HI(od0) - d;
        if (od < -0x30 || od > 0x180) continue;
        int16_t b[6]; int lo, hi, t2 = OW(o, 0x2e);
        if (t2 == 0x2f || t2 == 0x30) {         /* rams: timed separately below */
            if (od > -0x40 && od < 0x160 && nram < 8) rams[nram++] = o;
            continue;
        } else {
            if (e3_model_box(W, o, b) && b[3] > 0) { lo = HI(ol) + b[0]; hi = lo + b[3]; }
            else { lo = HI(ol) - 0x20; hi = HI(ol) + 0x20; }
            int spd = 9 - vd; if (spd < 2) spd = 2;
            int t = od > 0 ? od / spd : 0;
            if (t > 40) t = 40;
            lo += vl * t; hi += vl * t;
            if (vl < 0) lo += vl * 6; else hi += vl * 6;      /* margin for the sweep */
        }
        if (nh < 64) { hz[nh].lo = lo - 0x1c; hz[nh].hi = hi + 0x1c; hz[nh].ok = od; nh++; }
    }
    if (nram) {                     /* time the rams: keep running, speed up or hang back */
        if (!ram_hit(W, rams, nram, d, *target, 10)) {}
        else if (!ram_hit(W, rams, nram, d, *target, 16)) { *slow = -1; return; }
        else { *slow = 1; return; }
    }
    if (!nh) return;
    int bestl = l, bestc = -1;
    for (int cl = -0x100; cl <= 0x100; cl += 8) {
        int u, dn = e3_floor(W, d + 0x40, y, cl, path, &u);
        if (!((dn != 0x7fff && dn < 0x60) || (u != 0x7fff && u < 0x40))) continue;
        int c = 0x7fff;
        for (int k = 0; k < nh; k++) {
            int dist = cl < hz[k].lo ? hz[k].lo - cl : cl > hz[k].hi ? cl - hz[k].hi : -1;
            if (dist < c) c = dist;
        }
        int score = c * 4 - abs(cl - l) / 4;
        if (bestc == -1 || score > bestc) { bestc = score; bestl = cl; }
    }
    *target = bestl;
    if (bestc < 0) *slow = 1;
    for (int k = 0; k < nh; k++) if (hz[k].ok < 0x60 && l >= hz[k].lo && l <= hz[k].hi) *slow = 1;
    (void)jump;
}
