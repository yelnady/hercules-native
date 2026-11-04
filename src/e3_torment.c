/* TORMENT: per-type object handlers for the Passageways of Torment (ENG3 level 10), ported from the Torment
 * handler table (ENG3 0x1003b990 entry +0x1c = 12-byte {init, update, update2} per actor type; class 0x2000 /
 * 0x4000 tables at +0x2c / +0x24). Field offsets are the original object offsets (see e3world.h).
 *
 *   0x29 ghost carrier flying over the path, drops 0x2a ghosts (FUN_10025700 / FUN_10025770), escort 0x3f
 *   0x2a falling ghost -> 0x2b soul splash that hurts (FUN_10025960 / 100259e0, FUN_10025d20 / 10025d80)
 *   0x2e flock of four lost souls circling toward Hercules, 0x43 trails (FUN_10025ec0 / 10026340, 10025e30 / e90)
 *   0x2f wailing soul rising out of the floor (FUN_10026550 / FUN_10026600)
 *   0x31 / 0x32 souls drifting toward Hercules (FUN_10026b80 / 10026c60, FUN_10026d50 / 10026de0)
 *   0x35 coffin: lid breaks open, souls fly out (FUN_100272e0 / 10027370) -> 0x36 lid, 0x37 lid pieces, 0x3b bones
 *   0x3c soul river under the rafts (attribute 99 / 0x77, FUN_100284a0 / FUN_100284f0)
 *   0x3d / 0x3e skeleton arm snatching from the wall (FUN_100287b0 / 10028840, FUN_10028960 / 100289a0)
 *   0x40 skull pile that bursts when trodden on (FUN_10028a90 / 10028ae0) -> 0x41 skull pieces, 0x3b bones
 *   0x45 rolling soul wheels bouncing between the walls (FUN_10029140 / FUN_100292f0), children 0x402a model 7
 *   0x46 rock fall: twenty boulders dropping onto the path (FUN_100297d0 / FUN_10029a10), children 0x402b model 7
 *   0x2016 soul sparkle (FUN_10014b30 / 10014bb0), 0x4002 / 0x4004 debris (FUN_10014d90 / 10014f00, 10015170 / 152c0)
 * Physics: FUN_1001f690 (mover), FUN_10001600 / FUN_10001460 (gravity with bounce), FUN_1001eaf0 (landing). */
#include "e3world.h"
#include "eng3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HI(v) ((int16_t)((uint32_t)(v) >> 16))
#define PL (&W->obj[0])
#define TFLAG(o) OB(o, 0x13f)       /* our own "init done" marker for objects spawned without an init call */

static int ia(int v) { return v < 0 ? -v : v; }
static uint32_t rnd(int n) { return n > 0 ? e3_rand(n) : 0; }       /* FUN_10001010 / FUN_1001fab0 */
static void rel(E3Obj *a, E3Obj *b, int16_t r[3])                  /* FUN_10001420 */
{
    r[0] = HI(ODEP(a) - ODEP(b)); r[1] = HI(OY(a) - OY(b)); r[2] = HI(OLAT(a) - OLAT(b));
}
static void hit(E3World *W, E3Obj *by, int kind)
{
    OI(PL, 0x110) = (int32_t)(by - W->obj) + 1;
    OS(PL, 0x116) = (int16_t)kind;
    if (W->state != 5) e3_hurt(W, kind, by);
}
static int gone(E3World *W, E3Obj *o) { if (!e3_offscreen(W, o, 0x100)) return 0; e3_remove(W, o); return 1; }
static void set_prev(E3Obj *o) { OI(o, 0x9c) = ODEP(o); OI(o, 0xa0) = OY(o); OI(o, 0xa4) = OLAT(o); }

/* FUN_1001cb00 + the type's init: spawn an object of `type` at a point and initialise it. */
static E3Obj *spawn(E3World *W, int type, int32_t d, int32_t y, int32_t l, int path)
{
    E3Obj *c = e3_spawn(W, type, d, y, l, path);
    if (!c) return NULL;
    set_prev(c);
    e3_type_init(W, c);
    TFLAG(c) = 1;
    return c;
}

/* FUN_1001f690: three-axis mover (+0xa8 speed / +0xac max / +0xb0 acc / +0xb4 dec, same at +0xb8 and +0xc8). */
static void mover(E3Obj *o)
{
    int32_t v;
    v = OI(o, 0xa8) + (OI(o, 0xb0) - OI(o, 0xb4)); if (v < 0) v = 0; if (v > OI(o, 0xac)) v = OI(o, 0xac); OI(o, 0xa8) = v;
    v = OI(o, 0xb8) + (OI(o, 0xc0) - OI(o, 0xc4)); if (v < 0) v = 0; if (v > OI(o, 0xbc)) v = OI(o, 0xbc); OI(o, 0xb8) = v;
    v = OI(o, 0xc8) + (OI(o, 0xd0) - OI(o, 0xd4)); if (v < 0) v = 0; if (v > OI(o, 0xcc)) v = OI(o, 0xcc); OI(o, 0xc8) = v;
    ODEP(o) += OB(o, 0x94) ? -OI(o, 0xa8) : OI(o, 0xa8);
    OY(o) += OB(o, 0x95) ? -OI(o, 0xb8) : OI(o, 0xb8);
    OLAT(o) += OB(o, 0x96) ? -OI(o, 0xc8) : OI(o, 0xc8);
}

/* FUN_1001eaf0 (map part): has the object reached the floor? Snaps it up onto the floor. */
static int landed(E3World *W, E3Obj *o)
{
    int dy = ia(HI(OY(o) - OI(o, 0xa0))), dd = ia(HI(ODEP(o) - OI(o, 0x9c))), dl = ia(HI(OLAT(o) - OI(o, 0xa4)));
    int up, dn = e3_floor(W, HI(ODEP(o)), HI(OY(o)), HI(OLAT(o)), OW(o, 0x38), &up);
    if (dn == 0x7fff && up == 0x7fff) return 0;
    if (dn == 0) return 1;
    if (up != 0x7fff && up <= (dl + dd + 2 + dy) * 2) { OY(o) -= up << 16; return 1; }    /* DAT_100431e0 = 2 (Torment) */
    return 0;
}

/* FUN_10001600 (model = 1: models are tested from half their box height) / FUN_10001460 (model = 0).
 * Returns 1 once the bounce has died out; +0x9a = 1 on the tick of a bounce. */
static int fall(E3World *W, E3Obj *o, int model)
{
    mover(o);
    OW(o, 0x9a) = 0;
    if (OI(o, 0xb8) == 0) {
        OI(o, 0xc0) = OI(o, 0xc4); OI(o, 0xc4) = 0; OB(o, 0x95) = 0;
        OI(o, 0xb4) = OI(o, 0xb0); OI(o, 0xb0) = 0;
    }
    if (OB(o, 0x95)) return 0;
    int16_t b[6]; int half = 0;
    if (model && (OU(o, 0x54) & 0x10000000) && OW(o, 0x34) && e3_model_box(W, o, b)) { half = b[3]; OY(o) += half * 0x8000; }
    if (landed(W, o)) {
        int32_t vy = OI(o, 0xb8);
        OI(o, 0xb0) = OI(o, 0xb4); OI(o, 0xb8) = vy >> 1; OI(o, 0xc4) = OI(o, 0xc0); OI(o, 0xc0) = 0;
        OB(o, 0x95) = 0xff; OI(o, 0xac) >>= 1; OI(o, 0xb4) = 0; OW(o, 0x9a) = 1; OI(o, 0xcc) >>= 1;
        if ((vy >> 1) == 0) { OY(o) -= half * 0x8000; return 1; }
    }
    OY(o) -= half * 0x8000;
    return 0;
}

/* the debris kick used by several handlers: random speeds, 4 direction combinations by index */
static void dirs(E3Obj *c, int i, int flip)
{
    static const uint8_t D94[4] = {0xff, 0, 0xff, 0}, D96[4] = {0, 0, 0xff, 0xff};
    OB(c, 0x94) = D94[i & 3]; OB(c, 0x96) = D96[i & 3];
    if (flip) OB(c, 0x96) = 0xff;
}

