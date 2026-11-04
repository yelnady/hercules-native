/* CYCLOPS: the Cyclops chase level (ENG3 level 8), ported from the level's handler table (ENG3 0x1003b948 +0x1c:
 * {init, update, update2} per type, child handlers DAT_10040b98, effect handlers DAT_10040bb4).
 *
 *   0x29 the Cyclops (FUN_10003610)           0x2a falling masonry (FUN_10004580 / FUN_10004650)
 *   0x2b / 0x2e / 0x45 toppling columns (FUN_10001aa0/1fe0, 10002580/2ac0, 100062f0/67d0)
 *   0x2d sliding cart (FUN_100023a0 / 10002440)  0x2f runaway team, 3 parts (FUN_10002eb0 / 10003020)
 *   0x30 column the Cyclops pulls down (FUN_10003340 / 100033f0)
 *   0x35 people crossing the street (FUN_10003bf0 / 10003dc0)
 *   0x39 collapsing floor, 18 slabs (FUN_10003ef0 / 10004100)
 *   0x3d swooping flyer (FUN_10004c10 / 10004ca0)
 *   0x3e flyer dropping a boulder 0x43 (FUN_10004dc0 / 10005070, 0x43 FUN_10005e50 / 10005ee0)
 *   0x3f fleeing crowd spawner, crowd 0x4033 (FUN_100053a0 / 100053f0, 0x4033 FUN_100056a0 / 10005760)
 *   0x41 the pit (attribute 0x75, FUN_100284a0 / 100059d0)
 *   0x42 fleeing citizens (FUN_10005a20 / 10005b40)   0x47 drifting embers (FUN_10006af0 / 10006b90)
 *   0x48 barricade, smashed by the speed power-up (FUN_10006c20 / 10006ca0)
 *   children 0x4002 / 0x4003 / 0x4004 debris (FUN_10014d90/4f00, 10014fd0/5110, 10015170/52c0)
 *   effect 0x2016 dust (FUN_10014b30 / 10014bb0)
 * Object gravity: FUN_1001f690 (move), FUN_10001460 (bounce on the street, y 0x1c00000) and FUN_10001600
 * (bounce on the floor, FUN_1001eaf0). The ground shadows (0x2002 / 0x2017) are not drawn. */
#include "e3world.h"
#include "eng3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HI(v) ((int16_t)((uint32_t)(v) >> 16))
#define PL (&W->obj[0])

/* hooks exported by e3game.c (CYCLOPS) */
int e3_attr_at(E3World *W, int d, int y, int l, int path);
void e3_die(E3World *W);
int e3_landed(E3World *W, E3Obj *o);
int e3_track(E3World *W, int t, int d, int out[7]);

static int rnd(int n) { return n > 0 ? (int)e3_rand(n) : 0; }
static int16_t tab16(E3World *W, uint32_t va) { return pe_s16(W->dll, va); }

/* ---------------------------------------------------------------- spawn record / groups */

static E3Spawn *srec(E3World *W, E3Obj *o) { return o->spath >= 0 && o->sidx >= 0 ? &W->sp[o->spath][o->sidx] : NULL; }
static uint16_t sflags(E3World *W, E3Obj *o) { E3Spawn *s = srec(W, o); return s ? s->flags : 0; }
static int32_t sy(E3World *W, E3Obj *o) { E3Spawn *s = srec(W, o); return s ? s->y : OY(o); }
static int32_t sl(E3World *W, E3Obj *o) { E3Spawn *s = srec(W, o); return s ? s->l : OLAT(o); }

typedef struct { int owner, n; int idx[24]; } Group;     /* FUN_1001f3c0 / FUN_1001f410 group lists */
static Group grp[E3_MAXOBJ];

static Group *group_new(E3World *W, E3Obj *o) { Group *g = &grp[o - W->obj]; g->owner = (int)(o - W->obj) + 1; g->n = 0; return g; }
static Group *group_of(E3World *W, E3Obj *o) { Group *g = &grp[o - W->obj]; return g->owner == (int)(o - W->obj) + 1 ? g : NULL; }
static E3Obj *gkid(E3World *W, Group *g, int k)
{
    if (k >= g->n || g->idx[k] < 0) return NULL;
    E3Obj *c = &W->obj[g->idx[k]];
    return c->used ? c : NULL;
}
static void group_free(E3World *W, E3Obj *o)      /* FUN_1001f4d0 */
{
    Group *g = group_of(W, o);
    if (!g) return;
    for (int k = 0; k < g->n; k++) { E3Obj *c = gkid(W, g, k); if (c) c->used = 0; }
    g->owner = 0; g->n = 0;
}
static E3Obj *group_add(E3World *W, Group *g, E3Obj *o, int32_t d, int32_t y, int32_t l, int type)
{
    if (g->n >= 24) return NULL;
    E3Obj *c = e3_spawn(W, type, d, y, l, OW(o, 0x38));
    if (!c) return NULL;
    c->parent = (int)(o - W->obj) + 1;
    OU(c, 0x50) = 2;
    g->idx[g->n++] = (int)(c - W->obj);
    return c;
}
static void kill(E3World *W, E3Obj *o) { group_free(W, o); e3_remove(W, o); }

/* ---------------------------------------------------------------- shared helpers */

static void rel(E3Obj *a, E3Obj *b, int16_t r[3]) { r[0] = HI(ODEP(a) - ODEP(b)); r[1] = HI(OY(a) - OY(b)); r[2] = HI(OLAT(a) - OLAT(b)); }
static void hit(E3World *W, E3Obj *by, int kind)
{
    OI(PL, 0x110) = (int32_t)(by - W->obj) + 1;
    OS(PL, 0x116) = (int16_t)kind;
    if (W->state != 5) e3_hurt(W, kind, by);
}
static int sphere(E3Obj *a, E3Obj *b, int r)      /* FUN_10001140 */
{
    int dx = HI(ODEP(b) - ODEP(a)), dy = HI(OY(b) - OY(a)), dz = HI(OLAT(b) - OLAT(a));
    return (unsigned)(dx * dx + dy * dy + dz * dz) < (unsigned)(r * r);
}
static int offs(E3World *W, E3Obj *o, int m) { return e3_offscreen(W, o, m); }
static int sn(int a) { return e3_cos(a - 0x400); }
static void set_prev(E3Obj *o) { OI(o, 0x9c) = ODEP(o); OI(o, 0xa0) = OY(o); OI(o, 0xa4) = OLAT(o); }

static void move3(E3Obj *o)      /* FUN_1001f690 */
{
    int32_t v = OI(o, 0xa8) + (OI(o, 0xb0) - OI(o, 0xb4)); if (v < 0) v = 0; if (v > OI(o, 0xac)) v = OI(o, 0xac); OI(o, 0xa8) = v;
    v = OI(o, 0xb8) + (OI(o, 0xc0) - OI(o, 0xc4)); if (v < 0) v = 0; if (v > OI(o, 0xbc)) v = OI(o, 0xbc); OI(o, 0xb8) = v;
    v = OI(o, 0xc8) + (OI(o, 0xd0) - OI(o, 0xd4)); if (v < 0) v = 0; if (v > OI(o, 0xcc)) v = OI(o, 0xcc); OI(o, 0xc8) = v;
    ODEP(o) += OB(o, 0x94) ? -OI(o, 0xa8) : OI(o, 0xa8);
    OY(o) += OB(o, 0x95) ? -OI(o, 0xb8) : OI(o, 0xb8);
    OLAT(o) += OB(o, 0x96) ? -OI(o, 0xc8) : OI(o, 0xc8);
}

static int bounce(E3Obj *o)      /* the shared bounce of FUN_10001460 / FUN_10001600; 1 = came to rest */
{
    OI(o, 0xb0) = OI(o, 0xb4);
    OI(o, 0xc4) = OI(o, 0xc0);
    int32_t v = OI(o, 0xb8);
    OI(o, 0xb8) = v >> 1;
    OI(o, 0xc0) = 0;
    OB(o, 0x95) = 0xff;
    OI(o, 0xac) >>= 1;
    OI(o, 0xb4) = 0;
    OW(o, 0x9a) = 1;
    OI(o, 0xcc) >>= 1;
    return (v >> 1) == 0;
}
static void apex(E3Obj *o)
{
    if (OI(o, 0xb8) == 0) {
        OI(o, 0xc0) = OI(o, 0xc4); OI(o, 0xc4) = 0; OB(o, 0x95) = 0;
        OI(o, 0xb4) = OI(o, 0xb0); OI(o, 0xb0) = 0;
    }
}

static int fall_street(E3World *W, E3Obj *o)     /* FUN_10001460, level 8: bounces on the street level */
{
    int32_t lim = 0x1c00000;
    uint32_t dm = (uint32_t)ODEP(o) & 0xff800000u;
    if (OW(PL, 0x38) == 0 && (int32_t)dm > 0x37000000 && (int32_t)dm < 0x41000000) lim = 0x1400000;
    move3(o);
    OW(o, 0x9a) = 0;
    apex(o);
    if (!OB(o, 0x95) && lim <= OY(o)) return bounce(o);
    return 0;
}

static int fall_floor(E3World *W, E3Obj *o)      /* FUN_10001600, level 8: bounces on the floor (FUN_1001eaf0) */
{
    move3(o);
    OW(o, 0x9a) = 0;
    apex(o);
    if (OB(o, 0x95)) return 0;
    int adj = 0;
    int16_t b[6];
    if ((OU(o, 0x54) & 0x10000000) && OW(o, 0x34) && e3_model_box(W, o, b)) adj = b[3] * 0x8000;
    OY(o) += adj;
    int r = 0;
    if (e3_landed(W, o)) r = bounce(o);
    OY(o) -= adj;
    return r;
}

static void drop(E3World *W, E3Obj *o) { e3_drop(W, o); }

/* ---------------------------------------------------------------- debris and dust */

