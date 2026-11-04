/* ENG3B: more per-type object handlers for the ENG3 levels (Gauntlet / Cyclops chase / Torment), ported from the
 * per-level handler tables (ENG3 0x1003b900 / b948 / b990 entry +0x1c: {init, update, update2} per actor type).
 * Same conventions as e3types.c: fields are the original object offsets (see e3world.h). */
#include "e3world.h"
#include "eng3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HI(v) ((int16_t)((uint32_t)(v) >> 16))
#define PL (&W->obj[0])
#define STATE_HIT 5

static void rel(E3Obj *a, E3Obj *b, int16_t r[3])        /* FUN_10001420 */
{
    r[0] = HI(ODEP(a) - ODEP(b)); r[1] = HI(OY(a) - OY(b)); r[2] = HI(OLAT(a) - OLAT(b));
}
static void hit(E3World *W, E3Obj *by, int kind)
{
    OI(PL, 0x110) = (int32_t)(by - W->obj) + 1;
    OS(PL, 0x116) = (int16_t)kind;
    if (W->state != STATE_HIT) e3_hurt(W, kind, by);
}
static int gone(E3World *W, E3Obj *o) { if (!e3_offscreen(W, o, 0x100)) return 0; e3_remove(W, o); return 1; }
static int same_path(E3World *W, E3Obj *o) { return OW(o, 0x38) == OW(PL, 0x38); }
static int32_t home_lat(E3World *W, E3Obj *o) { return o->spath >= 0 ? W->sp[o->spath][o->sidx].l : OLAT(o); }
static int sn(int a) { return e3_cos(a - 0x400); }
static E3Obj *kid(E3World *W, E3Obj *o, int k) { return k < o->nchild && W->obj[o->child[k]].used ? &W->obj[o->child[k]] : NULL; }
static void model(E3Obj *o, int m) { OB(o, 0x22) = 0; OW(o, 0x34) = (uint16_t)m; OU(o, 0x54) |= 0x10000040; }

/* ---- Gauntlet 0x30: battering ram from the right (FUN_1000fec0 / FUN_1000ffd0), two 0x3f posts ---- */
static void g30_init(E3World *W, E3Obj *o)
{
    OS(o, 0x126) -= 1;
    OU(o, 0x54) |= 0x10000040; OU(o, 0x50) = 0x202;
    model(o, 9);
    OS(o, 0x116) = 0; OW(o, 0x2a) = 2; OI(o, 0xc8) = 0;
    OI(o, 0xdc) = o->variant; OI(o, 0xe0) = 1;
    OW(o, 0x98) = 0;
    for (int k = 0; k < 2; k++) {
        E3Obj *c = e3_child(W, o, ODEP(o), OY(o), OLAT(o) + (k ? 0x1c00000 : 0xc00000), 0x402f);
        if (c) { model(c, 0x11); OS(c, 0x74) = 0x100; OW(c, 0x3a) = 0x200; OW(c, 0x4a) = 0x76; }
    }
}
static void post_spawn(E3World *W, E3Obj *o, int32_t dd, int32_t dl, int kind)
{
    E3Obj *c = e3_spawn(W, 0x3f, ODEP(o) + dd, OY(o), home_lat(W, o) + dl, OW(o, 0x38));
    if (!c) return;
    e3_type_init(W, c);
    OS(c, 0x11e) = (int16_t)kind;
}
static void g30_update(E3World *W, E3Obj *o)
{
    int32_t home = home_lat(W, o);
    if (OS(o, 0x126) == -1) {
        post_spawn(W, o, -0x400000, 0x1000000, 1);
        post_spawn(W, o, -0xc40000, 0x1600000, 3);
        OS(o, 0x126) = 1;
    }
    OW(o, 0x98) = 1; OW(o, 0x3a) = 0x2a0; OW(o, 0x34) = 9; OS(o, 0x74) = 0x200; OB(o, 0x96) = 0xff;
    int16_t r[3]; rel(o, PL, r);
    if (OLAT(o) <= home - 0xac0000) OW(o, 0x2a) = 4;
    if (home - 0x200000 <= OLAT(o)) { OW(o, 0x2a) = 2; if (r[0] < 0) { OW(o, 0x2a) = 8; OI(o, 0xd0) = OI(o, 0xcc) = 0; } }
    if (OW(o, 0x2a) & 2) {
        if (OI(o, 0xe0)) { e3_sound(W, 0x60a3, o); OI(o, 0xe0) = 0; }
        OI(o, 0xd0) = OI(o, 0xdc) ? 0x48000 : 0x24000; OI(o, 0xcc) = OI(o, 0xdc) ? 0x480000 : 0x240000;
        OS(o, 0x70) = (int16_t)((HI(home) - HI(OLAT(o))) * 2);
    }
    if (OW(o, 0x2a) & 4) {
        if (!OI(o, 0xe0)) { e3_sound(W, 0xe0a4, o); OI(o, 0xe0) = 1; }
        OI(o, 0xd0) = OI(o, 0xdc) ? -0x4000 : -0x2000; OI(o, 0xcc) = OI(o, 0xdc) ? -0x80000 : -0x40000;
        OS(o, 0x70) = (int16_t)((HI(home) - HI(OLAT(o))) * 2);
    }
    /* FUN_1001f620 (the ram moves toward -lateral: speed negated) */
    OI(o, 0xc8) += OI(o, 0xd0) - OI(o, 0xd4);
    if (OI(o, 0xcc) >= 0) { if (OI(o, 0xc8) < 0) OI(o, 0xc8) = 0; if (OI(o, 0xc8) > OI(o, 0xcc)) OI(o, 0xc8) = OI(o, 0xcc); }
    else { if (OI(o, 0xc8) > 0) OI(o, 0xc8) = 0; if (OI(o, 0xc8) < OI(o, 0xcc)) OI(o, 0xc8) = OI(o, 0xcc); }
    OLAT(o) -= OI(o, 0xc8);
    for (int k = 0; k < 2; k++) {
        E3Obj *c = kid(W, o, k);
        if (!c) continue;
        ODEP(c) = ODEP(o); OY(c) = OY(o); OLAT(c) = OLAT(o) + (k ? 0x1c00000 : 0xc00000);
        OS(c, 0x70) = OS(o, 0x70);
    }
    if (same_path(W, o) && OLAT(o) - 0x540000 < OLAT(PL) && OLAT(PL) < OLAT(o) + 0x540000 &&
        ODEP(o) - 0x4e8000 < ODEP(PL) && ODEP(PL) < ODEP(o) + 0x358000) hit(W, o, 0x28);
    gone(W, o);
}

