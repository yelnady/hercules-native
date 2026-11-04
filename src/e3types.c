/* ENG3GAME: per-type object handlers for the ENG3 levels, ported from the handler tables of the per-level
 * object entries (ENG3 0x1003b900 Gauntlet, 0x1003b948 Cyclops, 0x1003b990 Torment: +0x1c = 12-byte
 * {init, update, update2} per actor type). Field offsets follow the original object (see e3world.h). */
#include "e3world.h"
#include "eng3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HI(v) ((int16_t)((uint32_t)(v) >> 16))
#define PL (&W->obj[0])

/* hit kinds the type deals on contact (FUN_10019200 argument), from each handler; 0 = harmless */
typedef struct { int level, type, kind; } HurtKind;
static const HurtKind HURT[] = {
    /* Gauntlet */
    {2, 0x29, 0x21}, {2, 0x2a, 0x800}, {2, 0x2b, 0x26}, {2, 0x2c, 0x22}, {2, 0x2d, 0x22}, {2, 0x2e, 0x2b},
    {2, 0x2f, 0x28}, {2, 0x30, 0x28}, {2, 0x31, 2}, {2, 0x32, 0x2a}, {2, 0x33, 0x2a}, {2, 0x35, 0x800},
    {2, 0x37, 0x2c}, {2, 0x3c, 0x40}, {2, 0x3e, 0x25}, {2, 0x40, 0x23}, {2, 0x44, 0x22}, {2, 0x48, 0x2d},
    {2, 0x49, 0x35}, {2, 0x4c, 0x40},
    /* Cyclops */
    {8, 0x2a, 0x200}, {8, 0x2b, 2}, {8, 0x2d, 0x400}, {8, 0x2e, 2}, {8, 0x2f, 0x400}, {8, 0x30, 2},
    {8, 0x3d, 0x800}, {8, 0x3e, 4}, {8, 0x42, 4}, {8, 0x43, 0x200}, {8, 0x48, 0x36},
    /* Torment */
    {10, 0x2b, 0x30}, {10, 0x30, 0x31}, {10, 0x31, 4}, {10, 0x32, 4}, {10, 0x33, 0x2f}, {10, 0x36, 0x33},
    {10, 0x3d, 2}, {10, 0x3e, 0x2e}, {10, 0x40, 0x32}, {10, 0x45, 0x26}, {10, 0x46, 0x34}, {10, 0x48, 0x35},
};

static int hurt_kind(E3World *W, int type)
{
    for (size_t i = 0; i < sizeof HURT / sizeof HURT[0]; i++)
        if (HURT[i].level == W->level && HURT[i].type == type) return HURT[i].kind;
    return 0;
}


/* ---- shared helpers ---- */

static void rel_pos(E3Obj *a, E3Obj *b, int16_t r[3])     /* FUN_10001420: a - b {depth, y, lateral} */
{
    r[0] = HI(ODEP(a) - ODEP(b)); r[1] = HI(OY(a) - OY(b)); r[2] = HI(OLAT(a) - OLAT(b));
}

static void hit_player(E3World *W, E3Obj *by, int kind)
{
    OI(PL, 0x110) = (int32_t)(by - W->obj) + 1;
    OS(PL, 0x116) = (int16_t)kind;
    if (W->state != 5) e3_hurt(W, kind, by);
}

static E3Obj *child(E3World *W, E3Obj *o, int k) { return k < o->nchild && W->obj[o->child[k]].used ? &W->obj[o->child[k]] : NULL; }

static int offscreen_remove(E3World *W, E3Obj *o)
{
    if (!e3_offscreen(W, o, 0x100)) return 0;
    e3_remove(W, o);
    return 1;
}

/* ---- Gauntlet 0x2e: spinning log turnstile (FUN_1000f460 / FUN_1000f7b0), children 0x402d model 0x17 ---- */

static void g2e_init(E3World *W, E3Obj *o)
{
    OW(o, 0x98) = 1;
    if (!OW(o, 0x2a)) OW(o, 0x2a) = 8;
    OW(o, 0x34) = 0;
    OU(o, 0x50) = 2; OU(o, 0x54) |= 0x10000040;
    OS(o, 0x116) = 0;
    e3_drop(W, o);
    OY(o) -= 0x140000;
    static const int OFF[4][3] = {{0, 0, 0}, {0, -0x500000, 0}, {0, 0, -0x500000}, {0, -0x500000, -0x500000}};
    static const uint8_t F94[4] = {0, 0, 0, 0xff}, F96[4] = {0xff, 0, 0xff, 0xff};
    for (int k = 0; k < 4; k++) {
        E3Obj *c = e3_child(W, o, ODEP(o) + OFF[k][0], OY(o) + OFF[k][1], OLAT(o) + OFF[k][2], 0x402d);
        if (!c) continue;
        OU(c, 0x54) |= 0x10000040; OW(c, 0x34) = 0x17;
        OB(c, 0x94) = F94[k]; OB(c, 0x96) = F96[k];
        OI(c, 0xdc) = OLAT(c) < 0 ? -0x20 : 0x20;
    }
}