static E3Obj *dust(E3World *W, int32_t d, int32_t y, int32_t l)     /* 0x2016 (FUN_1001d670 + FUN_10014b30) */
{
    E3Obj *o = e3_spawn(W, 0x2016, d, y, l, OW(PL, 0x38));
    if (!o) return NULL;
    OW(o, 0x34) = 0xd1;
    OU(o, 0x54) = 0x80000007u; OU(o, 0x50) = 2;
    OW(o, 0x3a) = 0x180; OS(o, 0x30) = 1; OW(o, 0x4a) = 200;
    OU(o, 0x64) = 0;            /* frames are stepped by the update, not a script */
    return o;
}
static void dust_update(E3World *W, E3Obj *o)    /* FUN_10014bb0 */
{
    int16_t t = OS(o, 0x30);
    OS(o, 0x30) = t - 1;
    if (t > 0) {
        if (OW(o, 0x34) > 0xdd) { e3_remove(W, o); return; }
        OW(o, 0x34)++;
        OS(o, 0x30) = 1;
    }
}

static void debris_common(E3Obj *o, int rising)
{
    OI(o, 0xd8) = 0; OW(o, 0x98) = 0; OU(o, 0x50) = 2;
    OI(o, 0xbc) = OI(o, 0xb8) = rnd(0x200000) + 0x200000;
    OI(o, 0xc0) = rnd(0x10000) + 0x10000;
    OI(o, 0xc4) = rnd(0x80) + 0x100;
    OI(o, 0xac) = OI(o, 0xa8) = OI(o, 0xbc) >> 1;
    OI(o, 0xb4) = 0x100 - rnd(0x20);
    OI(o, 0xb0) = 0x100 - rnd(0x20);
    OI(o, 0xd0) = rnd(0x100); OI(o, 0xd4) = rnd(0x100);
    OB(o, 0x95) = rising ? 0xff : 0;
    OS(o, 0x30) = 1;
    OW(o, 0x3a) = (uint16_t)(600 - rnd(400));
}
static E3Obj *debris(E3World *W, int type, int32_t d, int32_t y, int32_t l)
{
    E3Obj *o = e3_spawn(W, type, d, y, l, OW(PL, 0x38));
    if (!o) return NULL;
    OW(o, 0x38) = OW(PL, 0x38);
    if (type == 0x4004) {       /* FUN_10015170: a tumbling model piece */
        debris_common(o, 1);
        OU(o, 0x54) = 0x90000040u;
        OI(o, 0xcc) = OI(o, 0xac); OI(o, 0xc8) = OI(o, 0xac) >> 1;
        OI(o, 0xdc) = (int)e3_rand(0x20) + 0x10;
        OB(o, 0x22) = 0; OW(o, 0x34) = 0x25;
    } else {                    /* FUN_10014d90 / FUN_10014fd0: a sprite shard, level 8 script 0x10037658 */
        debris_common(o, 0);
        OI(o, 0xcc) = OI(o, 0xc8) = OI(o, 0xac) >> 2;
        OB(o, 0x22) = 6; OU(o, 0x54) = 0x80000000u; OU(o, 0x64) = 0x10037658; OS(o, 0x32) = -1;
    }
    return o;
}
static void dir4(E3Obj *o, int k, const uint8_t f94[4], const uint8_t f96[4]) { OB(o, 0x94) = f94[k & 3]; OB(o, 0x96) = f96[k & 3]; }
static const uint8_t D94A[4] = {0xff, 0, 0xff, 0}, D96A[4] = {0, 0, 0xff, 0xff};

static void debris_update(E3World *W, E3Obj *o)
{
    int t = OW(o, 0x2e) & 0xfff;
    set_prev(o);
    if (t == 4) {               /* FUN_100152c0 */
        if (!fall_floor(W, o)) {
            int16_t s = (int16_t)OI(o, 0xdc);
            OS(o, 0x72) += s; OS(o, 0x74) += s; OS(o, 0x70) += s;
        } else {
            OS(o, 0x72) >>= 1; OS(o, 0x74) >>= 1; OS(o, 0x70) >>= 1; OI(o, 0xd8) = 0;
        }
    } else {                    /* FUN_10014f00 (level 8) / FUN_10015110 */
        if (!fall_street(W, o)) e3_anim(W, o); else OI(o, 0xd8) = 0;
        if (!o->used) return;
        if (t == 2 && OW(o, 0x9a) && OI(o, 0xe0)) { e3_remove(W, o); return; }
    }
    if (OW(o, 0x9a) && OW(o, 0x3a) > 0x60) OW(o, 0x3a) >>= 1;
    int32_t lim = t == 2 ? 0x800000 : 0x200000;
    if (ODEP(o) < ODEP(PL) - lim) e3_remove(W, o);
}

/* ---------------------------------------------------------------- 0x29 the Cyclops (FUN_10003610) */

static int32_t prev_cd = 0x7fffffff;

static void cyc_bank(E3Obj *c) { uint32_t s = OU(c, 0x64); OB(c, 0x22) = (s == 0x10033100 || s == 0x100331b0) ? 7 : 5; }

static void cyclops_update(E3World *W, E3Obj *c)
{
    int dcam = prev_cd == 0x7fffffff ? 0 : W->cd - prev_cd;
    prev_cd = W->cd;
    if (dcam < -0x180) dcam = 0;
    if (ODEP(c) > 0x7f7fffff) { OW(c, 0x38) = OW(PL, 0x38) + 1; ODEP(c) = 0; }     /* past the end of its path */
    int64_t nd = (int64_t)ODEP(c) + (int64_t)dcam * 0x10000;
    ODEP(c) = nd > 0x7fffffff ? 0x7fffffff : (int32_t)nd;
    if (OW(c, 0x2a) == 0x41) {           /* stomp */
        OI(c, 0xac) = 1;
        cyc_bank(c); e3_anim(W, c);
        if (OI(c, 0xd8) == 0) {
            if (OW(c, 0x34) == 0x36) OS(c, 0x11e) = 1;
            if (OW(c, 0x34) == 0x3a) { OS(c, 0x11e) = 2; OI(c, 0xd8) = 1; e3_set_anim(W, c, 1); OW(c, 0x2a) = 0x45; }
        }
        if (OI(c, 0xd8) == 1) {
            if (OW(c, 0x34) == 0x5b) OS(c, 0x11e) = 1;
            if (OW(c, 0x34) == 0x5f) { OS(c, 0x11e) = 2; OI(c, 0xd8) = 0; e3_set_anim(W, c, 1); OW(c, 0x2a) = 0x45; }
        }
    }
    if (OS(c, 0x11e) == 1 || OS(c, 0x11e) == 2) {   /* the street shakes under the stomp */
        if (OI(c, 0xcc)) {
            for (int k = 0; k < 3; k++) {
                W->camshake = OI(c, 0xe0);
                OI(c, 0xe0) += 0x20000;
                if (OI(c, 0xe0) > 0x80000) OI(c, 0xe0) = -0x80000;
            }
            OI(c, 0xcc)--;
        }
    } else { OI(c, 0xcc) = 0x50; W->camshake = 0; }
    int st = OW(c, 0x2a);
    if (st == 0x42 || st == 0x45) {      /* run */
        OI(c, 0xac) = 1;
        if (st == 0x42) OS(c, 0x11e) = 0;
        cyc_bank(c); e3_anim(W, c);
        static const int BACK[] = {0xf, 0x21, 0x94, 0x72, 0x2b, 0x4d, 0xa7, 0x36};
        for (size_t k = 0; k < sizeof BACK / sizeof BACK[0]; k++) if (OW(c, 0x34) == BACK[k]) e3_set_anim(W, c, 4);
        if (OW(c, 0x34) == 0xc) {
            static int nxt;
            int n = nxt; nxt = nxt + 1 > 2 ? 0 : nxt + 1;
            OW(c, 0x98)++;
            e3_set_anim(W, c, n == 0 ? 3 : n == 1 ? 6 : 7);
        }
    }
    if (OW(c, 0x2a) == 0x44) {           /* throws: object 0x38 (no handler of its own, DAT_1003fb14) */
        cyc_bank(c); e3_anim(W, c);
        if (OW(c, 0x34) == 0x20 && !OI(c, 0x118)) {
            E3Obj *t = e3_spawn(W, 0x38, ODEP(c) + 0x1000000, OY(c), OLAT(c) - 0x400000, OW(c, 0x38));
            if (t) { OU(t, 0x50) = 2; OI(c, 0x118) = (int)(t - W->obj) + 1; }
        }
        if (OW(c, 0x34) == 0x2b) OW(c, 0x2a) = 0x42;
    }
    if (OW(c, 0x2a) == 0x43) {           /* pulls a column down onto the street */
        cyc_bank(c); e3_anim(W, c);
        if (OW(c, 0x34) == 0xa1 && OI(c, 0xac)) {
            E3Obj *t = e3_spawn(W, 0x30, ODEP(c), OY(c) - 0x1800000, OLAT(c) + 0x800000, OW(c, 0x38));
            if (t) { e3_type_init(W, t); OW(t, 0x38) = OW(c, 0x38); }
            OI(c, 0xac) = 0;
        }
        if (OW(c, 0x34) == 0xa7) { e3_set_anim(W, c, 1); OW(c, 0x2a) = 0x42; }
    }
    if (W->state == 6 && OW(c, 0x2a) != 0x47) { OI(c, 0xac) = 1; OW(c, 0x2a) = 0x47; OB(c, 0x22) = 7; e3_set_anim(W, c, 1); }
    if (OW(c, 0x2a) == 0x47) { OI(c, 0xac) = 1; e3_anim(W, c); }
}

/* ---------------------------------------------------------------- 0x2a falling masonry */