/* ---- Gauntlet 0x3f: the ram's posts (FUN_10012e40 / FUN_10012e80) ---- */
static void g3f_init(E3World *W, E3Obj *o)
{
    (void)W;
    OU(o, 0x50) = 0x202; model(o, 0x10);
    OS(o, 0x116) = 0; OS(o, 0x11e) = 0; OW(o, 0x3a) = 0x140; OW(o, 0x98) = 1;
}
static void g3f_update(E3World *W, E3Obj *o)
{
    int k = OS(o, 0x11e);
    if (k == 0) {
        int v = o->variant;
        if (v == 1) { OS(o, 0x74) -= 8; OW(o, 0x34) = 0x14; if (OW(o, 0x98)) { e3_sound(W, 0x289b, o); OW(o, 0x98) = 0; } }
        else if (v == 2) OS(o, 0x74) -= 8;
        else if (v == 3) { OS(o, 0x74) -= 8; OW(o, 0x3a) = 200; OS(o, 0x72) = 0x300; if (ODEP(o) < ODEP(PL) - 0x800000) e3_remove(W, o); return; }
        else if (v == 4) { OW(o, 0x3a) = 200; OS(o, 0x72) = 0x11e; OS(o, 0x74) -= 8; }
        else { OS(o, 0x74) -= 8; OW(o, 0x3a) = 400; OS(o, 0x72) = 0x11e; gone(W, o); return; }
        if (ODEP(o) < ODEP(PL) - 0x800000) e3_remove(W, o);
        return;
    }
    if (k == 2) { OW(o, 0x3a) = 0x100; OS(o, 0x72) = 0x300; if (ODEP(o) < ODEP(PL) - 0x800000) e3_remove(W, o); return; }
    if (k == 3) { OW(o, 0x3a) = 0x100; OS(o, 0x72) = 0x100; }
    gone(W, o);
}