static void g2e_update(E3World *W, E3Obj *o)
{
    for (int k = 0; k < o->nchild; k++) {
        E3Obj *c = child(W, o, k);
        if (!c) continue;
        if (k == 0 && OI(c, 0xdc) == 0x20 && OW(o, 0x98)) { e3_sound(W, 0x28a5, c); OW(o, 0x98) = 0; }
        int16_t r[3]; rel_pos(c, PL, r);
        if (OW(o, 0x2a) & 2) {
            if (OI(c, 0xdc) > 0) { if (--OI(c, 0xdc) < 0) OI(c, 0xdc) = 0; OS(c, 0x72) += (int16_t)OI(c, 0xdc); }
            if (OI(c, 0xdc) < 0) { if (++OI(c, 0xdc) > 0) OI(c, 0xdc) = 0; OS(c, 0x72) += (int16_t)OI(c, 0xdc); }
            if (r[2] > -0x40 && r[2] < 0x40 && r[0] > -0x40 && r[0] < 0x40) hit_player(W, o, 0x2b);
        }
        if (OW(o, 0x2a) & 8) {
            OS(c, 0x72) += (int16_t)OI(c, 0xdc);
            if (OW(o, 0x38) == OW(PL, 0x38) && r[2] > -0x36 && r[2] < 0x36 && r[0] > -0x40 && r[0] < 0x40) hit_player(W, o, 0x2b);
        }
    }
    offscreen_remove(W, o);
}

/* ---- Gauntlet 0x2f: battering ram pushed in from the side (FUN_1000f980 / FUN_1000fab0), model 9 ---- */

static void g2f_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) = 2; OU(o, 0x54) |= 0x10000040;
    OB(o, 0x22) = 0; OW(o, 0x34) = 9;
    OI(o, 0xdc) = o->variant;
    OW(o, 0x2a) = 2;
    OI(o, 0xe0) = 1;
    OW(o, 0x98) = 0;
    for (int k = 0; k < 2; k++) {
        E3Obj *c = e3_child(W, o, ODEP(o), OY(o), OLAT(o) - (k ? 0x1c00000 : 0xc00000), 0x402f);
        if (c) { OB(c, 0x22) = 0; OU(c, 0x54) |= 0x10000040; OS(c, 0x74) = 0x100; OW(c, 0x3a) = 0x200; OW(c, 0x4a) = 0x76; OW(c, 0x34) = 0x11; }
    }
}

static void g2f_update(E3World *W, E3Obj *o)
{
    OW(o, 0x3a) = 0x2a0; OW(o, 0x34) = 9;
    int16_t r[3]; rel_pos(o, PL, r);
    int32_t home = o->spath >= 0 ? W->sp[o->spath][o->sidx].l : OLAT(o);
    OW(o, 0x98) = 1;
    if (home + 0xac0000 <= OLAT(o)) { OW(o, 0x2a) = 4; OI(o, 0xe0) = 0; }
    if (OLAT(o) <= home + 0x200000) {
        OW(o, 0x2a) = 2; OI(o, 0xe0) = 1;
        if (r[0] < 0) { OW(o, 0x2a) = 8; OI(o, 0xd0) = OI(o, 0xcc) = 0; }
    }
    if (OW(o, 0x2a) & 2) {          /* shoot out */
        OI(o, 0xd0) = OI(o, 0xdc) ? 0x48000 : 0x24000; OI(o, 0xcc) = OI(o, 0xdc) ? 0x480000 : 0x240000;
        OS(o, 0x70) = (int16_t)((HI(home) - HI(OLAT(o))) * 2);
    }
    if (OW(o, 0x2a) & 4) {          /* pull back */
        OI(o, 0xd0) = OI(o, 0xdc) ? -0x4000 : -0x2000; OI(o, 0xcc) = OI(o, 0xdc) ? -0x80000 : -0x40000;
        OS(o, 0x70) = (int16_t)((HI(home) - HI(OLAT(o))) * 2);
    }
    /* FUN_1001f620 with a signed speed */
    OI(o, 0xc8) += OI(o, 0xd0) - OI(o, 0xd4);
    if (OI(o, 0xcc) >= 0) { if (OI(o, 0xc8) < 0) OI(o, 0xc8) = 0; if (OI(o, 0xc8) > OI(o, 0xcc)) OI(o, 0xc8) = OI(o, 0xcc); }
    else { if (OI(o, 0xc8) > 0) OI(o, 0xc8) = 0; if (OI(o, 0xc8) < OI(o, 0xcc)) OI(o, 0xc8) = OI(o, 0xcc); }
    OLAT(o) += OI(o, 0xc8);
    for (int k = 0; k < 2; k++) {
        E3Obj *c = child(W, o, k);
        if (!c) continue;
        ODEP(c) = ODEP(o); OY(c) = OY(o); OLAT(c) = OLAT(o) - (k ? 0x1c00000 : 0xc00000);
        OS(c, 0x70) = OS(o, 0x70);
    }
    if (OW(o, 0x38) == OW(PL, 0x38) && OLAT(o) - 0x540000 < OLAT(PL) && OLAT(PL) < OLAT(o) + 0x540000 &&
        ODEP(o) - 0x4e8000 < ODEP(PL) && ODEP(PL) < ODEP(o) + 0x358000) hit_player(W, o, 0x28);
    offscreen_remove(W, o);
}