static void t2a_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) = 0x400202; OU(o, 0x54) |= 0x10000040; OB(o, 0x22) = 0;
    OI(o, 0xdc) = rnd(5);
    OW(o, 0x2a) = 8; OW(o, 0x98) = 1; OI(o, 0xe0) = 0;
    OY(o) = sy(W, o) - 0x1000000;
    OS(o, 0x116) = 0; OS(o, 0x11e) = 0x10; OB(o, 0x95) = 0; OB(o, 0x97) = 2;
    OI(o, 0xbc) = 0xf00000; OI(o, 0xac) = 0; OI(o, 0xcc) = 0xf0000; OI(o, 0xb8) = 0;
    OI(o, 0xc0) = 0x20000; OI(o, 0xc4) = 0xffff;
    OS(o, 0x30) = 10; OW(o, 0x9a) = 0;
}
static void t2a_update(E3World *W, E3Obj *o)
{
    OW(o, 0x34) = (OW(o, 0x2a) & 8) ? 0 : (uint16_t)tab16(W, 0x10034fe0u + 4u * (uint32_t)OI(o, 0xdc));
    int16_t r[3]; rel(o, PL, r);
    if (r[0] < 0x400 && OS(o, 0x116) == 0) { OW(o, 0x2a) = 4; OS(o, 0x116) = 1; }
    if (r[2] > -0x36 && r[2] < 0x36 && r[0] > -10 && r[0] < 0x60 && r[1] != 0x18 && r[1] < 0x18) hit(W, o, 0x200);
    if (OW(o, 0x2a) & 4) {
        set_prev(o);
        OS(o, 0x72) += OS(o, 0x11e);
        OS(o, 0x70) += (int16_t)rnd(OS(o, 0x11e) * 2);
        OS(o, 0x74) += (int16_t)rnd(OS(o, 0x11e) * 2);
        if (OW(o, 0x9a)) {
            W->camshake = 4 << 16;
            OS(o, 0x11e) /= 2;
            if (OS(o, 0x30) != 1) { dust(W, ODEP(o) - 0x800000, OY(o), OLAT(o)); OS(o, 0x30) = 1; }
            if (OS(o, 0x98) == 1) {
                e3_sound(W, 0xe206, o); e3_sound(W, 0xe207, o);
                for (int k = 0; k < 8; k++) {
                    E3Obj *c = debris(W, 0x4003, ODEP(o), rnd(0x200000) + OY(o), rnd(0x200000) + OLAT(o));
                    if (c) dir4(c, k, D94A, D96A);
                }
                OS(o, 0x98) = 0;
            }
        }
        if (fall_floor(W, o)) { OW(o, 0x2a) = 2; W->camshake = 0; }
    }
    if (offs(W, o, 0x100)) kill(W, o);
}

/* ---------------------------------------------------------------- 0x2b / 0x2e / 0x45 toppling columns */

/* the four stacked pieces 0x4029: {y offset, model, timer, e0, d8, 0x96, vymax/ac, cc, b8} (FUN_10001aa0 family) */
static void column_init(E3World *W, E3Obj *o, int kind)
{
    OS(o, 0x116) = 0; OS(o, 0x11e) = 0; OS(o, 0x126) = 0;
    OU(o, 0x50) = 2; OI(o, 0xd8) = kind == 0x45 ? 0 : -4; OI(o, 0xe0) = 0;
    OS(o, 0x30) = 0;
    if (kind == 0x45) { OS(o, 0x11e) = (int16_t)(rnd(0xc) + 8); }
    OI(o, 0xdc) = (int)(sflags(W, o) & 0xff);
    OW(o, 0x2a) = kind == 0x45 ? 4 : 2;
    int32_t d = (ODEP(o) >> 23) * 0x800000 + 0x400000, l = (OLAT(o) >> 23) * 0x800000 + 0x400000;
    OI(o, 0xac) = d; OI(o, 0xcc) = l; ODEP(o) = d; OLAT(o) = l;
    Group *g = group_new(W, o);
    static const int32_t YO[4] = {0, -0x800000, -0x1000000, -0x11b0000};
    for (int k = 0; k < 4; k++) {
        E3Obj *c = group_add(W, g, o, ODEP(o), OY(o) + YO[k], OLAT(o), 0x4029);
        if (!c) continue;
        OB(c, 0x97) = 1; OU(c, 0x50) = 2; OU(c, 0x54) |= 0x10000040; OB(c, 0x22) = 0;
        OB(c, 0x94) = 0; OB(c, 0x96) = 0; OB(c, 0x95) = 0;
        OI(c, 0xbc) = OI(c, 0xac) = 0xc00000; OI(c, 0xcc) = 0; OI(c, 0xb8) = 0x100; OI(c, 0xc0) = 0x30000;
        OI(c, 0xc4) = 0; OI(c, 0xb0) = 0; OI(c, 0xb4) = 0; OI(c, 0xd8) = 1;
        if (kind != 0x45) {
            static const int M2B[4] = {1, 0xb, 2, 3}, M2E[4] = {1, 0xb, 2, 0x28};
            OW(c, 0x34) = (uint16_t)(kind == 0x2b ? M2B[k] : M2E[k]);
            if (k == 3 && OI(o, 0xdc)) OW(c, 0x34) = kind == 0x2b ? 0x2a : 0x33;
            OS(c, 0x30) = k < 2 ? 1 : 0;
            OI(c, 0xe0) = k == 0 ? 1 : 0;
            if (k == 0) OI(c, 0xd8) = 0;
            if (k) OI(c, 0xb8) = 0x900;
            if (k == 3) { OB(c, 0x96) = 0xff; OI(c, 0xbc) = OI(c, 0xac) = 0x1200000; OI(c, 0xcc) = 0x200000; OI(c, 0xc8) = 0; OI(c, 0xb8) = 0x2300; OI(c, 0xd0) = 0; }
        } else {
            static const int M45[4] = {1, 0xb, 2, 3};
            OW(c, 0x34) = (uint16_t)M45[k];
            OS(c, 0x30) = k < 2 ? 1 : 0;
            OI(c, 0xe0) = 1; OI(c, 0xdc) = 1; OI(c, 0xd8) = k == 0 ? 0 : 1;
            if (k == 3) {
                OU(c, 0x50) = 0x400002; OB(c, 0x96) = 0xff;
                if (OLAT(o) > 0x15e0000) { OW(c, 0x34) = 4; OY(c) -= 0x410000; }
                if (OLAT(o) < 0) OW(c, 0x34) = 0x28;
                if (OLAT(o) < -0x1000000) { OW(c, 0x34) = 0x29; OY(c) -= 0x410000; }
            }
        }
    }
}

/* FUN_10001e10 (0x2b, angle falling) / FUN_100028f0 (0x2e, angle rising): place piece k of the column at angle */
static int column_pose(E3Obj *o, E3Obj *c, int kind)
{
    static const int YK[4] = {0, -0x80, -0x100, -0x10b};
    int u = OS(o, 0x116), v = u, thr;
    int var = OI(o, 0xdc);
    OS(c, 0x74) = (int16_t)(u >> 2);
    if (var == 1 || var == 2) v = 0;
    if (kind == 0x2b) { thr = 0xc00; if (var == 0) { v = -v; thr = 0xdac; } if (var == 3) thr = 0xdac; }
    else { thr = 0x400; if (var == 3) { v = -v; thr = 0x258; } if (var == 0) thr = 0x258; }
    OS(c, 0x70) = (int16_t)(v >> 2);
    int k = OI(o, 0xe0) & 3, y0 = YK[k];
    int cu = e3_cos(u), su = sn(u), cv = e3_cos(v), sv = sn(v);
    int t = (y0 * cu) >> 12;
    OLAT(c) = ((-y0 * su) >> 12) * 0x10000 + OI(o, 0xcc);
    OY(c) = ((t * cv) >> 12) * 0x10000 + OY(o);
    ODEP(c) = ((t * sv) >> 12) * 0x10000 + OI(o, 0xac);
    if (++OI(o, 0xe0) > 3) OI(o, 0xe0) = 0;
    return kind == 0x2b ? OS(o, 0x116) <= thr : thr <= OS(o, 0x116);
}
static int column_pose45(E3Obj *o, E3Obj *c)      /* FUN_100066c0 */
{
    static const int A[4] = {0, -0x80, -0x100, -0x10b};
    int u = OS(o, 0x116);
    OS(c, 0x70) = (int16_t)-(u >> 2);
    int a = A[OS(o, 0x30) & 3];
    int cu = e3_cos(u), su = sn(u);
    OLAT(c) = OLAT(o);
    OY(c) = ((a * cu) >> 12) * 0x10000 + OY(o);
    ODEP(c) = ((-a * su) >> 12) * 0x10000 + ODEP(o);
    OS(o, 0x30) = (int16_t)((OS(o, 0x30) + 1) & 3);
    return OS(o, 0x116) < -900;
}

static void column_land(E3World *W, E3Obj *o, E3Obj *c, int k, int kind)
{
    if (OS(c, 0x30) != 1) {
        if (kind == 0x2b && (OW(c, 0x34) == 3 || OW(c, 0x34) == 0x2a)) e3_sound(W, 0xe205, c);
        if (k == 4) dust(W, ODEP(c) - 0x800000, OY(c), OLAT(c) + (kind == 0x2b ? -0x600000 : 0x600000));
        OS(c, 0x30) = 1;
    }
    if (OI(c, 0xe0) != 1) {
        if (kind == 0x2e && (OW(c, 0x34) == 0x28 || OW(c, 0x34) == 0x33)) e3_sound(W, 0xe205, c);
        for (int i = 0; i < 6; i++) {
            int32_t l = (kind == 0x2b ? rnd(0x20) : rnd(0x80)) * 0x10000 + OLAT(c);
            int32_t y = kind == 0x2b ? rnd(0x20) * 0x10000 + OY(c) : OY(c) - rnd(0x20) * 0x10000;
            E3Obj *b = debris(W, 0x4003, ODEP(c), y, l);
            if (!b) continue;
            OI(b, 0xbc) = OI(b, 0xac) = rnd(0x300000) + 0x300000;
            OI(b, 0xcc) = OI(b, 0xac) >> 1;
            if (i < 1) OI(b, 0xd8) = 1;
            OW(b, 0x4a) = (uint16_t)(kind == 0x2b ? rnd(0x20) + 0x80 : (0x80 - rnd(0x40)) + rnd(0x40));
            dir4(b, i, D94A, D96A);
        }
        OI(c, 0xe0) = 1;
    }
}