/* FUN_1001d670 + the 0x2016 init: a soul sparkle at a point */
static void sparkle(E3World *W, int32_t d, int32_t y, int32_t l) { spawn(W, 0x2016, d, y, l, OW(PL, 0x38)); }

/* ---------------------------------------------------------------- 0x29 / 0x2a / 0x2b / 0x3f */

#define FX3_SHADOW(o, t) do { if (!(OU((o), 0x50) & 0x20000)) (o)->fx2_shadow = (t); } while (0)   /* FX3: FUN_1001d600 */

static void t29_init(E3World *W, E3Obj *o)
{
    if (!OW(o, 0x2a)) OW(o, 0x2a) = 8;
    OU(o, 0x50) = 0x202;
    FX3_SHADOW(o, 0x2017);                                         /* FX3: FUN_10025700 */
    e3_set_anim(W, o, 0);
    OW(o, 0x34) = 0; OS(o, 0x30) = 1; OW(o, 0x4a) = 0xfe; OI(o, 0xcc) = 1;
    OS(o, 0x11e) = 0; OS(o, 0x126) = 0; OS(o, 0x116) = 1;
    OI(o, 0x118) = 0;
}
static E3Obj *link(E3World *W, E3Obj *o, int off) { int k = OI(o, off); return k > 0 && W->obj[k - 1].used ? &W->obj[k - 1] : NULL; }
static void t29_update(E3World *W, E3Obj *o)
{
    if (OS(o, 0x116) == 0) return;
    if (OW(o, 0x2a) == 8) {
        OY(o) = -0x400000; ODEP(o) = ODEP(PL) - 0x1800000;
        OLAT(o) = (rnd(0x100) & 1) ? 0x100000 : -0x100000;
        E3Obj *e = spawn(W, 0x3f, ODEP(o) - 0x400000, OY(o), OLAT(o), OW(o, 0x38));
        OI(o, 0x118) = e ? (int)(e - W->obj) + 1 : 0;
        e3_sound(W, 0x282, NULL);
        OW(o, 0x2a) = 2;
    }
    int16_t r[3]; rel(o, PL, r);
    if (r[0] > 600) OY(o) -= 0x20000;
    if (r[0] >= 0x259) OW(o, 0x2a) = 0x20;
    else if (r[0] >= -0x7f && r[0] > 0x19c) OW(o, 0x2a) = 4;
    if (OW(o, 0x2a) == 4 && OS(o, 0x126)-- < 0) {
        OS(o, 0x11e)++;
        if (OS(o, 0x11e) < 6) {
            E3Obj *g = spawn(W, 0x2a, ODEP(o), OY(o), OLAT(o), OW(o, 0x38));
            if (g) e3_sound(W, 0x2a84, o);
        }
        OS(o, 0x126) = 2;
    }
    int32_t d0 = ODEP(o);
    ODEP(o) += 0x160000;
    E3Obj *e = link(W, o, 0x118);
    if (e) { ODEP(e) = d0 - 0xa0000; OY(e) = OY(o); OLAT(e) = OLAT(o); }
    e3_anim(W, o);
    if (r[0] > 0x400) {
        OS(o, 0x116) = 0;
        if (e) e3_remove(W, e);
        e3_remove(W, o);
    }
}
static void t3f_init(E3World *W, E3Obj *o)      /* FUN_10028a40 */
{
    if (!OW(o, 0x2a)) OW(o, 0x2a) = 2;
    OU(o, 0x50) = 0x202; OU(o, 0x54) = 0x80000005; OW(o, 0x4a) = 0xfe;
    e3_set_anim(W, o, 3);
}
static void t3f_update(E3World *W, E3Obj *o) { e3_anim(W, o); }    /* FUN_10028a80 */

static void t2a_init(E3World *W, E3Obj *o)      /* FUN_10025960 */
{
    if (!OW(o, 0x2a)) OW(o, 0x2a) = 8;
    OS(o, 0x11e) = 0; OW(o, 0x98) = 0;
    OU(o, 0x50) = 0x202; OU(o, 0x54) = 0x80000005; OW(o, 0x4a) = 0xfe;
    OS(o, 0x30) = 1; OS(o, 0x126) = 0x1e; OW(o, 0x3a) = 300;
    OI(o, 0xb8) = 0xa0000; OI(o, 0xc0) = 0x60000; OI(o, 0xc8) = 0x10000;
    OI(o, 0xdc) = OY(o);
    e3_set_anim(W, o, 1);
}
static void t2a_update(E3World *W, E3Obj *o)    /* FUN_100259e0 */
{
    if (OW(o, 0x2a) & 8) OW(o, 0x2a) = 2;
    if (OW(o, 0x2a) & 2) {
        int a = OW(o, 0x11e);
        OS(o, 0x11e) = (int16_t)(a + 0x60);
        uint32_t s = (uint32_t)e3_cos(a - 0x400) & 0x3fffc0;     /* DAT_100407b4[a - 0x400] */
        OY(o) = OI(o, 0xdc) - (int32_t)(s * 0x400);
        if ((int16_t)(a + 0x60) > 1999) OW(o, 0x2a) = 4;
    }
    if (OW(o, 0x2a) & 4) {
        OY(o) += OI(o, 0xb8);
        int up, dn = e3_floor(W, HI(ODEP(o)), HI(OY(o)), HI(OLAT(o)), OW(o, 0x38), &up);
        int land = dn == 0 || (up != 0x7fff && up <= ia(HI(OY(o) - OI(o, 0xa0))) + ia(HI(OLAT(o) - OI(o, 0xa4))) + 2 + ia(HI(ODEP(o) - OI(o, 0x9c))));
        if (land) {                                                  /* FUN_1001ec20 */
            if (up != 0x7fff && dn != 0) OY(o) -= up << 16;
            OI(o, 0xc8) = 0;
            spawn(W, 0x2b, ODEP(o), OY(o), OLAT(o), OW(o, 0x38));
            e3_sound(W, 0xe285, o);
            e3_remove(W, o);
            return;
        }
    }
    if (OW(o, 0x2a) & 6) {
        if (OS(o, 0x98)) OLAT(o) += OI(o, 0xc8); else OLAT(o) -= OI(o, 0xc8);
    }
    e3_anim(W, o);
}
static void t2b_init(E3World *W, E3Obj *o)      /* FUN_10025d20 */
{
    OW(o, 0x38) = OW(PL, 0x38);
    OU(o, 0x50) = 0x202; OU(o, 0x54) = 0x80000005; OW(o, 0x4a) = 0xfe;
    OW(o, 0x2a) = 2; OS(o, 0x30) = 1; OI(o, 0xe0) = 0; OS(o, 0x116) = 0x1e;
    e3_drop(W, o);
    e3_set_anim(W, o, 3);
}
static void t2b_update(E3World *W, E3Obj *o)    /* FUN_10025d80 */
{
    e3_anim(W, o);
    if (!OI(o, 0xe0) && OS(o, 0x32) == 10) { e3_set_anim(W, o, 2); OS(o, 0x32) = 0; OI(o, 0xe0) = 1; }
    if (getenv("E3_BOXDBG")) { int16_t b[6]; if (e3_model_box(W, o, b)) printf("2b box l %d+%d y %d+%d d %d+%d frame %d\n", b[0], b[3], b[1], b[4], b[2], b[5], OW(o, 0x34)); }
    if (e3_overlap(W, o, PL)) hit(W, o, 0x30);
    gone(W, o);
}

/* ---------------------------------------------------------------- 0x2e flock + 0x43 trail */