/* ---- Gauntlet 0x48: rotating double blade (FUN_10012be0 / FUN_10012c40) ---- */
static void g48_init(E3World *W, E3Obj *o)
{
    OU(o, 0x50) = 0x202; model(o, 0x18);
    OS(o, 0x11e) = 0x10;
    e3_drop(W, o);
    OW(o, 0x3a) = 0x200;
    OY(o) -= 0x1000000;
    OS(o, 0x116) = 0xc0;
    OW(o, 0x98) = 1; OW(o, 0x2a) = 8;
}
static void g48_update(E3World *W, E3Obj *o)
{
    OS(o, 0x74) = (int16_t)(OS(o, 0x74) - OS(o, 0x11e));
    int a = OS(o, 0x74) * 4;
    int c = e3_cos(a) * OS(o, 0x116), s = sn(a) * OS(o, 0x116);
    if (OW(o, 0x98)) { e3_sound(W, 0x28a2, o); OW(o, 0x98) = 0; }
    int y1 = c * 0x10, y2 = c * -0x10;
    if ((y1 >> 16) == OS(o, 0x116) || (y2 >> 16) == OS(o, 0x116)) e3_sound(W, 0x60a1, o);
    int32_t l1 = s * 0x10 + OLAT(o), l2 = s * -0x10 + OLAT(o);
    int32_t e1 = abs(y1 - OY(o)), e2 = abs(y2 - OY(o));
    if (same_path(W, o)) {
        int32_t pd = ODEP(PL), py = OY(PL), pl = OLAT(PL);
        int near = pd < ODEP(o) + 0x5e0000 && ODEP(o) - 0x5e0000 < pd;
        if (near && pl < l1 + 0x400000 && l1 - 0x400000 < pl && py < e1 + 0x6e0000 && e1 - 0x6e0000 < py) { hit(W, o, 0x2d); OW(o, 0x2a) = 2; }
        if (near && pl < l2 + 0x400000 && l2 - 0x400000 < pl && py < e2 + 0x6e0000 && e2 - 0x6e0000 < py) { hit(W, o, 0x2d); OW(o, 0x2a) = 2; }
        if (OW(o, 0x2a) & 2) OS(o, 0x11e) = 8;
    }
    gone(W, o);
}

/* ---- Gauntlet 0x49 / Torment 0x48: spinning hazard that strikes once (FUN_10013cb0 / 1002a040, FUN_1002a090) ---- */
static void spin_init(E3World *W, E3Obj *o, int m, int scale)
{
    OU(o, 0x50) = 0x202; model(o, m);
    OW(o, 0x2a) = 8;
    if (scale) OW(o, 0x3a) = (uint16_t)scale;
    OS(o, 0x30) = 1;
    e3_drop(W, o);
    if (!scale) OS(o, 0x72) = 0x200;
    OY(o) += 0x200000;
}
static void g49_init(E3World *W, E3Obj *o) { spin_init(W, o, 0x19, 0x180); }
static void t48_init(E3World *W, E3Obj *o) { spin_init(W, o, 0xf, 0); }
static void spin_update(E3World *W, E3Obj *o)
{
    int16_t r[3]; rel(o, PL, r);
    if (same_path(W, o) && r[2] > -0x20 && r[2] < 0x20 && r[0] > -0x20 && r[0] < 0x20) OW(o, 0x2a) = 2;
    if (OW(o, 0x2a) & 2) {
        OS(o, 0x72) += 0x30;
        OS(o, 0x70) = (int16_t)((e3_cos(OS(o, 0x126) - 0x400) << 6) >> 12);
        OS(o, 0x126) += 0x20;
        if (OS(o, 0x30)) { hit(W, o, 0x35); OS(o, 0x30) = 0; }
    }
    gone(W, o);
}

/* ---- Gauntlet 0x4b / Torment 0x49: scenery models chosen by the spawn variant (FUN_10013d70 / 1002a180) ---- */
static void scen_init(E3World *W, E3Obj *o, const int m[3], int lat0)
{
    OU(o, 0x50) = 0x202; OB(o, 0x22) = 0; OU(o, 0x54) |= 0x10000040; OW(o, 0x2a) = 8;
    int v = o->variant;
    OW(o, 0x34) = (uint16_t)(v >= 0 && v < 3 ? m[v] : m[0]);
    OW(o, 0x3a) = 300; OS(o, 0x30) = 1;
    e3_drop(W, o);
    OS(o, 0x72) = 0x200;
    if (lat0) OLAT(o) = 0;
}
static void g4b_init(E3World *W, E3Obj *o) { static const int m[3] = {0x1c, 0x1d, 0x1e}; scen_init(W, o, m, 1); }
static void t49_init(E3World *W, E3Obj *o) { static const int m[3] = {0xd, 0xe, 0x10}; scen_init(W, o, m, 0); }
static void scen_update(E3World *W, E3Obj *o) { gone(W, o); }