static void column_update(E3World *W, E3Obj *o, int kind)
{
    Group *g = group_of(W, o);
    E3Obj *cyc = W->cyclops >= 0 && W->obj[W->cyclops].used ? &W->obj[W->cyclops] : NULL;
    if (g && kind != 0x45 && (OS(o, 0x30) != 0 || (cyc && OS(cyc, 0x11e) == 1))) {
        OS(o, 0x30) = 1;
        for (int k = 0; k < g->n; k++) {
            E3Obj *c = gkid(W, g, k);
            if (!c) continue;
            int16_t r[3]; rel(c, PL, r);
            if (sphere(c, PL, 0x80)) hit(W, o, 2);
            int st = OW(o, 0x2a);
            if (st == 0x20 || st == 8 || st == 0x10) {
                if (st & 0x20) {
                    set_prev(c);
                    int rr = fall_floor(W, c);
                    if (kind == 0x2e && !rr) OS(c, 0x74) -= 1;
                    if (OW(c, 0x9a) && !rr) column_land(W, o, c, k + 1, kind);
                    if (kind == 0x2e && ODEP(c) < ODEP(PL) - 0x2580000) { e3_remove(W, c); g->idx[k] = -1; }
                }
            } else {
                if (r[0] < (OI(o, 0xdc) + 7) * 0x80) OW(o, 0x2a) = 4;
                if (OW(o, 0x2a) & 4) {
                    int top = kind == 0x2b ? (OW(c, 0x34) == 3 || OW(c, 0x34) == 0x2a) : OW(c, 0x34) == 0x33;
                    if (OS(o, 0x126) == 0 && top) { e3_sound(W, 0x6204, c); OS(o, 0x126) = 1; }
                    int u = kind == 0x2b ? ((OS(o, 0x116) - 6) & 0xfff) : ((OS(o, 0x116) + 6) & 0xfff);
                    OS(o, 0x116) = (int16_t)u;
                    if ((kind == 0x2b ? u < 0xe11 : u > 399) && OI(c, 0xd8)) OI(c, 0xd8) = 0;
                    if (column_pose(o, c, kind)) OW(o, 0x2a) = 0x20;
                }
            }
        }
    }
    if (g && kind == 0x45) {
        if (ODEP(PL) > 0x1000000) OI(o, 0xd8) = 1;
        if (OI(o, 0xd8)) for (int k = 0; k < g->n; k++) {
            E3Obj *c = gkid(W, g, k);
            if (!c) continue;
            if (OW(o, 0x2a) & 4) {
                if (OS(o, 0x126) == 0 && OW(c, 0x34) == 3) { e3_sound(W, 0x6204, c); OS(o, 0x126) = 1; }
                if (OI(c, 0xe0)) {
                    dust(W, ODEP(c) - 0x800000, rnd(0x40) * 0x10000 + OY(c), rnd(0x40) * 0x10000 + OLAT(c));
                    for (int i = 0; i < 3; i++) {
                        E3Obj *b = debris(W, 0x4003, ODEP(c) - 0x800000, rnd(0x20) * 0x10000 + OY(c), rnd(0x20) * 0x10000 + OLAT(c));
                        if (!b) continue;
                        OI(b, 0xbc) = OI(b, 0xac) = rnd(0x100000) + 0x100000;
                        OI(b, 0xcc) = OI(b, 0xac) >> 1; OI(b, 0xc0) >>= 1;
                        OW(b, 0x4a) = (uint16_t)(rnd(0x20) + 0x80); OW(b, 0x3a) = (uint16_t)(0x80 - rnd(0x40));
                        OB(b, 0x94) = 0xff; OB(b, 0x96) = (i & 2) ? 0xff : 0;
                    }
                    OI(c, 0xe0) = 0;
                }
                OS(o, 0x116) -= OS(o, 0x11e);
                if (column_pose45(o, c)) OW(o, 0x2a) = 0x20;
            }
            if (OW(o, 0x2a) & 0x20) {
                set_prev(c);
                int rr = fall_floor(W, c);
                if (OW(c, 0x9a) && !rr && OS(o, 0x126)) { e3_sound(W, 0xe205, c); OS(o, 0x126) = 0; }
            }
        }
    }
    if (offs(W, o, 0x100)) kill(W, o);
}

/* ---------------------------------------------------------------- 0x2d sliding cart */

#define FX3_SHADOW(o, t) do { if (!(OU((o), 0x50) & 0x20000)) (o)->fx2_shadow = (t); } while (0)   /* FX3: FUN_1001d600 */

static void t2d_init(E3World *W, E3Obj *o)
{
    if (!OW(o, 0x2a)) OW(o, 0x2a) = 8;
    OS(o, 0x116) = (int16_t)sflags(W, o);
    e3_set_anim(W, o, 5);
    OU(o, 0x50) = 0x202;
    FX3_SHADOW(o, 0x2017);                                         /* FX3: FUN_100023a0 */
    if (OS(o, 0x116) == 0 || OS(o, 0x116) == 1) { OW(o, 0x2a) = 8; OW(o, 0x34) = 0x55; }
    OS(o, 0x30) = 1; OW(o, 0x3a) = 300; OW(o, 0x98) = 1;
}
static void t2d_update(E3World *W, E3Obj *o)
{
    if (OW(o, 0x98)) { e3_sound(W, 0x2a16, o); OW(o, 0x98) = 0; }
    int16_t r[3]; rel(o, PL, r);
    if (r[0] < 0x300 && (OW(o, 0x2a) & 8)) { OW(o, 0x2a) = 2; e3_anim(W, o); }
    if (OW(o, 0x2a) & 2) { OU(o, 0x54) &= ~0x10u; OLAT(o) += 0x80000; e3_anim(W, o); }
    if (!o->used) return;
    int a = e3_attr_at(W, HI(ODEP(o)), HI(OY(o)), HI(OLAT(o)), OW(o, 0x38));
    if (a == 0x5e) OW(o, 0x2a) = 2;
    if (a == 0x5d) OW(o, 0x2a) = 4;
    if (OW(o, 0x2a) & 4) { kill(W, o); return; }
    if (e3_overlap(W, o, PL)) hit(W, o, 0x400);
    drop(W, o);
    if (offs(W, o, 0x100)) kill(W, o);
}

/* ---------------------------------------------------------------- 0x2f runaway team (three parts 0x4030) */

static void t2f_init(E3World *W, E3Obj *o)
{
    if (!OW(o, 0x2a)) OW(o, 0x2a) = 8;
    OW(o, 0x34) = 0; OS(o, 0x116) = 0; OS(o, 0x30) = 1;
    Group *g = group_new(W, o);
    for (int k = 0; k < 3; k++) {
        E3Obj *c = group_add(W, g, o, ODEP(o), OY(o), OLAT(o), 0x4030);
        if (!c) continue;
        OS(c, 0x30) = 1; OU(c, 0x50) = 2; OW(c, 0x2a) = 2; OI(c, 0xdc) = 0; OI(c, 0xd8) = 1;
        OB(c, 0x94) = 0xff; OB(c, 0x96) = 0;
        e3_set_anim(W, o, k); OU(c, 0x64) = OU(o, 0x64); OS(c, 0x32) = -1; OS(c, 0x30) = 0;
        OB(c, 0x22) = OB(o, 0x22);
        if (k == 0) { OI(o, 0x118) = (int)(c - W->obj) + 1; drop(W, c); }
        if (k == 1) { OLAT(c) += 0x100000; OU(c, 0x54) = 0x80000004u; }
        if (k == 2) { OLAT(c) -= 0x100000; OU(c, 0x54) = 0x80000014u; }
        OI(c, 0xac) = 0x70000; OI(c, 0xb0) = 0x3000; OI(c, 0xa8) = 0; OI(c, 0xc8) = 0; OI(c, 0xd0) = 0;
        OI(c, 0xd4) = 0x6000; OI(c, 0xcc) = 0x90000; OI(c, 0xe0) = 1;
    }
}
static void t2f_update(E3World *W, E3Obj *o)
{
    Group *g = group_of(W, o);
    E3Obj *lead = OI(o, 0x118) > 0 ? &W->obj[OI(o, 0x118) - 1] : NULL;
    if (g) for (int k = 0; k < g->n; k++) {
        E3Obj *c = gkid(W, g, k);
        if (!c) continue;
        if (OI(c, 0xe0) == 1 && OU(c, 0x64) == 0x10033710) { e3_sound(W, 0x2a16, c); OI(c, 0xe0) = 0; }
        e3_anim(W, c);
        int16_t r[3]; rel(c, PL, r);
        if (k == 0 && e3_overlap(W, c, PL)) hit(W, c, 0x400);
        OI(c, 0xd0) = 0;
        if (OW(c, 0x2a) & 2) OB(c, 0x96) = 0xff;
        if (OW(c, 0x2a) & 4) OB(c, 0x96) = 0;
        if (k == 0) { drop(W, c); e3_move_lat(c); e3_move(c); }
        if ((k == 1 || k == 2) && lead && lead->used) {
            if (OI(c, 0xdc)) OW(c, 0x34) = 0;
            ODEP(c) = ODEP(lead); OY(c) = OY(lead); OLAT(c) = OLAT(lead) + (k == 1 ? 0x200000 : -0x200000);
        }
        if (r[0] < -0x20) {
            if (OI(c, 0xd8)) {
                OU(c, 0x54) = 0x80000005u; OW(c, 0x4a) = 0x40; OI(c, 0xd8) = 0;
                if (OU(c, 0x64) == 0x10033710) e3_sound(W, rnd(2) ? 0x6217 : 0x6218, c);
            }
            OW(c, 0x4a) -= 4;
        }
    }
    if (offs(W, o, 0)) kill(W, o);
}