/* ---- Gauntlet 0x3e: swinging chain with a hammer (FUN_10012500 / FUN_10012820 / FUN_10012aa0) ---- */

static void g3e_init(E3World *W, E3Obj *o)
{
    OI(o, 0xdc) = o->variant;
    OS(o, 0x11e) = 0; OS(o, 0x116) = 0x800; OS(o, 0x126) = 0x800; OW(o, 0x98) = 0;
    OI(o, 0xe0) = 0; OS(o, 0x30) = 0;
    OU(o, 0x50) = 2; OW(o, 0x2a) = 8;
    OI(o, 0xcc) = OI(o, 0xbc) = 1;
    static const int32_t DY[4] = {0, 0x780000, 0xf40000, 0x1b80000};
    for (int k = 0; k < 4; k++) {
        E3Obj *c = e3_child(W, o, ODEP(o), OY(o) + DY[k], OLAT(o), 0x402a);
        if (!c) continue;
        OB(c, 0x22) = 0;
        OW(c, 0x34) = k == 3 ? (OI(o, 0xdc) ? 0x16 : 0x15) : 0xc;
        OU(c, 0x54) |= 0x10000040;
        if (k == 3) { OU(c, 0x54) &= ~0x20u; OW(c, 0x3a) = 400; }
    }
}

static void g3e_update(E3World *W, E3Obj *o)
{
    static const int OFFB[4] = {0, -0x78, -0xf4, -0x198};
    for (int k = 0; k < o->nchild; k++) {
        E3Obj *c = child(W, o, k);
        if (!c) continue;
        if (OW(o, 0x2a) & 8) {
            int a = OS(o, 0x126) & 0xfff;
            OS(o, 0x126) = (int16_t)a;
            int sv = ((e3_cos(OS(o, 0x126) - 0x400) << 9) >> 12) + 0x800;
            OS(o, 0x116) = (int16_t)sv;
            if (OI(o, 0xdc) == 0) { if (sv > 0x7ff) OS(o, 0x116) = (int16_t)-sv; }
            else {
                if (a != 0 && a < 0x800 && OI(o, 0xcc)) { e3_sound(W, 0x60a0, c); OI(o, 0xcc) = 0; }
                if (OS(o, 0x126) > 0x7ff && !OI(o, 0xcc)) { e3_sound(W, 0x60a0, c); OI(o, 0xcc) = 1; }
                if (OS(o, 0x126) > 0x400 && OS(o, 0x126) < 0xc00 && OI(o, 0xbc)) { e3_sound(W, 0x609f, c); OI(o, 0xbc) = 0; }
                if (OS(o, 0x126) > 0xbff && !OI(o, 0xbc)) { e3_sound(W, 0x609f, c); OI(o, 0xbc) = 1; }
                if (OS(o, 0x116) < 0x801) OS(o, 0x116) = (int16_t)-OS(o, 0x116);
            }
            /* FUN_10012aa0 */
            int ang = OS(o, 0x116);
            OS(c, 0x74) = (int16_t)(ang >> 2);
            int cs = e3_cos(ang), sn = e3_cos(ang - 0x400), b = OFFB[k];
            OLAT(c) = ((0 * cs - b * sn) >> 12) * 0x10000 + OLAT(o);
            OY(c) = ((b * cs + sn * 0) >> 12) * 0x10000 + OY(o) - 0x800000;
            ODEP(c) = ODEP(o);
            if (OW(o, 0x98) == 0) OS(o, 0x126) += 0xc;
        }
        int16_t mb[6], r[3];
        if (OW(c, 0x34) == 0x15 && OW(o, 0x38) == OW(PL, 0x38) && e3_model_box(W, c, mb)) {
            rel_pos(c, PL, r);
            if (-(mb[3] >> 1) < r[2] && r[2] < (mb[3] >> 1) && -(mb[5] >> 1) < r[0] && r[0] < (mb[5] >> 1) &&
                -mb[4] < r[1] && r[1] < mb[4]) hit_player(W, o, 0x25);
        }
    }
    offscreen_remove(W, o);
}