#define FLOCK 4
static void t2e_init(E3World *W, E3Obj *o)      /* FUN_10025ec0 */
{
    static const int DD[FLOCK] = {0, 1, 2, 3}, DY[FLOCK] = {2, 1, 0, -1}, DL[FLOCK] = {0, 1, 2, 1};
    if (!OW(o, 0x2a)) OW(o, 0x2a) = 8;
    OI(o, 0xdc) = FLOCK; OW(o, 0x34) = 0; OU(o, 0x50) = 2;
    OS(o, 0x116) = 0; OS(o, 0x11e) = 0x180; OS(o, 0x30) = 1; OS(o, 0x98) = 0x40; OS(o, 0x126) = 10;
    OI(o, 0xe0) = 100;
    o->nchild = 0;
    for (int i = 0; i < FLOCK; i++) {
        E3Obj *c = e3_spawn(W, 0x4029, DD[i] * OS(o, 0x11e) * 0x10000 + ODEP(o), DY[i] * OS(o, 0x98) * 0x8000 + OY(o),
                            DL[i] * OS(o, 0x98) * 0x10000 + OLAT(o), OW(o, 0x38));
        if (!c) continue;
        set_prev(c); TFLAG(c) = 1;
        c->parent = (int)(o - W->obj) + 1;
        if (o->nchild < 8) o->child[o->nchild++] = (int)(c - W->obj);
        OU(c, 0x50) = 2; OB(c, 0x97) = 1; OB(c, 0x94) = 0xff; OB(c, 0x96) = 0xff;
        OY(c) -= 0x400000; OU(c, 0x54) = 0x80000004; OW(c, 0x4a) = 0xfe; OW(c, 0x3a) = 0x180;
        OI(c, 0xd8) = 0;
        OU(c, 0x64) = OU(o, 0x64);
        OS(c, 0x32) = (int16_t)(OS(c, 0x32) + rnd(10));
        OW(c, 0x2a) = 8;
        OI(c, 0xdc) = (int32_t)rnd(0x10) + 0x18;
        OI(c, 0xe0) = (int32_t)rnd(0xfff);
        OI(c, 0xac) = (int32_t)rnd(0x80000) + 0x10000;
        OI(c, 0xb0) = (int32_t)rnd(0x4000) + 0x2000; OI(c, 0xa8) = 0;
        OI(c, 0xd0) = (int32_t)rnd(0x4000) + 0x2000; OI(c, 0xcc) = 0;
        OI(c, 0xc8) = 0x60 - (int32_t)rnd(0x20);
        OI(c, 0xb8) = 0x40 - (int32_t)rnd(0x10);
        OI(c, 0xc0) = (int32_t)rnd(0x4000) + 0x2000;
        OI(c, 0xbc) = (int32_t)rnd(0x10000) + 0x20000;
    }
    e3_set_anim(W, o, 0);
}
static void t2e_update(E3World *W, E3Obj *o)    /* FUN_10026340 */
{
    if (OW(o, 0x38) == OW(PL, 0x38)) {
        for (int k = 0; k < o->nchild; k++) {
            E3Obj *c = &W->obj[o->child[k]];
            if (!c->used || c->parent != (int)(o - W->obj) + 1) continue;
            if (k == 0 && OI(o, 0xe0)-- < 0) {
                static const int S[4] = {0xe28e, 57999, 58000, 0xe291};
                OI(o, 0xdc) = (int32_t)rnd(4);
                e3_sound(W, S[OI(o, 0xdc) & 3], c);
                OI(o, 0xe0) = 100;
            }
            spawn(W, 0x43, ODEP(c) + 0x100000, OY(c), OLAT(c), OW(o, 0x38));
            e3_anim(W, c);
            ODEP(c) -= OI(c, 0xac);
            uint32_t ph = (uint32_t)OI(c, 0xe0);
            int sn2 = e3_cos((int)((ph & 0x7ff) * 2));      /* DAT_100407b4 + (ph & 0x7ff) * 4 */
            OY(c) = (sn2 * OI(c, 0xb8) - 0x40000) * 0x10;
            int sn = e3_cos((int)ph - 0x400);
            OI(c, 0xe0) = (int32_t)((ph + 0x20) & 0xfff);
            OLAT(c) = sn * OI(c, 0xc8) * 0x10;
            if (OY(c) < -0x1000000) OB(c, 0x95) = 0;
            if (OY(c) > 0xa0000) OB(c, 0x95) = 0xff;
            int16_t r[3]; rel(c, PL, r);
            if (r[0] < -0x80) ODEP(c) = ODEP(PL) + 0x9000000;
            if (e3_overlap(W, c, PL)) hit(W, c, 0x2f);
        }
        if (e3_attr_at(W, HI(ODEP(PL)), HI(OY(PL)), HI(OLAT(PL)), OW(PL, 0x38)) != -0x7a) return;   /* attribute 0x86 ends the flock */
    }
    e3_remove(W, o);
}
static void t43_init(E3World *W, E3Obj *o)      /* FUN_10025e30 */
{
    OW(o, 0x38) = OW(PL, 0x38);
    OU(o, 0x50) = 0x202; OU(o, 0x54) = 0x80000007; OW(o, 0x4a) = 0xb4;
    OW(o, 0x2a) = 2; OS(o, 0x30) = 1; OI(o, 0xe0) = 0; OS(o, 0x116) = 0x1e;
    e3_set_anim(W, o, 0);
    OW(o, 0x34) = 0x47;
}
static void t43_update(E3World *W, E3Obj *o)    /* FUN_10025e90 */
{
    OS(o, 0x4a) -= 0xf; OS(o, 0x3a) -= 8;
    if (OW(o, 0x4a) < 0x1f) e3_remove(W, o);
}

/* ---------------------------------------------------------------- 0x2f / 0x31 / 0x32 souls */

static void t2f_init(E3World *W, E3Obj *o)      /* FUN_10026550 */
{
    OU(o, 0x54) = 0x80000005;
    OW(o, 0x2a) = 8; OS(o, 0x30) = 1; OS(o, 0x116) = 0x1e;
    e3_drop(W, o);
    e3_set_anim(W, o, 3);
    OS(o, 0x11e) = 2;
    int p = OW(o, 0x38);
    if ((p == 3 || p == 4) && ODEP(o) > 0x74000000) {
        OY(o) += (int32_t)rnd(0x80) - (int32_t)rnd(0x40);
        OW(o, 0x3a) = (uint16_t)(0x100 - rnd(0x20));
    }
}
static void t2f_update(E3World *W, E3Obj *o)    /* FUN_10026600 */
{
    OY(o) += -0x8000 - (int32_t)rnd(0x8000);
    e3_anim(W, o);
    if ((OW(o, 0x2a) & 8) && OY(o) < -0x600000) { e3_sound(W, 57999, o); OW(o, 0x2a) = 2; }
    if (OW(o, 0x2a) & 2) { e3_set_anim(W, o, 4); OS(o, 0x32) = 0; OW(o, 0x2a) = 4; }
    if ((OW(o, 0x2a) & 4) && OS(o, 0x32) > 10) e3_remove(W, o);
}
static void t31_init(E3World *W, E3Obj *o)      /* FUN_10026b80 (and 0x32: FUN_10026d50) */
{
    OW(o, 0x4a) = 0xfe;
    if (OW(o, 0x2e) == 0x31) FX3_SHADOW(o, 0x2017);                /* FX3: FUN_10026b80 (0x32 FUN_10026d50 has none) */
    OI(o, 0xdc) = o->variant;
    OW(o, 0x38) = OW(PL, 0x38);
    OW(o, 0x2a) = 8; OS(o, 0x30) = 1; OS(o, 0x116) = 0x1e;
    e3_drop(W, o);
    e3_set_anim(W, o, 0);
    if (OW(o, 0x2e) == 0x31) {
        OS(o, 0x126) = 1;
        int k = (int)rnd(3);
        OS(o, 0x11e) = (int16_t)k;
        OB(o, 0x1c) = k == 0 ? 0x14 : k == 1 ? 6 : 0x13;
    }
    if (OI(o, 0xdc)) OU(o, 0x54) |= 0x10;
}
static void t31_update(E3World *W, E3Obj *o)    /* FUN_10026c60 / FUN_10026de0 */
{
    int is31 = OW(o, 0x2e) == 0x31;
    int16_t r[3]; rel(o, PL, r);
    if (OW(o, 0x2a) & 8) { ODEP(o) -= is31 ? 0x60000 : 0x20000; if (r[0] < 0x140) OW(o, 0x2a) = 2; }
    if (OW(o, 0x2a) & 2) { if (is31) { e3_set_anim(W, o, 1); OS(o, 0x32) = 0; } OW(o, 0x2a) = 4; }
    if ((OW(o, 0x2a) & 4) && OS(o, 0x32) == 5) { if (is31) { e3_set_anim(W, o, 2); OS(o, 0x32) = 0; } OW(o, 0x2a) = 0x80; }
    if (is31) e3_anim(W, o);
    if (e3_overlap(W, o, PL)) hit(W, o, 4);
    if (!is31) e3_anim(W, o);
    if (is31) e3_drop(W, o);
    gone(W, o);
}