/* ---------------------------------------------------------------- 0x30 the column the Cyclops pulls down */

static void t30_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) = 0x202; OB(o, 0x94) = 0xff; OU(o, 0x54) |= 0x10000040; OB(o, 0x22) = 0;
    OW(o, 0x2a) = 8; OS(o, 0x116) = 0; OS(o, 0x11e) = 0x10; OB(o, 0x95) = 0;
    OI(o, 0xbc) = 0xff00000; OI(o, 0xac) = 0; OI(o, 0xcc) = 0; OI(o, 0xb8) = 0; OI(o, 0xc0) = 0xf000; OI(o, 0xc4) = 0;
    OI(o, 0xd8) = 0x200000; OI(o, 0xdc) = 0x10;
    OS(o, 0x30) = 1; OW(o, 0x3a) = 0x200; OW(o, 0x9a) = 0; OW(o, 0x34) = 0x21;
}
static void t30_update(E3World *W, E3Obj *o)
{
    OW(o, 0x9a) = 0;
    int16_t r[3]; rel(o, PL, r);
    if (OW(o, 0x2a) & 8) OW(o, 0x2a) = 2;
    if (r[0] < 0x578) {
        if (r[2] > -0x36 && r[2] < 0x36 && r[0] > -0x80 && r[0] < 0x80) hit(W, o, 2);
        set_prev(o);
        if (fall_floor(W, o)) { OI(o, 0xd8) = 0; OI(o, 0xdc) = 0; OW(o, 0x9a) = 0; }
        if (OW(o, 0x9a)) {
            OI(o, 0xd8) >>= 3;
            if (OS(o, 0x30) == 1) {
                e3_sound(W, 0xe206, o);
                for (int k = 0; k < 4; k++) {
                    E3Obj *c = debris(W, 0x4002, ODEP(o), rnd(0x200000) + OY(o), rnd(0x200000) + OLAT(o));
                    if (!c) continue;
                    OW(c, 0x3a) += 0x80;
                    OB(c, 0x94) = (k & 1) ? 0 : 0xff; OB(c, 0x96) = (k & 2) ? 0xff : 0;
                }
                OS(o, 0x30) = 0;
            }
        }
        ODEP(o) -= OI(o, 0xd8);
        OS(o, 0x72) += (int16_t)OI(o, 0xdc);
    } else ODEP(o) -= OI(o, 0xd8);
    int16_t s = (int16_t)OI(o, 0xdc);
    OS(o, 0x70) += s; OS(o, 0x72) += s; OS(o, 0x74) += s;
    OLAT(o) = 0x400000;
    if (offs(W, o, 0x100)) kill(W, o);
}

/* ---------------------------------------------------------------- 0x35 people crossing (three 0x402c) */

static void t35_init(E3World *W, E3Obj *o)
{
    if (!OW(o, 0x2a)) OW(o, 0x2a) = 8;
    OI(o, 0xdc) = (int)(sflags(W, o) & 0xff);
    OU(o, 0x50) = 2; OS(o, 0x116) = 0; OS(o, 0x30) = 0;
    Group *g = group_new(W, o);
    static const int AN[3] = {0xb, 0xd, 0xe};
    for (int k = 0; k < 3; k++) {
        E3Spawn *s = srec(W, o);
        int32_t d = s ? s->d : ODEP(o), y = s ? s->y : OY(o), l = s ? s->l : OLAT(o);
        E3Obj *c = group_add(W, g, o, k * 0x200000 + d, y, rnd(0x20) * k * 0x10000 + l, 0x402c);
        if (!c) continue;
        OS(c, 0x30) = 1; OU(c, 0x50) = 2; OW(c, 0x2a) = 2;
        FX3_SHADOW(c, 0x2002);                                     /* FX3: FUN_10003bf0 */
        OB(c, 0x94) = 0xff; OB(c, 0x96) = 0;
        e3_set_anim(W, o, AN[k]); OU(c, 0x64) = OU(o, 0x64); OS(c, 0x32) = -1; OS(c, 0x30) = 0; OB(c, 0x22) = OB(o, 0x22);
        OI(c, 0xbc) = 0; OI(c, 0xac) = 0;
        OI(c, 0xcc) = rnd(0x40000) + 0x40000;
        OI(c, 0xb0) = 0x2000; OI(c, 0xa8) = 0; OI(c, 0xd0) = rnd(0xa000) + 0x3000; OI(c, 0xc8) = OI(c, 0xcc);
    }
}
static void t35_update(E3World *W, E3Obj *o)
{
    Group *g = group_of(W, o);
    int32_t home = sl(W, o);
    if (g) for (int k = 0; k < g->n; k++) {
        E3Obj *c = gkid(W, g, k);
        if (!c) continue;
        if (k == 2) OU(c, 0x54) |= 0x10;
        e3_anim(W, c);
        int a = e3_attr_at(W, HI(ODEP(c)), HI(OY(c)), HI(OLAT(c)), OW(c, 0x38));
        if (a == 0x5e) { OB(c, 0x96) = 0; OW(c, 0x2a) = 2; }
        else if (home + 0x3800000 < OLAT(c)) OLAT(c) = home;
        drop(W, c); e3_move_lat(c); e3_move(c);
    }
    if (offs(W, o, 0x100)) kill(W, o);
}

/* ---------------------------------------------------------------- 0x39 collapsing floor (18 slabs 0x402f) */

static void t39_init(E3World *W, E3Obj *o)
{
    static const int LA[18] = {0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2};
    static const int DE[18] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5};
    if (!OW(o, 0x2a)) OW(o, 0x2a) = 8;
    OW(o, 0x34) = 0; OU(o, 0x50) = 0x40002; OU(o, 0x54) |= 0x10000040;
    OS(o, 0x116) = 0; OS(o, 0x11e) = 6; OW(o, 0x98) = 1;
    Group *g = group_new(W, o);
    for (int i = 0; i < 18; i++) {
        E3Obj *c = group_add(W, g, o, ODEP(o) - DE[i] * 0x800000, OY(o), LA[i] * 0x800000 + OLAT(o), 0x402f);
        if (!c) continue;
        OY(c) = 0x1800000;
        OB(c, 0x22) = 0; OW(c, 0x34) = (uint16_t)tab16(W, 0x10034f98u + 4u * (uint32_t)i);
        OU(c, 0x50) = 2; OU(c, 0x54) |= 0x10000040; OW(c, 0x2a) = 2;
        OI(c, 0xb8) = (rnd(3) + 1) * 0x10000;
        OB(c, 0x94) = 0xff; OB(c, 0x96) = 0;
        OW(o, 0x98) = 2; OS(c, 0x72) = 0; OW(c, 0x3a) = 0x200; OI(o, 0xe0) = 0;
        OI(c, 0xd8) = 1; OI(c, 0xdc) = rnd(0x28) + 2;
    }
}
static void t39_update(E3World *W, E3Obj *o)
{
    Group *g = group_of(W, o);
    int16_t r[3]; rel(o, PL, r);
    if (r[0] < 0x400) for (int k = 0; k < 3; k++) {
        W->camshake = OI(o, 0xe0);
        OI(o, 0xe0) += 0x20000;
        if (OI(o, 0xe0) > 0x80000) OI(o, 0xe0) = -0x80000;
    }
    if (g) for (int k = 0; k < g->n; k++) {
        E3Obj *c = gkid(W, g, k);
        if (!c) continue;
        int i = k + 1;
        if (r[0] < 0x400) {
            if (OW(o, 0x98)) { e3_sound(W, 0x213, NULL); OW(o, 0x98) = 0; }
            if (i != 0x10 && i != 0xd && i != 2 && i != 5) {
                if (--OI(c, 0xdc) < 1 && OI(c, 0xd8)) { dust(W, ODEP(c), OY(c) + 0x200000, OLAT(c)); OI(c, 0xd8) = 0; }
                OW(c, 0x4a) -= 3;
                int32_t y0 = OY(c);
                OY(c) = y0 + OI(c, 0xb8);
                if (OS(c, 0x4a) < 0x33) OW(c, 0x4a) = 0x32;
                if (y0 + OI(c, 0xb8) > 0x1bfffff) OI(c, 0xb8) = 0;
            }
        }
    }
    if (offs(W, o, 0x100)) kill(W, o);
}

/* ---------------------------------------------------------------- 0x3d swooping flyer */

static void t3d_init(E3World *W, E3Obj *o)
{
    if (!OW(o, 0x2a)) OW(o, 0x2a) = 2;
    OS(o, 0x116) = (int16_t)sflags(W, o);
    OU(o, 0x50) = 0x202; OU(o, 0x54) = 0x80000000u;
    FX3_SHADOW(o, 0x2017);                                         /* FX3: FUN_10004c10 */
    OI(o, 0xe0) = 0; OS(o, 0x126) = 0x20; OS(o, 0x11e) = 0; OS(o, 0x30) = 1; OW(o, 0x2a) = 2;
    ODEP(o) = ODEP(PL) + 0x9000000;
    e3_set_anim(W, o, 0);
}
static void t3d_update(E3World *W, E3Obj *o)
{
    if (OS(o, 0x11e) > 0xffe) OS(o, 0x126) += 4;
    int a = OS(o, 0x11e) & 0xfff;
    OS(o, 0x11e) = (int16_t)a;
    OLAT(o) = sl(W, o) + (int32_t)((uint32_t)(e3_cos(a - 0x400) * OS(o, 0x126)) & 0xfffff000u) * -0x20;
    OY(o) = OY(PL) + (int32_t)((uint32_t)(e3_cos(a) * OS(o, 0x126)) & 0xfffff000u) * -0x10 - 0x600000;
    OS(o, 0x11e) = (int16_t)(a + 0x4a);
    ODEP(o) -= 0x60000;
    e3_anim(W, o);
    if (!o->used) return;
    if (e3_overlap(W, o, PL)) hit(W, o, 0x800);
    if (offs(W, o, 0x100)) kill(W, o);
}