/* ---- Gauntlet 0x40: swinging blade (FUN_1000ebe0 / FUN_1000ec60), bank 3 ---- */

static void g40_init(E3World *W, E3Obj *o)
{
    OW(o, 0x34) = 0;
    OU(o, 0x50) = 2;
    OS(o, 0x116) = (int16_t)e3_rand(0x800);
    e3_set_anim(W, o, 0);
    OW(o, 0x2a) = 8;
    OW(o, 0x3a) = 0x168;
    OS(o, 0x11e) = 0; OS(o, 0x126) = 1; OW(o, 0x98) = 1;
}

static void g40_update(E3World *W, E3Obj *o)
{
    int16_t r[3]; rel_pos(o, PL, r);
    int sw = e3_cos((OS(o, 0x116) - 0x400) - 0x400) * 0x5a >> 12;   /* swing angle */
    int reach = ((e3_cos(sw - 0x400 - 0x400) << 8) >> 12) * 4;
    OW(o, 0x2a) = sw < 0 ? 2 : 4;
    if (sw > 0x58 && OS(o, 0x126)) { OS(o, 0x126) = 0; e3_sound(W, 0x6081, o); }
    if (sw < -0x58 && !OS(o, 0x126)) { OS(o, 0x126) = 1; e3_sound(W, 0x6082, o); }
    if (OW(o, 0x2a) & 4) OS(o, 0x11e) = 1;
    OS(o, 0x116) = (int16_t)((OS(o, 0x116) + 0x30) & 0xfff);
    e3_anim(W, o);
    OS(o, 0x74) = (int16_t)sw;
    if (OW(o, 0x38) == OW(PL, 0x38) && reach - 0x10 < r[2] && r[2] < reach + 0x10 && r[0] > -0x40 && r[0] < 0x40) {
        hit_player(W, o, 0x23);
        if (OW(o, 0x98)) { e3_sound(W, 0x6080, PL); OW(o, 0x98) = 0; }
    }
    offscreen_remove(W, o);
}

static void child_update(E3World *W, E3Obj *o)
{
    if (OW(o, 0x2e) == 0x402f) OW(o, 0x34) = 0x11;
    E3Obj *p = o->parent > 0 ? &W->obj[o->parent - 1] : NULL;
    if (!p || !p->used) e3_remove(W, o);
}


/* ---- Cyclops chase: the Cyclops runs ahead of Hercules (FUN_10003290 spawn, FUN_10003200 init,
 * FUN_10003610 per frame; attack states set by floor attributes 0x53/0x54/0x57/0x5b in FUN_10017ae0) ---- */

static void cyc_init(E3World *W, E3Obj *o)
{
    OW(o, 0x98) = 0x50; OI(o, 0xcc) = 0x50;
    OU(o, 0x50) = 0x202; OU(o, 0x54) = 0;
    OW(o, 0x3e) = 0x7fa;
    OW(o, 0x34) = 0; OS(o, 0x30) = 0;
    OI(o, 0xe0) = 0; OS(o, 0x11e) = 0; OI(o, 0xdc) = 0; OI(o, 0xbc) = 0; OI(o, 0xac) = 1; OI(o, 0xd8) = 1;
    OB(o, 0x22) = 5;
}

static void cyc_bank(E3Obj *o)
{
    uint32_t s = OU(o, 0x64);
    OB(o, 0x22) = (s == 0x10033100 || s == 0x100331b0) ? 7 : 5;
}

void e3_cyclops_attr(E3World *W, int a)
{
    if (W->cyclops < 0) return;
    E3Obj *c = &W->obj[W->cyclops];
    if (!c->used) return;
    switch (a) {
    case 0x53: OW(c, 0x2a) = 0x42; break;
    case 0x54: if (OW(c, 0x2a) != 0x44) { OW(c, 0x2a) = 0x44; e3_set_anim(W, c, 5); } break;
    case 0x57: if (OW(c, 0x2a) != 0x41) { OW(c, 0x2a) = 0x41; e3_set_anim(W, c, OI(c, 0xd8) ? 2 : 0); } break;
    case 0x5b: if (OW(c, 0x2a) != 0x43) { OW(c, 0x2a) = 0x43; e3_set_anim(W, c, 8); } break;
    default: break;
    }
}