/* ---------------------------------------------------------------- 0x35 coffin + 0x36 / 0x37 lid, 0x3b / 0x41 bones */

static void t35_init(E3World *W, E3Obj *o)      /* FUN_100272e0 */
{
    OI(o, 0xdc) = o->variant;
    OW(o, 0x38) = OW(PL, 0x38);
    OW(o, 0x2a) = 8; OS(o, 0x30) = 1; OS(o, 0x116) = 0; OW(o, 0x3a) = 0x200;
    e3_drop(W, o);
    OY(o) -= 0x400000;
    OW(o, 0x34) = 0xaf;
    if (OI(o, 0xdc)) OU(o, 0x54) |= 0x10;
}
static void t35_update(E3World *W, E3Obj *o)    /* FUN_10027370 */
{
    int16_t r[3]; rel(o, PL, r);
    if ((OW(o, 0x2a) & 8) && r[0] < 0x300) {
        e3_sound(W, 0xe287, o);
        static const int32_t DY[4] = {-0x200000, -0x600000, -0x800000, -0xc40000};
        for (int i = 0; i < 4; i++) {
            int n = i == 3 ? 0x20 : 0x80;
            int32_t l = (int32_t)(rnd(n) * 0x10000) - (int32_t)(rnd(n) * 0x10000) + OLAT(o);
            E3Obj *s = spawn(W, 0x2016, ODEP(o) - 0x400000, OY(o) + DY[i] - (int32_t)rnd(0x200000), l, OW(PL, 0x38));
            if (s) { OW(s, 0x4a) = 0x80; OU(s, 0x54) = 0x80000005; }
        }
        OW(o, 0x2a) = 4; OW(o, 0x4a) = 0xfe;
    }
    if (OW(o, 0x2a) & 4) {
        OS(o, 0x4a) -= 0x20; if (OW(o, 0x4a) < 0x80) OW(o, 0x4a) = 0x80;
        if (OI(o, 0xdc) == 0) {
            OS(o, 0x74) += OS(o, 0x116); if (++OS(o, 0x116) > 0x10) OS(o, 0x116) = 0x10;
            if (OS(o, 0x74) > 0xff) OW(o, 0x2a) = 2;
        } else if (OI(o, 0xdc) == 1) {
            OS(o, 0x74) -= OS(o, 0x116); if (++OS(o, 0x116) > 0x10) OS(o, 0x116) = 0x10;
            if (OS(o, 0x74) < -0xff) OW(o, 0x2a) = 2;
        }
    }
    if (OW(o, 0x2a) & 2) {
        e3_sound(W, 0xe288, o);
        OW(o, 0x34) = 0;
        int fl = OI(o, 0xdc) != 0;
        if (fl) OLAT(o) -= 0x1c40000;
        E3Obj *lid = spawn(W, 0x36, ODEP(o), OY(o) - 0x600000, OLAT(o) + 0xe00000, OW(o, 0x38));
        if (lid && fl) OU(lid, 0x54) |= 0x10;
        if (fl) OLAT(o) += 0x1000000;
        for (int i = 0; i < 4; i++) {
            E3Obj *p = spawn(W, 0x37, ODEP(o), OY(o) - 0x600000, (int32_t)(rnd(0x80) * 0x10000) + OLAT(o), OW(o, 0x38));
            if (p && fl) OU(p, 0x54) |= 0x10;
        }
        for (int i = 0; i < 0xc; i++) {
            E3Obj *b = spawn(W, 0x3b, ODEP(o), OY(o) - (int32_t)rnd(0x400000), (int32_t)(rnd(0x130) * 0x10000) + OLAT(o), OW(o, 0x38));
            if (!b) continue;
            int32_t v = (int32_t)rnd(0x200000) + 0x400000;
            OI(b, 0xbc) = v; OI(b, 0xac) = v >> 2; OI(b, 0xcc) = v >> 4;
            dirs(b, i, fl);
        }
        OW(o, 0x2a) = 0x80;
    }
    gone(W, o);
}
static void lid_init(E3World *W, E3Obj *o, int frame, int anim, int d94)   /* FUN_100279f0 / FUN_10027c10 */
{
    OI(o, 0xdc) = o->variant;
    OU(o, 0x50) = 0x202; OW(o, 0x38) = OW(PL, 0x38); OU(o, 0x54) = 0x80000000;
    OW(o, 0x2a) = 8; OS(o, 0x30) = 1; OS(o, 0x116) = 0x1e;
    e3_drop(W, o);
    if (frame == 0xb1) OW(o, 0x3a) = 0xfa;
    OW(o, 0x34) = (uint16_t)frame; OS(o, 0x74) = 0xc0;
    int32_t v = (int32_t)rnd(0x200000) + 0x200000;
    OI(o, 0xbc) = v; OI(o, 0xb8) = v;
    OI(o, 0xc0) = (int32_t)rnd(0x10000) + 0x10000; OI(o, 0xc4) = (int32_t)rnd(0x80) + 0x100;
    OI(o, 0xac) = v >> 3; OI(o, 0xa8) = v >> 1;
    OI(o, 0xb4) = 0x100 - (int32_t)rnd(0x20); OI(o, 0xb0) = 0x100 - (int32_t)rnd(0x20);
    OI(o, 0xcc) = OI(o, 0xac) >> 2; OI(o, 0xc8) = OI(o, 0xac) >> 2;
    OI(o, 0xd0) = (int32_t)rnd(0x100); OI(o, 0xd4) = (int32_t)rnd(0x100);
    OB(o, 0x95) = 0; OS(o, 0x30) = 1;
    if (d94) OB(o, 0x94) = 0xff;
    e3_set_anim(W, o, anim);
    OW(o, 0x34) = (uint16_t)frame;
}
static void t36_init(E3World *W, E3Obj *o) { lid_init(W, o, 0xb1, 0, 0); }
static void t37_init(E3World *W, E3Obj *o) { lid_init(W, o, 0xb2, 1, 1); }
static void t36_update(E3World *W, E3Obj *o)    /* FUN_10027b50 (0x37: FUN_10027d70, no sparkle, harmless) */
{
    int is36 = OW(o, 0x2e) == 0x36;
    if (!fall(W, o, 0)) {
        OS(o, 0x74) += 0x10;
        if (is36 && OW(o, 0x9a)) sparkle(W, ODEP(o) - 0x400000, OY(o), OLAT(o));
        e3_anim(W, o);
    }
    if (is36 && e3_overlap(W, o, PL)) hit(W, o, 0x33);
    gone(W, o);
}
static void t3b_init(E3World *W, E3Obj *o)      /* FUN_10028240 */
{
    OI(o, 0xdc) = o->variant;
    OU(o, 0x50) = 0x202; OW(o, 0x38) = OW(PL, 0x38); OU(o, 0x54) = 0x80000000;
    OW(o, 0x2a) = 8; OS(o, 0x30) = 1; OS(o, 0x116) = 0x1e; OW(o, 0x3a) = 0x100;
    OW(o, 0x34) = 0xd4; OS(o, 0x74) = 0xc0;
    int32_t v = (int32_t)rnd(0x200000) + 0x200000;
    OI(o, 0xbc) = v; OI(o, 0xb8) = v;
    OI(o, 0xc0) = (int32_t)rnd(0x8000) + 0x8000; OI(o, 0xc4) = (int32_t)rnd(0x80) + 0x100;
    OI(o, 0xac) = v >> 1; OI(o, 0xa8) = v >> 1;
    OI(o, 0xb4) = 0x100 - (int32_t)rnd(0x20); OI(o, 0xb0) = 0x100 - (int32_t)rnd(0x20);
    OI(o, 0xcc) = OI(o, 0xac) >> 2; OI(o, 0xc8) = OI(o, 0xac) >> 2;
    OI(o, 0xd0) = (int32_t)rnd(0x100); OI(o, 0xd4) = (int32_t)rnd(0x100);
    OB(o, 0x95) = 0; OW(o, 0x98) = 1;
    OU(o, 0x64) = 0;            /* no animation unless a caller sets one (FUN_1001cb00 leaves +0x64 = 0) */
}
static void bone_sound(E3World *W, E3Obj *o)
{
    if (OW(o, 0x9a)) {
        if (OS(o, 0x98)) { static const int S[5] = {0x629c, 0x629d, 0x629e, 0x629f, 0x62a0}; OI(o, 0xdc) = (int32_t)rnd(5); e3_sound(W, S[OI(o, 0xdc) % 5], o); }
        OW(o, 0x98) = 0;
    }
}
static void t3b_update(E3World *W, E3Obj *o)    /* FUN_10028390 */
{
    const uint32_t FLY = 0x10039f98u, SPIN = 0x10039fc0u;
    if (OU(o, 0x64) == FLY && OW(o, 0x9a)) { OU(o, 0x64) = SPIN; OS(o, 0x32) = 0; }
    if (OU(o, 0x64) == SPIN && e3_anim(W, o)) { e3_remove(W, o); return; }
    bone_sound(W, o);
    fall(W, o, 0);
    gone(W, o);
}
static void t41_init(E3World *W, E3Obj *o)      /* FUN_10028f20 */
{
    if (!OW(o, 0x2a)) OW(o, 0x2a) = 2;
    OU(o, 0x50) = 2; OU(o, 0x54) = 0x80000000; OW(o, 0x2a) = 2;
    int32_t v = (int32_t)rnd(0x100000) + 0x100000;
    OI(o, 0xbc) = v; OI(o, 0xb8) = v;
    OI(o, 0xc0) = (int32_t)rnd(0x10000) + 0x10000; OI(o, 0xc4) = (int32_t)rnd(0x80) + 0x100;
    OI(o, 0xac) = v >> 1; OI(o, 0xa8) = v >> 1;
    OI(o, 0xb4) = 0x100 - (int32_t)rnd(0x20); OI(o, 0xb0) = 0x100 - (int32_t)rnd(0x20);
    OI(o, 0xcc) = OI(o, 0xac) >> 2; OI(o, 0xc8) = OI(o, 0xac) >> 2;
    OI(o, 0xd0) = (int32_t)rnd(0x100); OI(o, 0xd4) = (int32_t)rnd(0x100);
    OB(o, 0x95) = 0; OW(o, 0x98) = 1; OI(o, 0xd8) = 1;
    OS(o, 0x116) = (int16_t)rnd(0x40);
    OU(o, 0x64) = 0;
}
static void t41_update(E3World *W, E3Obj *o)    /* FUN_10029040 */
{
    if (OI(o, 0xdc) && OU(o, 0x64) == 0x10039f98u) e3_anim(W, o);
    bone_sound(W, o);
    if (fall(W, o, 0)) { OS(o, 0x70) = 0; OS(o, 0x72) = 0; OS(o, 0x74) = 0; }
    gone(W, o);
}