/* ---------------------------------------------------------------- 0x3e flyer dropping a boulder, 0x43 boulder */

static void t43_init(E3World *W, E3Obj *o)
{
    OU(o, 0x54) |= 0x10000040; OU(o, 0x50) = 0x202; OB(o, 0x22) = 0;
    OB(o, 0x94) = 0xff;
    OI(o, 0xbc) = 0xff00000; OI(o, 0xac) = 0; OI(o, 0xcc) = 0; OI(o, 0xb8) = 0x40000; OI(o, 0xc0) = 0xf000; OI(o, 0xc4) = 0;
    OI(o, 0xdc) = (int)(sflags(W, o) & 0xff);
    OW(o, 0x2a) = 8; OW(o, 0x34) = 2; OS(o, 0x74) = 0x100; OI(o, 0xe0) = 0;
}
static void t43_update(E3World *W, E3Obj *o)
{
    int16_t r[3]; rel(o, PL, r);
    if (OW(o, 0x2a) & 4) {
        OS(o, 0x70) += 0x20; ODEP(o) -= 0x40000;
        set_prev(o);
        if (fall_floor(W, o)) OW(o, 0x2a) = 0x20;
        if (OW(o, 0x9a) && OI(o, 0xe0) != 1) {
            e3_sound(W, 0xe206, o);
            dust(W, ODEP(o) - 0x800000, OY(o), OLAT(o));
            for (int k = 0; k < 16; k++) {
                E3Obj *c = debris(W, 0x4002, ODEP(o), rnd(0x20) * 0x10000 + OY(o), rnd(0x20) * 0x10000 + OLAT(o));
                if (!c) continue;
                OI(c, 0xbc) = OI(c, 0xac) = rnd(0x300000) + 0x300000; OI(c, 0xcc) = OI(c, 0xac) >> 1;
                OW(c, 0x4a) = (uint16_t)(rnd(0x20) + 0x80);
                dir4(c, k, D94A, D96A);
            }
            OI(o, 0xe0) = 1;
        }
    }
    if (OW(o, 0x2a) & 0x20) { OS(o, 0x70) += 0x20; ODEP(o) -= 0x40000; }
    if (r[2] > -0x40 && r[2] < 0x40 && r[0] > -0x20 && r[0] < 0x40 && r[1] != 0x20 && r[1] < 0x20) hit(W, o, 0x200);
    if (offs(W, o, 0x100)) kill(W, o);
}

static void t3e_init(E3World *W, E3Obj *o)
{
    if (!OW(o, 0x2a)) OW(o, 0x2a) = 8;
    OI(o, 0xdc) = (int)(sflags(W, o) & 0xff);
    OW(o, 0x34) = 0; OU(o, 0x50) = 2; OS(o, 0x116) = 0; OS(o, 0x30) = 1;
    Group *g = group_new(W, o);
    E3Obj *c = group_add(W, g, o, ODEP(o), OY(o), OLAT(o), 0x4032);
    if (!c) return;
    OS(c, 0x30) = 1; OI(c, 0xd8) = 0; OU(c, 0x50) = 2; OW(c, 0x2a) = 8;
    FX3_SHADOW(c, 0x2017);                                         /* FX3: FUN_10004dc0 */
    OB(c, 0x97) = 1; OB(c, 0x94) = 0xff; OB(c, 0x96) = 0xff;
    e3_set_anim(W, o, 0); OU(c, 0x64) = OU(o, 0x64); OS(c, 0x32) = -1; OS(c, 0x30) = 0; OB(c, 0x22) = OB(o, 0x22);
    OI(c, 0xbc) = 0x40000; OI(c, 0xac) = rnd(0x80000) + 0x90000; OI(c, 0xcc) = 0;
    OI(c, 0xb0) = rnd(0x4000) + 0x2000; OI(c, 0xd0) = rnd(0x2000) + 0x2000; OI(c, 0xa8) = 0; OI(c, 0xc8) = 0;
}
static void t3e_update(E3World *W, E3Obj *o)
{
    Group *g = group_of(W, o);
    E3Obj *c = g ? gkid(W, g, 0) : NULL;
    if (c) {
        OI(c, 0xcc) = rnd(0x10000) + 0x10000;
        int16_t r[3]; rel(c, PL, r);
        int16_t t = OS(o, 0x11e);
        OS(o, 0x11e) = t - 1;
        if (t < 0) { e3_sound(W, (rnd(0x100) & 1) ? 0x6222 : 0x6223, c); OS(o, 0x11e) = 100; }
        E3Obj *rock = OI(o, 0x118) > 0 ? &W->obj[OI(o, 0x118) - 1] : NULL;
        if (rock && !rock->used) rock = NULL;
        if (OW(c, 0x2a) & 8) {
            E3Obj *b = e3_spawn(W, 0x43, ODEP(c), OY(c), OLAT(c), OW(o, 0x38));
            if (b) { t43_init(W, b); OW(b, 0x38) = OW(o, 0x38); OI(o, 0x118) = (int)(b - W->obj) + 1; OW(c, 0x2a) = 4; rock = b; }
        }
        if ((OW(c, 0x2a) & 4) && rock) {
            ODEP(rock) = ODEP(c); OY(rock) = OY(c) + 0x600000; OLAT(rock) = OLAT(c);
            if (r[0] < 0x300) { OW(rock, 0x2a) = 4; e3_sound(W, 0x6224, c); OW(c, 0x2a) = 2; }
        }
        e3_anim(W, c);
        if (r[0] < 900) {
            OY(c) += (rnd(7) + 7) * 0x10000;
            if (OY(c) > 0x17fffff) OY(c) = 0x1800000;
        } else {
            OY(c) = e3_cos(OI(c, 0xd8) - 0x400) * 0x400 + sy(W, o);
            OI(c, 0xd8) += 0x80;
        }
        if (r[0] < 0x100) {
            if (OLAT(c) < 0) OLAT(c) -= 0x50000; else OLAT(c) += 0x50000;
            OY(c) -= 0x120000;
        }
        if (e3_overlap(W, c, PL)) hit(W, c, 4);
        int a = e3_attr_at(W, HI(ODEP(c)), HI(OY(c)), HI(OLAT(c)) + 0x20, OW(c, 0x38));
        if (a == 0x5e) { OB(c, 0x96) = 0; OI(c, 0xcc) = rnd(0x10000) + 0x60000; }
        if (a == 0x5d) { OB(c, 0x96) = 0xff; OI(c, 0xcc) = rnd(0x10000) + 0x60000; }
        e3_move_lat(c); e3_move(c);
    }
    if (offs(W, o, 0x100)) kill(W, o);
}

/* ---------------------------------------------------------------- 0x3f crowd spawner, 0x4033 crowd */

static int crowd_track(int path) { return path == 0 ? 2 : path == 1 ? 5 : path == 2 ? 8 : 0; }