/* ---- Torment 0x34: floating raft Hercules can stand on (FUN_10027130 / FUN_10027180) ---- */
static void t34_init(E3World *W, E3Obj *o)
{
    (void)W;
    OU(o, 0x50) = 0x40202; OW(o, 0x2a) = 2; OU(o, 0x54) |= 0x10000000;
    OI(o, 0xdc) = 0x400000; OS(o, 0x72) = 0x200; OW(o, 0x3a) = 0x300; OS(o, 0x30) = 0;
    OW(o, 0x4a) = 0xfe; OS(o, 0x126) = 0; OS(o, 0x116) = 1;
}
static void t34_update(E3World *W, E3Obj *o)
{
    OW(o, 0x34) = 1;
    int16_t r[3]; rel(o, PL, r);
    OS(o, 0x74) = (int16_t)((e3_cos(OS(o, 0x126) - 0x400) << 4) >> 12);
    OS(o, 0x126) += 0x20;
    if (OY(PL) < 0x400000) {
        if (OS(o, 0x74) < 0) OS(o, 0x74) += 2;
        if (OS(o, 0x74) > 0) OS(o, 0x74) -= 2;
        if (OS(o, 0x70) < 0) OS(o, 0x70) += 2;
        if (OS(o, 0x70) > 0) OS(o, 0x70) -= 2;
    } else if (r[0] < 0x94 && r[0] > -0x94) {
        if (!OS(o, 0x30)) { OI(o, 0xdc) = 0x500000; if (OY(o) > 0x6bffff) OS(o, 0x30) = 1; }
        else OI(o, 0xdc) = 0x400000;
        OS(o, 0x70) = (int16_t)(-(r[0] >> 2));
        if (r[2] < 0x54 && r[2] > -0x54) OS(o, 0x74) += (int16_t)(r[2] >> 2);
    }
    if (OY(o) < OI(o, 0xdc)) OY(o) += OS(o, 0x116) * 0x40000;
    if (OI(o, 0xdc) < OY(o)) OY(o) += (OS(o, 0x116) & ~1) * -0x8000;
    gone(W, o);
}

/* ---- Torment 0x47: collapsing ceiling chunk with falling debris (FUN_10029e50 / FUN_10029ea0) ---- */
static void t47_init(E3World *W, E3Obj *o)
{
    (void)W;
    OU(o, 0x50) = 0x400202; OB(o, 0x22) = 0; OW(o, 0x34) = 10; OU(o, 0x54) |= 0x10000040;
    OS(o, 0x72) = 0x200; OW(o, 0x3a) = 0x180; OY(o) -= 0x600000;
    OS(o, 0x11e) = 0; OW(o, 0x98) = 1; OW(o, 0x4a) = 0xfe;
}
static void t47_update(E3World *W, E3Obj *o)
{
    int16_t r[3]; rel(o, PL, r);
    if (r[0] < 0x200) {
        W->shake = 1;      /* FUN_10008120: screen rumble */
        if (OW(o, 0x98)) {
            e3_sound(W, 0x2a2, o);
            for (int k = 0; k < 16; k++) {
                int32_t l = (int32_t)(e3_rand(0x100) * 0x10000) - (int32_t)(e3_rand(0x100) * 0x10000) + OLAT(o);
                E3Obj *c = e3_spawn(W, 0x4002, ODEP(o), OY(o) - (int32_t)(e3_rand(0x40) * 0x10000) - 0x800000, l, OW(o, 0x38));
                if (!c) continue;
                OU(c, 0x50) = 2; OW(c, 0x4a) = 200;
                OB(c, 0x94) = (k & 1) ? 0 : 0xff;   /* TORMENT: depth direction is +0x94 */ OB(c, 0x96) = (k & 2) ? 0xff : 0;
            }
            OW(o, 0x98) = 0;
        }
        OY(o) += (int32_t)(e3_rand(4) * -0x10000) - 0x8000;
    }
    gone(W, o);
}

typedef struct { int level, type; void (*init)(E3World *, E3Obj *); void (*update)(E3World *, E3Obj *); } TypeFn2;
static const TypeFn2 TYPES2[] = {
    {2, 0x30, g30_init, g30_update}, {2, 0x3f, g3f_init, g3f_update}, {2, 0x48, g48_init, g48_update},
    {2, 0x49, g49_init, spin_update}, {2, 0x4b, g4b_init, scen_update},
    {10, 0x34, t34_init, t34_update}, {10, 0x47, t47_init, t47_update}, {10, 0x48, t48_init, spin_update},
    {10, 0x49, t49_init, scen_update},
};

static const TypeFn2 *find2(E3World *W, int t)
{
    for (size_t i = 0; i < sizeof TYPES2 / sizeof TYPES2[0]; i++) if (TYPES2[i].level == W->level && TYPES2[i].type == t) return &TYPES2[i];
    return NULL;
}
int e3b_type_init(E3World *W, E3Obj *o) { const TypeFn2 *f = find2(W, OW(o, 0x2e)); if (!f) return 0; f->init(W, o); return 1; }
int e3b_type_update(E3World *W, E3Obj *o) { const TypeFn2 *f = find2(W, OW(o, 0x2e)); if (!f) return 0; f->update(W, o); return 1; }