static void cyc_update(E3World *W, E3Obj *c)
{
    ODEP(c) += ODEP(PL) - OI(PL, 0x9c) > 0 ? ODEP(PL) - OI(PL, 0x9c) : 0;    /* keeps pace with the camera */
    OW(c, 0x38) = OW(PL, 0x38);
    int st = OW(c, 0x2a);
    if (st == 0x41) {           /* stomp */
        OI(c, 0xac) = 1;
        cyc_bank(c); e3_anim(W, c);
        if (OI(c, 0xd8) == 0) {
            if (OW(c, 0x34) == 0x36) OS(c, 0x11e) = 1;
            if (OW(c, 0x34) == 0x3a) { OS(c, 0x11e) = 2; OI(c, 0xd8) = 1; e3_set_anim(W, c, 1); OW(c, 0x2a) = 0x45; }
        } else {
            if (OW(c, 0x34) == 0x5b) OS(c, 0x11e) = 1;
            if (OW(c, 0x34) == 0x5f) { OS(c, 0x11e) = 2; OI(c, 0xd8) = 0; e3_set_anim(W, c, 1); OW(c, 0x2a) = 0x45; }
        }
    }
    st = OW(c, 0x2a);
    if (st == 0x42 || st == 0x45) {     /* run */
        if (st == 0x42) OS(c, 0x11e) = 0;
        cyc_bank(c); e3_anim(W, c);
        static const int BACK[] = {0xf, 0x21, 0x94, 0x72, 0x2b, 0x4d, 0xa7, 0x36};
        for (size_t k = 0; k < sizeof BACK / sizeof BACK[0]; k++) if (OW(c, 0x34) == BACK[k]) e3_set_anim(W, c, 4);
        if (OW(c, 0x34) == 0xc) {
            static int nxt = 0;
            int n = nxt; nxt = nxt + 1 > 2 ? 0 : nxt + 1;
            e3_set_anim(W, c, n == 0 ? 3 : n == 1 ? 6 : 7);
        }
    }
    if (OW(c, 0x2a) == 0x44) {          /* throws */
        cyc_bank(c); e3_anim(W, c);
        if (OW(c, 0x34) == 0x20 && !OI(c, 0xe0)) {
            E3Obj *t = e3_spawn(W, 0x38, ODEP(c) + 0x1000000, OY(c), OLAT(c) - 0x400000, OW(c, 0x38));
            if (t) { e3_type_init(W, t); OI(c, 0xe0) = 1; }
        }
        if (OW(c, 0x34) == 0x2b) { OW(c, 0x2a) = 0x42; OI(c, 0xe0) = 0; }
    }
    if (OW(c, 0x2a) == 0x43) {          /* pulls a column down */
        cyc_bank(c); e3_anim(W, c);
        if (OW(c, 0x34) == 0xa1 && OI(c, 0xac)) {
            E3Obj *t = e3_spawn(W, 0x30, ODEP(c), OY(c) - 0x1800000, OLAT(c) + 0x800000, OW(c, 0x38));
            if (t) e3_type_init(W, t);
            OI(c, 0xac) = 0;
        }
        if (OW(c, 0x34) == 0xa7) { e3_set_anim(W, c, 1); OW(c, 0x2a) = 0x42; }
    }
    if (W->state == 6 && OW(c, 0x2a) != 0x47) { OI(c, 0xac) = 1; OW(c, 0x2a) = 0x47; OB(c, 0x22) = 7; e3_set_anim(W, c, 1); }
    if (OW(c, 0x2a) == 0x47) { OI(c, 0xac) = 1; e3_anim(W, c); }
    if (W->ending && OW(c, 0x2a) != 0x48) { OI(c, 0xac) = 1; OW(c, 0x2a) = 0x48; OB(c, 0x22) = 7; e3_set_anim(W, c, 3); }
    if (OW(c, 0x2a) == 0x48) e3_anim(W, c);
}


/* ---- Torment 0x30: rock falling from the cave roof (FUN_100266a0 / FUN_10026770 / FUN_10001600) ---- */

static void t30_init(E3World *W, E3Obj *o)
{
    OU(o, 0x54) |= 0x10000040; OU(o, 0x50) = 0x202;
    OW(o, 0x2a) = 8; OW(o, 0x98) = 1; OI(o, 0xe0) = 0; OS(o, 0x116) = 0; OS(o, 0x11e) = 1;
    OB(o, 0x95) = 0;
    OI(o, 0xbc) = 0x1e0000; OI(o, 0xac) = 0; OI(o, 0xcc) = 0xf0000; OI(o, 0xb8) = 0;
    OI(o, 0xc0) = 0x20000; OI(o, 0xc4) = 0xffff;
    OS(o, 0x30) = 10; OW(o, 0x9a) = 0;
    OB(o, 0x22) = 0; OW(o, 0x34) = 2; OS(o, 0x72) = 0x100;
    OY(o) -= 0x4000000;
    OW(o, 0x3a) = 0x200; OW(o, 0x4a) = 0xfe;
    OI(o, 0xdc) = 1;
    OI(o, 0xd8) = (int32_t)e3_rand(0x10) + 0x60;
}