static void t3f_init(E3World *W, E3Obj *o)
{
    OW(o, 0x2a) = 2; OS(o, 0x11e) = 8; OI(o, 0xdc) = 0x18; OS(o, 0x126) = 0;
    OW(o, 0x98) = 0; OI(o, 0xd8) = 0; OI(o, 0xe0) = 0; OS(o, 0x116) = 0;
    OU(o, 0x50) = 2;
}
static void t3f_update(E3World *W, E3Obj *o)
{
    OS(o, 0x116)++;
    if (OW(o, 0x2a) & 2) {
        int n = (int)e3_rand(6);
        for (int i = 0; i < n; i++) {
            if (++OI(o, 0xe0) > 0x20) return;
            int32_t l = ((int)e3_rand(0x80) - (int)e3_rand(0x80)) * 0x10000;
            E3Obj *c = e3_spawn(W, 0x4033, ((int)e3_rand(0x180) + 0x680) * 0x10000 + ODEP(PL), 0x1c00000, l, OW(o, 0x38));
            if (!c) continue;
            c->parent = (int)(o - W->obj) + 1;
            OU(c, 0x50) = 0x202; OU(c, 0x54) = 0x80000000u;
            OI(c, 0xbc) = 0x40000; OI(c, 0xac) = rnd(0x80000) + 0x90000; OI(c, 0xcc) = 0;
            OI(c, 0xb0) = rnd(0x4000) + 0x2000; OI(c, 0xd0) = rnd(0x2000) + 0x2000; OI(c, 0xa8) = 0; OI(c, 0xc8) = 0;
            OB(c, 0x94) = 0xff; OI(c, 0xdc) = 0; OI(c, 0xe0) = 1; OW(c, 0x4a) = 0x80; OW(c, 0x2a) = 8;
            OI(c, 0xd8) = (int)(o - W->obj) + 1;
            OS(o, 0x98) = OS(o, 0x126);
            OB(c, 0x96) = (uint8_t)(rnd(2) ? 0xff : 0);
            OW(c, 0x38) = OW(o, 0x38);
            int s = (int)e3_rand(0xb);
            OS(o, 0x126) = (int16_t)s;
            if (OS(o, 0x98) == s) OS(o, 0x126) = (int16_t)(s + 1);
            if (OS(o, 0x126) > 10) OS(o, 0x126) = 0;
            s = OS(o, 0x126);
            if (s == 5 || s == 7) { e3_set_anim(W, o, 6); OW(c, 0x1c) = (uint16_t)(s == 5 ? 3 : 5); }
            else e3_set_anim(W, o, s);
            OU(c, 0x64) = OU(o, 0x64); OS(c, 0x32) = -1; OS(c, 0x30) = 0;
            uint32_t sc = OU(c, 0x64);
            if (sc == 0x100335b0) { int q = (int)e3_rand(3); OW(c, 0x1c) = (uint16_t)(q == 0 ? 0x15 : q == 1 ? 0x18 : 0x14); }
            if (sc == 0x10033418) OW(c, 0x1c) = (uint16_t)(e3_rand(2) ? 0x16 : 6);
            if (sc == 0x10033448) OW(c, 0x1c) = (uint16_t)(e3_rand(2) ? 7 : 0x1a);
            if (sc == 0x10033610) OW(c, 0x1c) = 0x1b;
            if (sc == 0x100336e0) OW(c, 0x1c) = 0x1d;
            e3_drop(W, c);
        }
        OW(o, 0x2a) = 4;
    }
    if (OW(o, 0x2a) & 4) {
        int16_t t = OS(o, 0x11e);
        OS(o, 0x11e) = t - 1;
        if (t < 0) { OW(o, 0x2a) = 2; OS(o, 0x11e) = (int16_t)OI(o, 0xdc); }
    }
    int a = e3_attr_at(W, HI(ODEP(PL)), HI(OY(PL)), HI(OLAT(PL)), OW(PL, 0x38));
    if (a == 0x6e) { OS(o, 0x11e) = 0x40; OI(o, 0xdc) = 0x40; }
    else if (a == 0x6f) { OS(o, 0x11e) = 0x20; OI(o, 0xdc) = 0x20; }
    else if (a == 0x70) kill(W, o);
}
static void crowd_update(E3World *W, E3Obj *c)   /* FUN_10005760 */
{
    int t = crowd_track(OW(c, 0x38));
    OW(c, 0x98)++;
    OI(c, 0xcc) = rnd(0x10000) + 0x10000;
    int16_t r[3]; rel(c, PL, r);
    if (r[0] < 0x20 && r[0] > -0x20 && r[2] < 0x20 && r[2] > -0x20 && r[1] < 0x20 && r[1] > -0x20) hit(W, c, 4);
    int lane[7];
    if (e3_track(W, t, HI(ODEP(c)), lane)) {
        if (OLAT(c) <= lane[0] * 0x10000) { OB(c, 0x96) = 0; OI(c, 0xcc) = rnd(0x10000) + 0x70000; }
        if (lane[2] * 0x10000 <= OLAT(c)) { OB(c, 0x96) = 0xff; OI(c, 0xcc) = rnd(0x10000) + 0x70000; }
    }
    e3_move_lat(c); e3_move(c);
    OY(c) = 0x1c00000;
    e3_anim(W, c);
    if (!c->used) return;
    if (ODEP(c) < ODEP(PL) + 0x1000000) {
        if (OW(c, 0x2a) & 8) {
            uint32_t s = OU(c, 0x64);
            static const struct { uint32_t s; int id; } SND[] = {{0x10033418, 0xe21d}, {0x100334a8, 0xe21e}, {0x100334d0, 0xe21e},
                {0x10033448, 0xe21b}, {0x100335b0, 0xe21c}, {0x100336e0, 0xe215}, {0x10033610, 0xe21b}};
            for (size_t k = 0; k < sizeof SND / sizeof SND[0]; k++) if (s == SND[k].s) e3_sound(W, SND[k].id, c);
        }
        OW(c, 0x2a) = 4;
    }
    if (offs(W, c, 0x100)) {
        E3Obj *p = OI(c, 0xd8) > 0 ? &W->obj[OI(c, 0xd8) - 1] : NULL;
        if (p && p->used) OI(p, 0xe0)--;
        e3_remove(W, c);
    }
}

/* ---------------------------------------------------------------- 0x41 the pit (attribute 0x75) */

static void t41_init(E3World *W, E3Obj *o)
{
    OI(o, 0xdc) = 0;
    OU(o, 0x50) = 0x202; OW(o, 0x38) = OW(PL, 0x38); OU(o, 0x54) = 0x80000000u; OS(o, 0x11e) = 0;
}
static void t41_update(E3World *W, E3Obj *o)
{
    OW(o, 0x34) = 0;
    ODEP(PL) = ODEP(o); OLAT(PL) = OLAT(o); OY(PL) = OY(o);
    if (W->state != 6) e3_die(W);
}

void cyc_attr_effect(E3World *W, int a)     /* FUN_10017ae0 case 0x75 */
{
    if (a != 0x75 || OY(PL) < 0x1f40000 || OI(PL, 0xe0)) return;
    E3Obj *o = e3_spawn(W, 0x41, ODEP(PL), OY(PL), OLAT(PL), OW(PL, 0x38));
    if (o) t41_init(W, o);
    OI(PL, 0xe0) = 1;
}

/* ---------------------------------------------------------------- 0x42 fleeing citizens */

static void t42_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) = 0x202;
    E3Spawn *s = srec(W, o);
    int32_t home = s ? s->l : OLAT(o);
    OU(o, 0x54) = 0x80000000u;
    OI(o, 0xdc) = (int)(sflags(W, o) & 0xff);
    OW(o, 0x2a) = 8;
    OI(o, 0xbc) = 0; OI(o, 0xcc) = 0; OI(o, 0xac) = 0;
    OI(o, 0xb0) = rnd(0x4000) + 0x2000; OI(o, 0xd0) = rnd(0x2000) + 0x2000; OI(o, 0xa8) = 0; OI(o, 0xc8) = 0;
    OS(o, 0x11e) = (int16_t)rnd(0x80);
    if (OI(o, 0xdc) == 0) { if (home > 0) OU(o, 0x54) |= 0x10; e3_set_anim(W, o, 0xd); }
    if (OI(o, 0xdc) == 1) { OU(o, 0x54) |= 0x10; if (home > 0) OU(o, 0x54) &= ~0x10u; e3_set_anim(W, o, 0xc); }
    if (OI(o, 0xdc) == 2) { if (home > 0) OU(o, 0x54) |= 0x10; e3_set_anim(W, o, 0xb); }
    e3_drop(W, o);
}
static void t42_update(E3World *W, E3Obj *o)
{
    int16_t r[3]; rel(o, PL, r);
    if (OW(o, 0x2a) & 8) {
        int right = sl(W, o) >= 0;
        OS(o, 0x116) = (int16_t)right;
        OI(o, 0xcc) = (rnd(3) + 3) * 0x10000;
        OB(o, 0x96) = right ? 0xff : 0;
        OW(o, 0x2a) = 2;
    }
    if (OW(o, 0x2a) & 2) {
        int a;
        if (OS(o, 0x116) == 0) {
            a = e3_attr_at(W, HI(ODEP(o)), HI(OY(o)), OS(o, 0x11e) + HI(OLAT(o)) + 0x100, OW(o, 0x38));
            if (a == 0x5d) { OB(o, 0x96) = 0xff; OW(o, 0x2a) = 4; }
        } else {
            a = e3_attr_at(W, HI(ODEP(o)), HI(OY(o)), HI(OLAT(o)) - OS(o, 0x11e) - 0x80, OW(o, 0x38));
            if (a == 0x5e) { OB(o, 0x96) = 0; OW(o, 0x2a) = 4; }
        }
    }
    if (OW(o, 0x2a) & 4) {
        if (OI(o, 0xdc) == 0) e3_set_anim(W, o, 0);
        if (OI(o, 0xdc) == 1) { e3_set_anim(W, o, 8); OW(o, 0x1c) = 0x1b; }
        if (OI(o, 0xdc) == 2) e3_set_anim(W, o, 1);
        OI(o, 0xbc) = 0x40000; OI(o, 0xac) = rnd(0x80000) + 0x90000; OI(o, 0xcc) = 0;
        OI(o, 0xb0) = rnd(0x4000) + 0x2000; OI(o, 0xd0) = rnd(0x2000) + 0x2000; OI(o, 0xa8) = 0; OI(o, 0xc8) = 0;
        OB(o, 0x94) = 0xff; OW(o, 0x2a) = 0x20;
    }
    if (OW(o, 0x2a) & 0x20) {
        OI(o, 0xcc) = rnd(0x10000) + 0x10000;
        if (e3_overlap(W, o, PL)) hit(W, o, 4);
        int lane[7];
        if (e3_track(W, 0, HI(ODEP(o)), lane)) {
            if (OLAT(o) <= lane[0] * 0x10000) { OB(o, 0x96) = 0; OI(o, 0xcc) = rnd(0x10000) + 0x70000; }
            if (lane[2] * 0x10000 <= OLAT(o)) { OB(o, 0x96) = 0xff; OI(o, 0xcc) = rnd(0x10000) + 0x70000; }
        }
    }
    e3_move_lat(o); e3_move(o);
    e3_anim(W, o);
    if (o->used && offs(W, o, 0x100)) kill(W, o);
}

/* ---------------------------------------------------------------- 0x47 drifting embers */

static void t47_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) = 0x202; OW(o, 0x2a) = 4;
    OS(o, 0x116) = (int16_t)e3_rand(8);
    e3_set_anim(W, o, (e3_rand(0x100) & 1) == 0);
    e3_drop(W, o);
    OI(o, 0xdc) = OY(o);
    OS(o, 0x126) = (int16_t)e3_rand(0xfff);
    OS(o, 0x11e) = (int16_t)(e3_rand(0x20) + 0x20);
    OW(o, 0x3a) = 0x180;
    OS(o, 0x32) = (int16_t)e3_rand(8);
}
static void t47_update(E3World *W, E3Obj *o)
{
    OLAT(o) += (OS(o, 0x116) + 3) * 0x10000;
    int c = e3_cos(OS(o, 0x126) - 0x400) * OS(o, 0x11e);
    OS(o, 0x126) += 0x20;
    OY(o) = (OI(o, 0xdc) + c * -0x10) - 0x280000;
    e3_anim(W, o);
    if (o->used && offs(W, o, 0x100)) kill(W, o);
}