/* ---------------------------------------------------------------- 0x3c soul river (attributes 99 / 0x77) */

static void t3c_init(E3World *W, E3Obj *o)      /* FUN_100284a0 */
{
    OI(o, 0xdc) = o->variant;
    OU(o, 0x50) = 0x202; OW(o, 0x38) = OW(PL, 0x38); OU(o, 0x54) = 0x80000000; OS(o, 0x11e) = 0;
}
static void t3c_update(E3World *W, E3Obj *o)    /* FUN_100284f0: Hercules sinks into the river and dies */
{
    OW(o, 0x34) = 0;
    OI(PL, 0xac) = 0; OI(PL, 0xa8) = 0;
    if (OY(PL) > 0xa9ffff) {
        ODEP(PL) = (ODEP(o) >> 0x17) << 0x17;
        OS(PL, 0x126) = 1;
        if (W->state != 6) {
            for (int i = 0; i < 0x18; i++) {
                int32_t l = (int32_t)(rnd(0x10) - rnd(0x10)) * 0x10000 + OLAT(PL);
                E3Obj *b = spawn(W, 0x3b, (int32_t)(rnd(4) - rnd(4)) * 0x10000 + ODEP(PL), OY(PL) - 0x100000, l, OW(PL, 0x38));
                if (!b) continue;
                int32_t v = (int32_t)rnd(0x400) + 0x100000;
                OI(b, 0xbc) = v; OI(b, 0xb8) = v; OI(b, 0xac) = v >> 2;
                OB(b, 0x22) = 4; OI(b, 0xc4) = 0x18000;
                OU(b, 0x64) = 0x10039f98u; OS(b, 0x32) = 0;
                OI(b, 0xb4) <<= 1; OI(b, 0xcc) = v >> 4;
                OW(b, 0x3a) = (uint16_t)(rnd(0x40) + 0x80); OW(b, 0x4a) = (uint16_t)(rnd(0x40) + 0x80);
                OB(b, 0x95) = 0xff; OB(b, 0x1c) = 0x16; OI(b, 0xdc) = 1;
                OW(b, 0x34) = 0x152;
                dirs(b, i + 1, OI(o, 0xdc) != 0);
            }
            e3_kill(W);                 /* FUN_10019a30 */
        }
    }
}
/* FUN_10017ae0 cases 99 / 0x77: Hercules has dropped below the rafts into the river: the river takes him */
void e3t_attr(E3World *W, int a)
{
    if (W->level != 10) return;
    if (a != 99 && a != 0x77) return;
    int32_t lim = a == 99 ? 0x7bffff : 0x7c0000 - 1;
    if (OY(PL) <= lim) return;
    static int river;
    if (!OI(PL, 0xe0)) {                    /* hero +0xe0 (DAT_10041d54[0x38]): river object made */
        E3Obj *r = spawn(W, 0x3c, ODEP(PL), OY(PL) - 0x500000, OLAT(PL), OW(PL, 0x38));
        river = r ? (int)(r - W->obj) + 1 : 0;
        OI(PL, 0xe0) = 1;
    }
    E3Obj *r = river > 0 ? &W->obj[river - 1] : NULL;
    if (r && r->used && OW(r, 0x2e) == 0x3c) t3c_update(W, r);
}

/* ---------------------------------------------------------------- 0x3d / 0x3e skeleton arm */

static void t3d_init(E3World *W, E3Obj *o)      /* FUN_100287b0 */
{
    OI(o, 0xdc) = o->variant;
    OU(o, 0x50) = 0x202; OW(o, 0x38) = OW(PL, 0x38);
    OW(o, 0x3a) = 0x100; OW(o, 0x4a) = 0x80; OU(o, 0x54) = 0x80000600;
    OW(o, 0x2a) = 8; OS(o, 0x30) = 1; OS(o, 0x116) = 0x1e;
    e3_drop(W, o);
    e3_set_anim(W, o, 1);
    if (OI(o, 0xdc)) OU(o, 0x54) |= 0x10;
}
static void t3d_update(E3World *W, E3Obj *o)    /* FUN_10028840 */
{
    e3_anim(W, o);
    OW(o, 0x4a) = 0x80;
    int edge = OS(o, 0x32) == 9 && OS(o, 0x126) != 9;
    OS(o, 0x126) = OS(o, 0x32);
    if (edge) {
        E3Obj *a;
        if (!OI(o, 0xdc)) a = spawn(W, 0x3e, ODEP(o), OY(o) - 0x200000, OLAT(o) - 0x400000, OW(o, 0x38));
        else {
            a = spawn(W, 0x3e, ODEP(o), OY(o) - 0x200000, OLAT(o) + 0x400000, OW(o, 0x38));
            if (a) { OS(a, 0x74) += 0x200; OU(a, 0x54) &= ~0x10u; }
        }
        OI(o, 0x118) = a ? (int)(a - W->obj) + 1 : -1;
    }
    if (e3_overlap(W, o, PL)) hit(W, o, 2);
    gone(W, o);
}
static void t3e_init(E3World *W, E3Obj *o)      /* FUN_10028960 */
{
    if (!OW(o, 0x2a)) OW(o, 0x2a) = 2;
    OU(o, 0x50) = 0x202; OU(o, 0x54) = 0x80000005; OS(o, 0x74) = (int16_t)0xff00;
    e3_set_anim(W, o, 2);
}
static void t3e_update(E3World *W, E3Obj *o)    /* FUN_100289a0 */
{
    e3_anim(W, o);
    int16_t r[3]; rel(o, PL, r);
    if (r[2] > -0x40 && r[2] < 0x40 && r[0] > -0x30 && r[0] < 0x30 && r[1] != 0x1e && r[1] < 0x1e) hit(W, o, 0x2e);
    if (OS(o, 0x32) == 7) e3_remove(W, o);
}