static void t30_update(E3World *W, E3Obj *o)
{
    int16_t r[3]; rel_pos(o, PL, r);
    if (r[0] < 1000 && OS(o, 0x116) == 0) { e3_sound(W, 0x6280, o); OW(o, 0x2a) = 4; OS(o, 0x116) = 1; }
    if (r[2] > -0x20 && r[2] < 0x20 && r[0] > -0x20 && r[0] < 0x20 && r[1] != 0x20 && r[1] < 0x20) hit_player(W, o, 0x31);
    if (OW(o, 0x2a) & 4) {
        OS(o, 0x72) += (int16_t)OI(o, 0xd8);
        /* FUN_10001600: fall with gravity until the floor */
        OI(o, 0xb8) += OI(o, 0xc0); if (OI(o, 0xb8) > OI(o, 0xbc)) OI(o, 0xb8) = OI(o, 0xbc);
        OY(o) += OI(o, 0xb8);
        int up, dn = e3_floor(W, HI(ODEP(o)), HI(OY(o)), HI(OLAT(o)), OW(o, 0x38), &up);
        if (up < 0x7fff && up < 0x100) {
            OY(o) -= up << 16;
            if (OI(o, 0xdc)) { e3_sound(W, 0xe281, o); OI(o, 0xdc) = 0; }
            OI(o, 0xd8) = 0; OW(o, 0x2a) = 2; OW(o, 0x34) = 3;
        }
        (void)dn;
    }
    offscreen_remove(W, o);
}

/* ---- Torment 0x33: lost soul dropping down onto the path (FUN_10026f00 / FUN_10026fe0) ---- */

static void t33_init(E3World *W, E3Obj *o)
{
    OI(o, 0xdc) = o->variant;
    OU(o, 0x50) = 0x202; OW(o, 0x38) = OW(PL, 0x38);
    OU(o, 0x54) = 0x80000000u & 0;
    OW(o, 0x2a) = 8; OS(o, 0x30) = 1; OS(o, 0x116) = 0x1e;
    e3_drop(W, o);
    e3_set_anim(W, o, 4);
    OW(o, 0x98) = 1; OI(o, 0xe0) = 1;
    if (OI(o, 0xdc)) OU(o, 0x54) |= 0x10;
    int k = (int)e3_rand(3);
    OS(o, 0x11e) = (int16_t)k;
    OB(o, 0x1c) = k == 0 ? 0x14 : k == 1 ? 6 : 0x13;
    OY(o) = (int32_t)0xfe800000;
}

static void t33_update(E3World *W, E3Obj *o)
{
    int16_t r[3]; rel_pos(o, PL, r);
    if ((OW(o, 0x2a) & 8) && r[0] < 0x3c4) OW(o, 0x2a) = 4;
    if (OW(o, 0x2a) & 4) {
        if (OI(o, 0xe0)) { e3_sound(W, 0x62a1, o); OI(o, 0xe0) = 0; }
        OY(o) += 0x120000;
        int up, dn = e3_floor(W, HI(ODEP(o)), HI(OY(o)), HI(OLAT(o)), OW(o, 0x38), &up);
        if (dn == 0 || (up < 0x7fff && up <= 0x24)) {
            if (up < 0x7fff) OY(o) -= up << 16;
            e3_sound(W, 0x629b, o); OW(o, 0x2a) = 2;
        }
    }
    if (OW(o, 0x2a) & 2) { e3_set_anim(W, o, 5); OS(o, 0x32) = 0; OW(o, 0x2a) = 0x80; }
    if ((OW(o, 0x2a) & 0x80) && OS(o, 0x32) == 0x10) { e3_set_anim(W, o, 2); OS(o, 0x32) = 0; OW(o, 0x2a) = 0x20; }
    if (e3_overlap(W, o, PL)) {
        hit_player(W, o, 0x2f);
        if (OW(o, 0x98)) { e3_sound(W, 0x6282, o); OW(o, 0x98) = 0; }
    }
    e3_anim(W, o);
    if (o->used) offscreen_remove(W, o);
}