/* ---------------------------------------------------------------- 0x48 barricade */

static void t48_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) = 0x400202; OU(o, 0x54) |= 0x10000040; OB(o, 0x22) = 0;
    OW(o, 0x2a) = 8; OI(o, 0xdc) = (int)(sflags(W, o) & 0xff);
    OW(o, 0x34) = 0x34; OS(o, 0x72) = 0x200; OW(o, 0x3a) = 0x200; OI(o, 0xe0) = 0; OB(o, 0x97) = 2;
    e3_drop(W, o);
    OY(o) -= 0x200000;
}
static void t48_update(E3World *W, E3Obj *o)
{
    int16_t r[3]; rel(o, PL, r);
    int16_t b[6];
    if (e3_model_box(W, o, b) && -(b[3] >> 1) < r[2] && r[2] < (b[3] >> 1) && -(b[5] >> 1) < r[0] && r[0] < (b[5] >> 1)
        && -b[4] < r[1] && r[1] < b[4]) {
        hit(W, o, 0x36);
        if (W->boost) {                 /* the speed power-up smashes through */
            dust(W, ODEP(o) + 0x1000000, OY(o), OLAT(o));
            e3_sound(W, 0x22b, NULL);
            for (int k = 0; k < 16; k++) {
                int32_t l = (rnd(0x80) - rnd(0x80)) * 0x10000 + OLAT(o);
                int32_t y = rnd(0x40) * 0x10000 - rnd(0x40) * 0x10000 + OY(o);
                E3Obj *c = debris(W, 0x4004, ODEP(o) + 0x800000, y, l);
                if (!c) continue;
                OI(c, 0xbc) = OI(c, 0xb8) = ((int)e3_rand(4) + 0x10) * 0x10000; OI(c, 0xc4) = 0x28000;
                OI(c, 0xac) = OI(c, 0xa8) = ((int)e3_rand(8) + 0x1e) * 0x10000; OI(c, 0xb0) = 0; OI(c, 0xb4) = 0x100;
                OI(c, 0xd0) = (int)e3_rand(0x80); OI(c, 0xcc) = 0x1e0000;
                OB(c, 0x94) = 0; OB(c, 0x96) = (k & 2) ? 0xff : 0;
            }
            kill(W, o);
            return;
        }
    }
    if (offs(W, o, 0x100)) kill(W, o);
}

/* ---------------------------------------------------------------- checkpoint statues 0x200b..0x2014 (LV_CYCLOPS) */
/* FUN_100165f0 (init) / FUN_10016670 (update, disassembled). The statue of checkpoint n = (type + 6) & 0xf. It is inactive
 * (script 0x10037c00, frames 0xeb / 0xe6..0xea) until Hercules comes within 0x2bc rows of it on the same path; then it
 * plays the chime 0x6028 (only when its state word is non-zero, i.e. not while it was already the current one),
 * switches to the active script 0x10037ba8 (frames 0xec..) and makes itself the restart point (gamestruct[2]).
 * Any other statue becoming current sends this one back to the inactive script. The port never set W->cp, so every
 * death restarted the level at path 0 depth 0 (table 0x10038534 +0x18 per checkpoint: path 1 / path 2 start for n = 2 / 3).
 * The sparkle shower (0x201b children, FUN_100168b0 / 100168f0, spawned on odd ticks while current) is not ported. */
#define CP_ACTIVE 0x10037ba8u
#define CP_IDLE 0x10037c00u
static int cp_index(E3Obj *o) { return (OW(o, 0x2e) + 6) & 0xf; }
static void cp_reset(E3Obj *o) { OS(o, 0x30) = 0; OS(o, 0x32) = 0; }          /* FUN_1001e2d0 */
static void wscript(E3World *W, E3Obj *o)      /* FUN_1001e000: word script, frame list with a common delay in word 0 */
{
    uint32_t sc = OU(o, 0x64);
    if (!sc) return;
    OS(o, 0x30)--;
    if (OS(o, 0x30) >= 0) return;
    int16_t v;
    for (int guard = 0; guard < 64; guard++) {
        OS(o, 0x32)++;
        v = tab16(W, sc + 2u * (uint32_t)OS(o, 0x32));
        if (v >= 0) break;
        switch (v) {
        case -0x28: OS(o, 0x32)++; e3_sound(W, (uint16_t)tab16(W, sc + 2u * (uint32_t)OS(o, 0x32)), o); break;
        case -0x27: OS(o, 0x32)++; e3_sound(W, (uint16_t)tab16(W, sc + 2u * (uint32_t)OS(o, 0x32)), NULL); break;
        case -9: OS(o, 0x32)++; OS(o, 0x30) = tab16(W, sc + 2u * (uint32_t)OS(o, 0x32)); break;
        case -6: case -7: e3_remove(W, o); return;
        case -3: OU(o, 0x54) ^= 0x10; break;
        case -1: OS(o, 0x32) = 0; break;
        default: break;
        }
    }
    OW(o, 0x34) = (uint16_t)v;
    if (OS(o, 0x30) < 0) OS(o, 0x30) = tab16(W, sc);
}
static void cp_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) = 2;
    OU(o, 0x64) = W->cp == cp_index(o) ? CP_ACTIVE : CP_IDLE;
    OW(o, 0x2a) = 1;
    cp_reset(o);
    wscript(W, o);
    OY(o) = 0x1a00000;
}
static void cp_update(E3World *W, E3Obj *o)
{
    int16_t r[3]; rel(o, PL, r);
    if (OU(o, 0x64) == CP_IDLE) {
        if (r[0] < 0x2bc && OW(o, 0x38) == OW(PL, 0x38)) {
            if (OW(o, 0x2a)) { e3_sound(W, 0x6028, o); OW(o, 0x2a) = 0; }
            OU(o, 0x64) = CP_ACTIVE;
            W->cp = cp_index(o);
            if (getenv("LV_CYC_CPLOG")) printf("CHECKPOINT %d reached t=%ld path %d d=%d\n", W->cp, W->ticks, OW(PL, 0x38), HI(ODEP(PL)));
            cp_reset(o);
        } else OW(o, 0x2a) = (uint16_t)(OW(o, 0x2a) + 4);
    } else if (W->cp != cp_index(o)) { OU(o, 0x64) = CP_IDLE; cp_reset(o); }
    wscript(W, o);
    if (o->used && offs(W, o, 0x100)) kill(W, o);
}

/* ---------------------------------------------------------------- dispatch */

int cyc_type_init(E3World *W, E3Obj *o)
{
    int t = OW(o, 0x2e);
    grp[o - W->obj].owner = 0;
    if (t >= 0x200b && t <= 0x2014) { cp_init(W, o); return 1; }      /* LV_CYCLOPS */
    switch (t) {
    case 0x2a: t2a_init(W, o); return 1;
    case 0x2b: case 0x2e: case 0x45: column_init(W, o, t); return 1;
    case 0x2d: t2d_init(W, o); return 1;
    case 0x2f: t2f_init(W, o); return 1;
    case 0x30: t30_init(W, o); return 1;
    case 0x35: t35_init(W, o); return 1;
    case 0x39: t39_init(W, o); return 1;
    case 0x3d: t3d_init(W, o); return 1;
    case 0x3e: t3e_init(W, o); return 1;
    case 0x3f: t3f_init(W, o); return 1;
    case 0x41: t41_init(W, o); return 1;
    case 0x42: t42_init(W, o); return 1;
    case 0x43: t43_init(W, o); return 1;
    case 0x47: t47_init(W, o); return 1;
    case 0x48: t48_init(W, o); return 1;
    case 0x38: OU(o, 0x50) = 2; return 1;       /* thrown object: no handler in the original */
    default: break;
    }
    if (t & 0x4000) return 1;       /* children: set up by their parents */
    return 0;
}

int cyc_type_update(E3World *W, E3Obj *o)
{
    int t = OW(o, 0x2e);
    if (t == 0x29) { cyclops_update(W, o); return 1; }
    if (t == 0x2016) { dust_update(W, o); return 1; }
    if (t >= 0x200b && t <= 0x2014) { cp_update(W, o); return 1; }      /* LV_CYCLOPS */
    if (t & 0x4000) {
        int ix = t & 0xfff;
        if (ix == 2 || ix == 3 || ix == 4) { debris_update(W, o); return 1; }
        if (ix == 0x33) { crowd_update(W, o); return 1; }
        E3Obj *p = o->parent > 0 ? &W->obj[o->parent - 1] : NULL;     /* group pieces live while their owner does */
        if (!p || !p->used) e3_remove(W, o);
        return 1;
    }
    switch (t) {
    case 0x2a: t2a_update(W, o); return 1;
    case 0x2b: case 0x2e: case 0x45: column_update(W, o, t); return 1;
    case 0x2d: t2d_update(W, o); return 1;
    case 0x2f: t2f_update(W, o); return 1;
    case 0x30: t30_update(W, o); return 1;
    case 0x35: t35_update(W, o); return 1;
    case 0x39: t39_update(W, o); return 1;
    case 0x3d: t3d_update(W, o); return 1;
    case 0x3e: t3e_update(W, o); return 1;
    case 0x3f: t3f_update(W, o); return 1;
    case 0x41: t41_update(W, o); return 1;
    case 0x42: t42_update(W, o); return 1;
    case 0x43: t43_update(W, o); return 1;
    case 0x47: t47_update(W, o); return 1;
    case 0x48: t48_update(W, o); return 1;
    case 0x38: if (offs(W, o, 0x100)) e3_remove(W, o); return 1;
    default: break;
    }
    return 0;
}

void cyc_level_reset(void) { prev_cd = 0x7fffffff; memset(grp, 0, sizeof grp); }