/* ---------------------------------------------------------------- 0x40 skull pile */

static void t40_init(E3World *W, E3Obj *o)      /* FUN_10028a90 */
{
    if (!OW(o, 0x2a)) OW(o, 0x2a) = 8;
    OU(o, 0x50) = 0x202; OU(o, 0x54) = 0x90000640;
    OB(o, 0x22) = 0; OW(o, 0x34) = 6; OW(o, 0x4a) = 0x80;
    e3_drop(W, o);
    OY(o) += 0xc0000;
}
static void t40_update(E3World *W, E3Obj *o)    /* FUN_10028ae0 */
{
    if (OW(o, 0x38) != OW(PL, 0x38)) { e3_remove(W, o); return; }
    int16_t r[3]; rel(o, PL, r);
    if ((OW(o, 0x2a) & 8) && r[2] > -0x3c && r[2] < 0x3c && r[0] > -0x10 && r[0] < 0x10 && r[1] != 0x1e && r[1] < 0x1e) {
        sparkle(W, ODEP(o), OY(o), OLAT(o));
        OW(o, 0x2a) = 2; OW(o, 0x34) = 0;
        hit(W, o, 0x32);
    }
    if (OW(o, 0x2a) & 2) {
        e3_sound(W, 0xe286, o);
        for (int i = 0; i < 8; i++) {
            E3Obj *p = spawn(W, 0x41, ODEP(o), OY(o) - (int32_t)rnd(0x40) * 0x10000, OLAT(o), OW(o, 0x38));
            if (!p) continue;
            OB(p, 0x22) = 4; OW(p, 0x34) = (uint16_t)(0x136 + i);
            int32_t v = (int32_t)rnd(0x200000) + 0x300000;
            OI(p, 0xbc) = v; OW(p, 0x3a) = 0x80; OI(p, 0xac) = v >> 2; OI(p, 0xcc) = v >> 2;
            dirs(p, i, 0);
        }
        static const int FR[4] = {0x138, 0x13b, 0x13d, 0x13c};
        static const uint8_t D96[4] = {0, 0, 0xff, 0xff};
        for (int i = 0; i < 0xc; i++) {
            int32_t l = (int32_t)(rnd(0x40) * 0x10000) - (int32_t)(rnd(0x40) * 0x10000) + OLAT(o);
            E3Obj *b = spawn(W, 0x3b, ODEP(o), OY(o) - (int32_t)rnd(0x60) * 0x10000, l, OW(o, 0x38));
            if (!b) continue;
            int32_t v = ((int32_t)rnd(1) + 0x40) * 0x10000;
            OI(b, 0xbc) = v; OI(b, 0xac) = v >> 1; OI(b, 0xcc) = v >> 3;
            OW(b, 0x4a) = (uint16_t)(0x80 - rnd(0x40)); OB(b, 0x95) = 0; OW(b, 0x3a) = (uint16_t)(0x100 - rnd(100));
            OW(b, 0x34) = (uint16_t)FR[i & 3]; OB(b, 0x94) = 0; OB(b, 0x96) = D96[i & 3];
            if (OI(o, 0xdc)) OB(b, 0x96) = 0xff;
        }
        OW(o, 0x2a) = 4;
    }
    gone(W, o);
}

static void d4004_init(E3World *W, E3Obj *o);

/* ---------------------------------------------------------------- 0x45 rolling wheels, 0x46 rock fall */

static void kids_add(E3World *W, E3Obj *o, E3Obj *c, int *list, int *n, int max)
{
    (void)W;
    if (*n >= max) return;
    c->parent = (int)(o - W->obj) + 1;
    list[(*n)++] = (int)(c - W->obj);
    set_prev(c); TFLAG(c) = 1;
}
/* parents with more than 8 children (FUN_1001f3c0 lists): kept here by object index */
static struct { int list[20]; int n; } KIDS[E3_MAXOBJ];

static void t45_init(E3World *W, E3Obj *o)      /* FUN_10029140 */
{
    OI(o, 0xdc) = o->variant;
    if (!OW(o, 0x2a)) OW(o, 0x2a) = 8;
    OW(o, 0x34) = 0; OU(o, 0x50) = 2; OU(o, 0x54) |= 0x10000640;
    OS(o, 0x116) = 8; OS(o, 0x11e) = 0x20; OS(o, 0x30) = 0x1e; OW(o, 0x98) = 1;
    int i = (int)(o - W->obj);
    KIDS[i].n = 0;
    for (int32_t dd = 0; dd < 0x200000; dd += 0x200000) {
        E3Obj *c = e3_spawn(W, 0x402a, ODEP(o) - dd, OY(o), OLAT(o), OW(o, 0x38));
        if (!c) continue;
        kids_add(W, o, c, KIDS[i].list, &KIDS[i].n, 20);
        OB(c, 0x22) = 0; OW(c, 0x34) = 7; OU(c, 0x50) = 2; OU(c, 0x54) |= 0x10000040;
        OI(c, 0xd8) = (int32_t)rnd(0x14) + 0x14; OI(c, 0xdc) = 0;
        OS(c, 0x30) = 0x1e; OW(c, 0x2a) = 2; OB(c, 0x94) = 0xff; OB(c, 0x96) = 0;
        OW(c, 0x3a) = 0x200; OW(c, 0x4a) = 0xfe;
        OI(c, 0xbc) = 0x40000; OI(c, 0xac) = (int32_t)rnd(0x40000) + 0x60000; OI(c, 0xcc) = (int32_t)rnd(0x40000) + 0x40000;
        OI(c, 0xb0) = (int32_t)rnd(0x4000) + 0x2000; OI(c, 0xd0) = (int32_t)rnd(0x4000) + 0x2000;
        OI(c, 0xa8) = 0; OI(c, 0xc8) = 0;
    }
}
extern int e3_attr_at(E3World *W, int d, int y, int l, int path);
static void t45_update(E3World *W, E3Obj *o)    /* FUN_100292f0 */
{
    int i = (int)(o - W->obj);
    for (int k = 0; k < KIDS[i].n; k++) {
        E3Obj *c = &W->obj[KIDS[i].list[k]];
        if (!c->used || c->parent != i + 1) continue;
        if (OS(o, 0x98)) { e3_sound(W, 0x2aa3, c); OW(o, 0x98) = 0; }
        OS(c, 0x70) += OS(c, 0x30);
        if (OW(c, 0x2a) == 0x20) { if (OI(c, 0xd8)-- < 0) OW(c, 0x2a) = 2; }
        else {
            if (OW(c, 0x2a) & 2) { OI(c, 0xdc) -= 8; if ((uint32_t)OI(c, 0xdc) <= (uint32_t)((int32_t)rnd(0x10) - 0x20)) OW(c, 0x2a) = 4; }
            if ((OW(c, 0x2a) & 4) && (OI(c, 0xdc) += 8) > -1) { OI(c, 0xd8) = (int32_t)rnd(0xa0); OW(c, 0x2a) = 0x20; OI(c, 0xdc) = 0; }
        }
        int l = HI(OLAT(c)), pl = OLAT(c) < 1 ? l - OS(o, 0x11e) : l + OS(o, 0x11e);
        int a = e3_attr_at(W, HI(ODEP(c)), HI(OY(c)), pl, OW(o, 0x38));
        if (a == 0x5e) OB(c, 0x96) = 0;
        if (a == 0x5d) OB(c, 0x96) = 0xff;
        int16_t r[3], b[6]; rel(c, PL, r);
        if (e3_model_box(W, c, b) && -(b[3] >> 1) < r[2] && r[2] < (b[3] >> 1) && -(b[5] >> 1) < r[0] && r[0] < (b[5] >> 1) &&
            -b[4] < r[1] && r[1] < b[4]) {
            hit(W, o, 0x26);
            if (W->boost) {                 /* DAT_1003fb20: Hercules on the speed power-up smashes the wheel */
                sparkle(W, ODEP(o) - 0x400000, OY(o), OLAT(o));
                e3_sound(W, 0xe2a8, c);
                for (int j = 0; j < 0x10; j++) {
                    int32_t ll = ((int32_t)rnd(0x100) - (int32_t)rnd(0x100)) * 0x10000 + OLAT(PL);
                    E3Obj *dbr = e3_spawn(W, 0x4004, ODEP(PL) + 0x800000, OY(PL) - (int32_t)rnd(0x80) * 0x10000, ll, OW(PL, 0x38));
                    if (!dbr) continue;
                    set_prev(dbr); d4004_init(W, dbr); TFLAG(dbr) = 1;
                    int32_t vy = ((int32_t)rnd(4) + 0x10) * 0x10000, vd = ((int32_t)rnd(8) + 0x1e) * 0x10000;
                    OI(dbr, 0xbc) = vy; OI(dbr, 0xb8) = vy; OI(dbr, 0xc4) = 0x28000;
                    OI(dbr, 0xac) = vd; OI(dbr, 0xa8) = vd; OI(dbr, 0xb0) = 0; OI(dbr, 0xb4) = 0x100;
                    OI(dbr, 0xd0) = (int32_t)rnd(0x20); OI(dbr, 0xcc) = 0x1e0000;
                    OW(dbr, 0x34) = (uint16_t)((j & 3) == 0 ? 0xb : 0xc);
                    OB(dbr, 0x94) = 0; OB(dbr, 0x96) = (j & 3) >= 2 ? 0xff : 0;
                }
                for (int j = 0; j < KIDS[i].n; j++) if (W->obj[KIDS[i].list[j]].used) e3_remove(W, &W->obj[KIDS[i].list[j]]);
                KIDS[i].n = 0;
                e3_remove(W, o);
                return;
            }
        }
        e3_drop(W, c);
        OY(c) -= 0x360000;
        e3_move_lat(c);
        e3_move(c);
        if (ODEP(c) < ODEP(PL) - 0xc40000) { e3_remove(W, c); }
    }
    if (e3_offscreen(W, o, 0x100)) {
        for (int j = 0; j < KIDS[i].n; j++) if (W->obj[KIDS[i].list[j]].used) e3_remove(W, &W->obj[KIDS[i].list[j]]);
        KIDS[i].n = 0;
        e3_remove(W, o);
    }
}