typedef struct { int level, type; void (*init)(E3World *, E3Obj *); void (*update)(E3World *, E3Obj *); } TypeFn;
static const TypeFn TYPES[] = {
    {2, 0x2e, g2e_init, g2e_update}, {2, 0x2f, g2f_init, g2f_update}, {2, 0x3e, g3e_init, g3e_update},
    {2, 0x40, g40_init, g40_update},
    {8, 0x29, cyc_init, cyc_update},
    {10, 0x30, t30_init, t30_update}, {10, 0x33, t33_init, t33_update},
};

static const TypeFn *find_type(E3World *W, int t)
{
    for (size_t i = 0; i < sizeof TYPES / sizeof TYPES[0]; i++) if (TYPES[i].level == W->level && TYPES[i].type == t) return &TYPES[i];
    return NULL;
}

/* ---- type 6: pickups (FUN_10015440 / FUN_10015680) ---- */

static void pickup_init(E3World *W, E3Obj *o)
{
    int v = o->variant;
    OU(o, 0x50) = 2;
    OU(o, 0x54) = 0x80000000u;
    OW(o, 0x2a) = 8;
    OI(o, 0xbc) = (int32_t)((e3_rand(0x100000) + 0xf00000u) >> 5);
    OI(o, 0xac) = OI(o, 0xcc) = OI(o, 0xb8) = 0;
    OI(o, 0xc0) = OI(o, 0xc4) = 0x14000;
    e3_drop(W, o);
    OI(o, 0xd8) = OY(o);                    /* entry +4: resting height */
    if (W->lives > 8 && v == 6) v = 1;      /* no more extra lives above 9 */
    OI(o, 0xdc) = v;
    static const int ANIM[16] = {0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15, 11, 16};
    if (v == 0) OY(o) -= 0x100000;
    if (v == 2 || v == 4) OB(o, 0x22) = 2;
    if (v == 4) { OW(o, 0x3a) = 0xc0; }
    if (v >= 8) OB(o, 0x22) = 8;
    e3_set_anim(W, o, ANIM[v & 15]);
}

static void pickup_update(E3World *W, E3Obj *o)
{
    int v = OI(o, 0xdc);
    if ((OW(o, 0x2a) & 8) && e3_overlap(W, o, PL)) {
        switch (v) {
        case 0: W->shield_state = 1; e3_sound(W, 0x603e, PL); OB(PL, 0xec) = 200; break;        /* shield */
        case 1: W->health += 0x20; if (W->health > W->maxhealth) W->health = W->maxhealth; e3_sound(W, 0x603b, PL); break;
        case 2: W->coins++; e3_sound(W, 0x603f, PL); break;
        case 3: OW(PL, 0xee) = 0x56; W->boost = 1; e3_sound(W, 0x602c, PL); OU(o, 0x54) = 0x84000005u; break;    /* speed */
        case 4: W->coins2++; e3_sound(W, 0x603f, PL); break;
        case 5: {   /* password vase: which of the 4 bits depends on the level / path (FUN_10015680) */
            static int found; int p = OW(PL, 0x38), bit = 0;
            if (W->level == 2) bit = p == 0 ? 1 : p <= 2 ? 2 : p <= 6 ? 4 : 8;
            else if (W->level == 8) { found = W->vases ? found + 1 : 1; bit = found >= 4 ? 8 : 1 << (found - 1); }
            else bit = p == 0 ? 1 : p <= 2 ? 4 : 8;
            W->vases |= bit; e3_sound(W, 0x603e, PL); break;
        }
        case 6: W->lives++; e3_sound(W, 0x603d, PL); break;
        case 7: W->maxhealth += 0x20; if (W->maxhealth > 0xff) W->maxhealth = 0xff; e3_sound(W, 0x603d, PL); break;
        default: W->letters |= 1 << ((v - 8) & 7); e3_sound(W, 0x603c, PL); e3_scroll_trigger(W, (v - 8) & 7); break;           /* HERCULES letters */
        }
        OW(o, 0x2a) = 2 | 0x8000;
    }
    if (OW(o, 0x2a) & 4) {
        OU(o, 0x54) = 0x84000005u;
        OW(o, 0x4a) = W->level == 10 ? 0x80 : W->level == 2 ? 0x20 : 0x40;
        if (e3_anim(W, o)) { e3_remove(W, o); return; }
    }
    if (OW(o, 0x2a) & 2) {          /* collected: fly up, then the sparkle animation */
        OY(o) -= 0x80000;
        if (OY(o) < OI(o, 0xd8) - 0x800000) {
            OW(o, 0x3a) = 0x100; OB(o, 0x22) = 1;
            e3_set_anim(W, o, 7);
            OW(o, 0x2a) = 4 | 0x8000;
        }
        e3_anim(W, o);
    }
    if (OW(o, 0x2a) & 8) e3_anim(W, o);
    if (e3_offscreen(W, o, 0x100)) e3_remove(W, o);
}