static void t46_init(E3World *W, E3Obj *o)      /* FUN_100297d0 */
{
    static const int SC[20] = {0x40, 0x40, 0x80, 0xc0, 0x100, 0x140, 0x100, 0xc0, 0x80, 0x40,
                               0x40, 0x40, 0x80, 0xc0, 0x100, 0x140, 0x100, 0xc0, 0x80, 0x40};
    OI(o, 0xdc) = 0x14; OI(o, 0xe0) = 0x20000; OW(o, 0x98) = 1; OS(o, 0x11e) = 100;
    int i = (int)(o - W->obj);
    KIDS[i].n = 0;
    for (int k = 0; k < OI(o, 0xdc); k++) {
        int32_t l = (int32_t)(rnd(0x100) * 0x10000) - (int32_t)(rnd(0x100) * 0x10000) + OLAT(o);
        E3Obj *c = e3_spawn(W, 0x402b, ODEP(o) - k * 0x800000, (int32_t)0xfe800000, l, OW(o, 0x38));
        if (!c) continue;
        kids_add(W, o, c, KIDS[i].list, &KIDS[i].n, 20);
        OB(c, 0x22) = 0; OW(c, 0x34) = 7; OU(c, 0x50) = 2; OU(c, 0x54) |= 0x10000040;
        OI(c, 0xd8) = 1; OI(c, 0xdc) = 0; OS(c, 0x30) = 0x1e; OW(c, 0x98) = 1; OI(c, 0xe0) = 0x20;
        OW(c, 0x3a) = (uint16_t)SC[k];
        OW(c, 0x2a) = 8; OB(c, 0x94) = 0xff; OB(c, 0x96) = 0;
        OI(c, 0xbc) = 0x1e0000; OI(c, 0xac) = 0; OI(c, 0xcc) = 0xf0000; OI(c, 0xb8) = 0; OI(c, 0xc0) = 0x20000; OI(c, 0xc4) = 0xffff;
    }
}
static void t46_update(E3World *W, E3Obj *o)    /* FUN_10029a10 */
{
    int i = (int)(o - W->obj);
    if (W->state == 6) { W->shake = 0; }
    else if (OS(o, 0x11e)-- > 0) {
        W->shake = 1;       /* DAT_100405fc camera rumble, three steps per tick */
        for (int k = 0; k < 3; k++) { OI(o, 0xe0) += 0x20000; if (OI(o, 0xe0) > 0x80000) OI(o, 0xe0) = (int32_t)0xfff80000; }
    }
    for (int k = 0; k < KIDS[i].n; k++) {
        E3Obj *c = &W->obj[KIDS[i].list[k]];
        if (!c->used || c->parent != i + 1) continue;
        if (OW(o, 0x98)) { e3_sound(W, 0x2a7, NULL); OW(o, 0x98) = 0; }
        int16_t r[3]; rel(c, PL, r);
        if (OW(c, 0x2a) & 8) {
            if (OI(c, 0xd8) == 1) {
                for (int j = 0; j < 3; j++) {
                    E3Obj *d = e3_spawn(W, 0x4002, ODEP(c), OY(c), OLAT(c), OW(c, 0x38));
                    if (!d) continue;
                    set_prev(d); TFLAG(d) = 0;
                    OB(d, 0x94) = (j & 1) ? 0 : 0xff; OB(d, 0x96) = (j & 2) ? 0xff : 0;
                    OI(d, 0xbc) = 0x1e0000; OI(d, 0xac) = 0; OI(d, 0xcc) = 0; OI(d, 0xb8) = 0; OI(d, 0xc0) = 0x20000; OI(d, 0xc4) = 0xffff;
                    OB(d, 0x13e) = 1;       /* keep these speeds (spawned with an explicit kick) */
                }
                OI(c, 0xd8) = 0;
            }
            if (r[0] < 700) OW(c, 0x2a) = 4;
        }
        if (OW(c, 0x2a) & 4) {
            OS(c, 0x72) += (int16_t)(OI(c, 0xe0) * 2);
            int16_t sv = (int16_t)(OI(c, 0xe0) >> 1);
            OS(c, 0x70) += sv; OS(c, 0x74) += sv;
            if (OW(c, 0x9a)) {
                OI(c, 0xe0) >>= 1;
                if (OS(c, 0x98) == 1) {
                    if (OW(c, 0x3a) == 0x140) e3_sound(W, 0xe2a4, c);
                    if (OW(c, 0x3a) == 0x100) e3_sound(W, 0xe2a5, c);
                    if (OW(c, 0x3a) == 0xc0) e3_sound(W, 0xe2a6, c);
                    if (OW(c, 0x3a) > 0x80) sparkle(W, ODEP(c) - 0x800000, OY(c), OLAT(c));
                    for (int j = 0; j < 2; j++) {
                        E3Obj *d = e3_spawn(W, 0x4002, ODEP(c), (int32_t)rnd(0x200000) + OY(c), (int32_t)rnd(0x200000) + OLAT(c), OW(c, 0x38));
                        if (!d) continue;
                        set_prev(d); TFLAG(d) = 0;
                        OB(d, 0x94) = (j & 1) ? 0 : 0xff; OB(d, 0x96) = 0;
                    }
                    OW(c, 0x98) = 0;
                }
            }
            if (fall(W, c, 1)) OI(c, 0xe0) = 0;
            int16_t b[6];
            if (e3_model_box(W, c, b) && -(b[3] >> 1) < r[2] && r[2] < (b[3] >> 1) && -(b[5] >> 1) < r[0] && r[0] < (b[5] >> 1) &&
                -b[4] < r[1] && r[1] < b[4]) hit(W, o, 0x34);
        }
    }
    if (e3_offscreen(W, o, 0x100)) {
        for (int j = 0; j < KIDS[i].n; j++) if (W->obj[KIDS[i].list[j]].used) e3_remove(W, &W->obj[KIDS[i].list[j]]);
        KIDS[i].n = 0;
        e3_remove(W, o);
    }
}

/* ---------------------------------------------------------------- class 0x2000 / 0x4000 effects */

static void s2016_init(E3World *W, E3Obj *o)    /* FUN_10014b30 */
{
    OB(o, 0x22) = 4; OW(o, 0x34) = 0xb4;
    OU(o, 0x54) = 0x80000007; OU(o, 0x50) = 2; OW(o, 0x3a) = 0x180; OS(o, 0x30) = 1; OW(o, 0x4a) = 200;
    OW(o, 0x38) = OW(PL, 0x38);
}
static void s2016_update(E3World *W, E3Obj *o)  /* FUN_10014bb0 */
{
    if (OS(o, 0x30)-- > 0) {
        if (OW(o, 0x34) > 0xc0) { e3_remove(W, o); return; }
        OW(o, 0x34)++; OS(o, 0x30) = 1;
    }
}
static void d4002_init(E3World *W, E3Obj *o)    /* FUN_10014d90 (Torment) */
{
    OU(o, 0x54) |= 0x10000040; OI(o, 0xd8) = 0; OW(o, 0x98) = 0; OU(o, 0x50) = 2; OW(o, 0x38) = OW(PL, 0x38);
    if (!OB(o, 0x13e)) {
        int32_t v = (int32_t)rnd(0x200000) + 0x200000;
        OI(o, 0xbc) = v; OI(o, 0xb8) = v;
        OI(o, 0xc0) = (int32_t)rnd(0x10000) + 0x10000; OI(o, 0xc4) = (int32_t)rnd(0x80) + 0x100;
        OI(o, 0xac) = v >> 1; OI(o, 0xa8) = v >> 1;
        OI(o, 0xb4) = 0x100 - (int32_t)rnd(0x20); OI(o, 0xb0) = 0x100 - (int32_t)rnd(0x20);
        OI(o, 0xcc) = OI(o, 0xac) >> 2; OI(o, 0xc8) = OI(o, 0xac) >> 2;
        OI(o, 0xd0) = (int32_t)rnd(0x100); OI(o, 0xd4) = (int32_t)rnd(0x100);
    }
    OB(o, 0x95) = 0; OS(o, 0x30) = 1;
    OW(o, 0x3a) = (uint16_t)(600 - rnd(400));
    OB(o, 0x22) = 0; OW(o, 0x34) = 4;
}
static void d4002_update(E3World *W, E3Obj *o)  /* FUN_10014f00 */
{
    if (!fall(W, o, 1)) { OS(o, 0x70) += (int16_t)rnd(0x40); OS(o, 0x72) += (int16_t)rnd(0x40); OS(o, 0x74) += (int16_t)rnd(0x40); }
    else { OS(o, 0x70) = 0; OS(o, 0x72) = 0; OS(o, 0x74) = 0; }
    if (OW(o, 0x9a)) { if (OI(o, 0xe0)) { e3_remove(W, o); return; } if (OW(o, 0x3a) > 0x60) OW(o, 0x3a) >>= 1; }
    if (ODEP(o) < ODEP(PL) - 0x800000) e3_remove(W, o);
}
static void d4004_init(E3World *W, E3Obj *o)    /* FUN_10015170 */
{
    int fr = OW(o, 0x34);
    OU(o, 0x54) = 0x90000040; OI(o, 0xd8) = 0; OW(o, 0x98) = 0; OU(o, 0x50) = 2; OW(o, 0x38) = OW(PL, 0x38);
    int32_t v = (int32_t)rnd(0x200000) + 0x200000;
    OI(o, 0xbc) = v; OI(o, 0xb8) = v;
    OI(o, 0xc0) = (int32_t)rnd(0x10000) + 0x10000; OI(o, 0xc4) = (int32_t)rnd(0x80) + 0x100;
    OI(o, 0xac) = v >> 1; OI(o, 0xa8) = v >> 1;
    OI(o, 0xb4) = 0x100 - (int32_t)rnd(0x20); OI(o, 0xcc) = OI(o, 0xac); OI(o, 0xb0) = 0x100 - (int32_t)rnd(0x20);
    OI(o, 0xc8) = OI(o, 0xac) >> 1;
    OI(o, 0xd0) = (int32_t)rnd(0x100); OI(o, 0xd4) = (int32_t)rnd(0x100);
    OB(o, 0x95) = 0xff; OS(o, 0x30) = 1; OW(o, 0x3a) = (uint16_t)(600 - rnd(400));
    OI(o, 0xdc) = (int32_t)rnd(0x20) + 0x10;
    OB(o, 0x22) = 0; OW(o, 0x34) = (uint16_t)(fr ? fr : 0x25);
}
static void d4004_update(E3World *W, E3Obj *o)  /* FUN_100152c0 */
{
    if (!fall(W, o, 1)) { int16_t s = (int16_t)OI(o, 0xdc); OS(o, 0x72) += s; OS(o, 0x74) += s; OS(o, 0x70) += s; }
    else { OS(o, 0x72) >>= 1; OS(o, 0x74) >>= 1; OS(o, 0x70) >>= 1; OI(o, 0xd8) = 0; }
    if (OW(o, 0x9a) && OW(o, 0x3a) > 0x60) OW(o, 0x3a) >>= 1;
    if (ODEP(o) < ODEP(PL) - 0x200000) e3_remove(W, o);
}

/* ---------------------------------------------------------------- dispatch */

typedef struct { int type; void (*init)(E3World *, E3Obj *); void (*update)(E3World *, E3Obj *); } TFn;
static const TFn TT[] = {
    {0x29, t29_init, t29_update}, {0x2a, t2a_init, t2a_update}, {0x2b, t2b_init, t2b_update},
    {0x2e, t2e_init, t2e_update}, {0x2f, t2f_init, t2f_update}, {0x31, t31_init, t31_update},
    {0x32, t31_init, t31_update}, {0x35, t35_init, t35_update}, {0x36, t36_init, t36_update},
    {0x37, t37_init, t36_update}, {0x3b, t3b_init, t3b_update}, {0x3c, t3c_init, NULL},
    {0x3d, t3d_init, t3d_update}, {0x3e, t3e_init, t3e_update}, {0x3f, t3f_init, t3f_update},
    {0x40, t40_init, t40_update}, {0x41, t41_init, t41_update}, {0x43, t43_init, t43_update},
    {0x45, t45_init, t45_update}, {0x46, t46_init, t46_update},
    {0x2016, s2016_init, s2016_update}, {0x4002, d4002_init, d4002_update}, {0x4004, d4004_init, d4004_update},
};
static const TFn *tfind(E3World *W, int t)
{
    if (W->level != 10) return NULL;
    for (size_t i = 0; i < sizeof TT / sizeof TT[0]; i++) if (TT[i].type == t) return &TT[i];
    return NULL;
}
int e3t_type_init(E3World *W, E3Obj *o)
{
    const TFn *f = tfind(W, OW(o, 0x2e));
    if (!f) return 0;
    f->init(W, o);
    TFLAG(o) = 1;
    return 1;
}
int e3t_type_update(E3World *W, E3Obj *o)
{
    int t = OW(o, 0x2e);
    if (W->level != 10) return 0;
    if (t == 0x4029 || t == 0x402a || t == 0x402b) {       /* driven by their parent */
        E3Obj *p = o->parent > 0 ? &W->obj[o->parent - 1] : NULL;
        if (!p || !p->used) e3_remove(W, o);
        return 1;
    }
    const TFn *f = tfind(W, t);
    if (!f) return 0;
    if (!TFLAG(o)) { f->init(W, o); TFLAG(o) = 1; }
    if (f->update) f->update(W, o);
    return 1;
}