/* ---- type 7: end of the level (FUN_10016240 / FUN_10016320) ---- */

static void endobj_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) = 2; OU(o, 0x54) = 0x80000000u;
    OW(o, 0x38) = OW(PL, 0x38);
    OW(o, 0x11e) = 0;
    OI(o, 0xac) = OI(PL, 0xac); OI(o, 0xa8) = OI(PL, 0xa8);
    OI(o, 0xb4) = OI(o, 0xb0) = 0;
    OW(o, 0x98) = 1;
    W->ending = 1;
}

static void endobj_update(E3World *W, E3Obj *o)
{
    E3Obj *P = PL;
    OW(o, 0x34) = 0;
    W->camtarget = 0x140;
    int rel = HI(ODEP(o) - ODEP(P));
    if (W->level == 2 || W->level == 10) {
        if (W->state == 10 || W->state == 1) {
            if (OLAT(o) > 0) OLAT(o) -= 0x20000;
            if (OLAT(o) < 0) OLAT(o) += 0x20000;
            OLAT(P) = OLAT(o);
        }
    } else {
        if (OY(o) > 0x17f0000) OY(o) -= 0x10000;
        if (W->state == 10 || W->state == 1) {
            if (OLAT(o) > -0x400000) OLAT(o) -= 0x20000;
            if (OLAT(o) < -0x400000) OLAT(o) += 0x20000;
            OLAT(P) = OLAT(o);
        }
    }
    ODEP(o) += OI(o, 0xac);                         /* FUN_1001f550 */
    OI(o, 0xac) -= W->level == 10 ? 0x4000 : 0x2000;
    if (W->level == 8) { if (OI(o, 0xac) <= 0) OI(o, 0xac) = 0; }
    else if (W->level == 2) { if (OI(o, 0xac) < 1 || rel > -3) OI(o, 0xac) = 0; }
    else if (!(OI(o, 0xac) > 0 && rel < 5)) OI(o, 0xac) = 0;
    int32_t lim = W->level == 2 ? 0x3bffffff : W->level == 8 ? 0x777fffff : 0x73ffffff;
    if (ODEP(P) > lim && !W->end_flag[1]) { W->end_flag[1] = 1; e3_win(W); }
}

/* ---- generic actors: animate, hurt on contact, leave when behind ---- */

static void generic_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) |= 2;
    if (OU(o, 0x110)) e3_set_anim(W, o, 0);
    OW(o, 0x2a) = 8;
    if (OB(o, 0x22)) e3_drop(W, o);
}

static void generic_update(E3World *W, E3Obj *o)
{
    e3_anim(W, o);
    if (!o->used) return;
    int k = hurt_kind(W, OW(o, 0x2e));
    if (k && W->state != 5 && W->state != 6 && e3_overlap(W, o, PL)) e3_hurt(W, k, o);
    if (e3_offscreen(W, o, 0x100)) e3_remove(W, o);
}

int e3_type_init(E3World *W, E3Obj *o)
{
    if (e3g_type_init(W, o)) return 1;      /* GAUNTLET: e3_gauntlet.c */
    int t = OW(o, 0x2e);
    { int cyc_type_init(E3World *, E3Obj *); if (W->level == 8 && cyc_type_init(W, o)) return 1; }    /* CYCLOPS: e3_cyclops.c */
    if (t == 6) { pickup_init(W, o); return 1; }
    if (t == 7) { endobj_init(W, o); return 1; }
    if (e3t_type_init(W, o)) return 1;      /* TORMENT */
    if (t & 0x4000) { OU(o, 0x50) = 2; OU(o, 0x54) |= 0x10000040; return 1; }
    const TypeFn *f = find_type(W, t);
    if (f) { f->init(W, o); return 1; }
    if (e3b_type_init(W, o)) return 1;      /* ENG3B */
    generic_init(W, o);
    return 0;
}

void e3_type_update(E3World *W, E3Obj *o)
{
    if (e3g_type_update(W, o)) return;      /* GAUNTLET: e3_gauntlet.c */
    int t = OW(o, 0x2e);
    { int cyc_type_update(E3World *, E3Obj *); if (W->level == 8 && cyc_type_update(W, o)) return; }  /* CYCLOPS */
    if (t == 6) { pickup_update(W, o); return; }
    if (t == 7) { endobj_update(W, o); return; }
    if (e3t_type_update(W, o)) return;      /* TORMENT */
    if (t & 0x4000) { child_update(W, o); return; }
    const TypeFn *f = find_type(W, t);
    if (f) { f->update(W, o); return; }
    if (e3b_type_update(W, o)) return;      /* ENG3B */
    generic_update(W, o);
}
