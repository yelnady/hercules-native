/* ENEMIES3: Thebes harpies and the Forest / Thebes actors left generic by the earlier passes.
 * See analysis/objects_notes.md "Enemies, part 3".
 *
 *   Thebes harpy 0x38      init 10010540, update2 100105a0 (13 states 10010800..100113c0), AI callback 5 = 10011450,
 *                          re-target 10010cc0 / 10010a40, take-off 10010900, spring mover FUN_1002b9a0
 *   Thebes harpy 0x47      init 100115f0, update2 10011690 (the one guarding the way on; leaves when the others die)
 */
#include "objects_int.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

enum { AI4_BASE = 400, AI4_HARPY, AI4_HARPY47, AI4_FISH, AI4_FISH4D, AI4_BIRD, AI4_SCRIPT, AI4_STATIC, AI4_TOWN, AI4_CHAR };
enum { H_HARPY = 0x10010540, H_HARPY47 = 0x100115f0,
       H_FISH = 0x10008dd0, H_FISH4D = 0x1000e170, H_BIRD = 0x1000e520, H_MEG = 0x1000e3b0, H_POLE5 = 0x10013c90,
       H_FANGIRL = 0x100342f0, H_THFLOOD = 0x100348a0, H_THCHAR = 0x10033ee0, H_THOLD = 0x10034c80, H_THFAT = 0x100341a0,
       H_THEQ = 0x100343a0, H_MARK24 = 0x10001010 };
static int thebes_sync, thebes_sync_t = -1;     /* DAT_10070802 / DAT_10070800 (Thebes per-frame hook FUN_10033720) */

#define P_ (O->P)
static int iabs(int v) { return v < 0 ? -v : v; }
static int rnd(Objects *O, int n) { return obj_rnd(O, n); }
static void mirror(Object *o) { o->flags54 = (o->flags54 & ~0x10u) | (o->facing & 0x10); }

static int harpy_slot;          /* DAT_100721ac */
static int harpies_gone;        /* DAT_1006ffdc: the harpy fight is over (set by the 0x3d controller, leftovers.c) */
static int harpies_ctl;         /* LEFTOVERS: a 0x3d controller drives harpies_gone */
void enemies3_set_harpies_done(int done) { if (done < 0) { harpies_ctl = 0; harpies_gone = 0; } else { harpies_ctl = 1; harpies_gone = done; } }

/* FUN_1002b9a0(o, 7): accelerate toward the target d[0..2] on each axis, braking once past it.
 * Returns bit 0 / 1 / 2 when that axis is braking. */
static int spring(Object *o)
{
    int32_t *v = o->v;
    int r = 0;
    /* x */
    if (!o->facing ? o->d[0] < o->x : o->d[0] > o->x) { r |= 1; v[0] -= v[3]; } else v[0] += v[2];
    if (v[0] < 0) { v[0] = 0; o->facing = (uint8_t)~o->facing; }
    if (v[1] < v[0]) v[0] = v[1];
    /* y */
    if (!o->b95 ? o->d[1] < o->y : o->d[1] > o->y) { r |= 2; v[4] -= v[7]; } else v[4] += v[6];
    if (v[4] < 0) { v[4] = 0; o->b95 = (uint8_t)~o->b95; }
    if (v[1] < v[4]) v[4] = v[5];
    /* z */
    if (!o->zface ? o->d[2] < o->z : o->d[2] > o->z) { r |= 4; v[8] -= v[11]; } else v[8] += v[10];
    if (v[8] < 0) { v[8] = 0; o->zface = (uint8_t)~o->zface; }
    if (v[1] < v[8]) v[8] = v[9];
    o->x += o->facing ? -v[0] : v[0];
    o->y += o->b95 ? -v[4] : v[4];
    o->z += o->zface ? -v[8] : v[8];
    return r;
}

static int target_dist(const Object *o)                            /* FUN_1002ad70(o, o)[4]: approximate 3D distance */
{                                                                   /* FX: exact port of the original estimate */
    int32_t ex = o->x - o->d[0], ey = o->y - o->d[1], ez = o->z - o->d[2];
    int16_t ax = (int16_t)((uint32_t)ex >> 16), ay = (int16_t)((uint32_t)ey >> 16), az = (int16_t)((uint32_t)ez >> 16);
    if (ex < 0) ax = (int16_t)-ax;
    if (ey < 0) ay = (int16_t)-ay;
    if (ez < 0) az = (int16_t)-az;
    int16_t hi = ax, lo = ay;
    if (ax < ay) { hi = ay; lo = ax; }
    int16_t mid = az;
    if (hi < az) { mid = hi; hi = az; }
    return (uint16_t)((mid >> 2) + hi + (lo >> 5) * 0xb);
}

static int floor_below(Objects *O, const Object *o)                /* FUN_10020b50 != 0x7fff */
{
    int up, down;
    level_probe(O->L, I(o->x), I(o->y), I(o->z), &up, &down, NULL);
    return down != 0x7fff;
}

static int landed(Objects *O, Object *o)                           /* FUN_10029e20 */
{
    int up, down;
    level_probe(O->L, I(o->x), I(o->y), I(o->z), &up, &down, NULL);
    if (down == 0) return 1;
    if (up != 0x7fff && up <= 24) { o->y -= FX(up); return 1; }
    return 0;
}

static void harpy_reset(Objects *O, Object *o)                     /* FUN_100107c0 */
{
    o->movei = rnd(O, 3) + 3;
    o->facing = 0;
    obj_set_anim(O, o, 0);
    o->v2a = 1;
}

static void harpy_takeoff(Objects *O, Object *o)                   /* FUN_100108b0 */
{
    obj_set_anim(O, o, 0x29); o->v2a = 2; o->movei = 0;
}

static void spring_v(Object *o, int32_t ax, int32_t dx, int32_t ay, int32_t dy, int32_t az, int32_t dz)
{
    o->v[0] = 0; o->v[1] = 0xf0000; o->v[2] = ax; o->v[3] = dx;
    o->v[4] = 0; o->v[5] = 0x80000; o->v[6] = ay; o->v[7] = dy;
    o->v[8] = 0; o->v[9] = 0xf0000; o->v[10] = az; o->v[11] = dz;
}

static void harpy_retarget(Objects *O, Object *o)                  /* FUN_10010cc0: fly up to a new perch */
{
    o->facing = o->b95 = o->zface = 0xff;
    o->b21 = 0x28; o->v2a = 7;
    spring_v(o, 0x8000, 0xf000, 0x6000, 0xf000, 0x8000, 0xa000);
    o->d[0] = o->home[0] + FX(rnd(O, 0x80) - 0x40);
    o->d[1] = 0x3500000;
    o->d[2] = (int32_t)((uint32_t)pe_u16(O->pe, 0x10052790 + 2u * (uint32_t)harpy_slot) << 16);
    harpy_slot = (harpy_slot + 1) & 0x1f;
    obj_set_anim(O, o, 0xd);
}

static void harpy_gohome(Objects *O, Object *o)                    /* FUN_10010a40 */
{
    o->facing = o->b95 = o->zface = 0xff;
    o->b21 = 0x28; o->v2a = 4;
    spring_v(o, 0x8000, 0xf000, 0x6000, 0xf000, 0x8000, 0xa000);
    o->d[0] = o->home[0]; o->d[1] = o->home[1]; o->d[2] = o->home[2];
    obj_set_anim(O, o, 0xd);
}

static void harpy_up(Objects *O, Object *o)                        /* FUN_10010900: take off */
{
    o->v[3] = o->v[7] = o->v[11] = 0xa000;
    o->b21 = 10; o->v2a = 3;
    o->v[0] = 0; o->v[1] = 0xf0000; o->v[2] = 0xb000;
    o->v[4] = 0; o->v[5] = 0x80000; o->v[6] = 0x6000; o->b95 = 0xff;
    o->v[8] = 0; o->v[9] = 0xf0000; o->v[10] = 0xb000; o->zface = 0xff;
    o->d[0] = o->facing ? o->x + 0x1800000 : o->x - 0x1800000;
    o->d[1] = o->y - 0x300000; o->d[2] = o->z - 0x1000000;
    obj_set_anim(O, o, 9);
}

static void harpy_release(Objects *O, Object *o)
{
    (void)O;
    o->d[4] = 0;                                                    /* +0xe8 victim link */
    if (o->victim) { o->victim->carrier = NULL; o->victim = NULL; }  /* LEFTOVERS: FUN_10010b90 drops the townsperson */
}

static void harpy_ai(Objects *O, Object *o)                        /* FUN_10011450 (AI callback 5) */
{
    if (o->v2a == 2) harpy_up(O, o);
    if (o->v2a == 2 || o->v2a == 3) {
        if (o->b21 || rnd(O, 2)) return;
        harpy_retarget(O, o);
        return;
    }
    if (o->v2a != 1) return;
    const Obj *p = &P_->o;
    if (o->z < 0x1000000) {
        if (iabs(I(o->z - p->z)) > 0xf) {
            int r = rnd(O, 0xe);
            if (r < 2) obj_set_anim(O, o, 2);
            if (r < 4) obj_set_anim(O, o, 0x21);
            if (r < 7) return;
            harpy_takeoff(O, o); return;
        }
        int dx = I(o->x - p->x), adx = iabs(dx);
        if (adx > 0x40) {
            if (dx < 0 && o->facing == 0xff) { obj_set_anim(O, o, 2); return; }
            if (dx > 0 && o->facing == 0) { obj_set_anim(O, o, 2); return; }
            if (dx < 0x51 || rnd(O, 4)) return;
            harpy_takeoff(O, o); return;
        }
        if (rnd(O, 10)) { obj_set_anim(O, o, adx > 0x28 ? 5 : 2); return; }
    } else {
        int r = rnd(O, 7);
        if (r == 1) obj_set_anim(O, o, 0x21);
        if (r == 2) obj_set_anim(O, o, 2);
        if (r < 4) return;
    }
    harpy_takeoff(O, o);
}

static void harpy_init(Objects *O, Object *o)                      /* FUN_10010540 */
{
    o->ai = AI4_HARPY;
    o->flags50 |= 0x80200;
    o->anim = -1; obj_set_anim(O, o, 0);
    o->movei = 0; o->moving = 1;
    o->cbid = 5;
    obj_drop(O, o);
    o->hp = 3;
    o->home[0] = o->x; o->home[1] = o->y; o->home[2] = o->z;
    harpy_reset(O, o);
}

static void harpy_tick(Objects *O, Object *o)                      /* FUN_100105a0 */
{
    if (o->airborne) { obj_drop(O, o); return; }
    int32_t px = o->x, py = o->y;
    switch (o->v2a) {
    case 1:                                                         /* FUN_10010800: walk the perch */
        obj_run_script(O, o); en_move4(O, o);
        if (!obj_ground_follow(O, o) ||
            (obj_attr_flags(O, I(o->x) + (o->facing ? -0x10 : 0x10), I(o->y), (I(o->z) + 0x40) & ~0x7f) & 2)) {
            if (getenv("HARPY_DBG")) printf("  harpy %d revert gf=%d\n", I(o->x), obj_ground_follow(O, o));
            o->x = px; o->y = py; obj_set_anim(O, o, 2);
        }
        mirror(o);
        break;
    case 2: obj_run_script(O, o); en_move4(O, o); break;            /* FUN_100108e0 */
    case 3: case 6: {                                               /* FUN_100109e0 */
        obj_run_script(O, o);
        int r = spring(o);
        if (o->v2a == 3) { if ((r & 1) && o->v[0] < 0x60000) obj_set_anim(O, o, 10); mirror(o); }
        if (o->b21) o->b21--;
        break;
    }
    case 4: {                                                       /* FUN_10010b10: back to the perch */
        obj_run_script(O, o);
        spring(o);
        if (target_dist(o) < 8) { obj_set_anim(O, o, 0x11); o->v2a = 5; o->movei = 0; harpy_release(O, o); }
        mirror(o);
        break;
    }
    case 5:                                                         /* FUN_10010bd0: settle onto the floor */
        if (!floor_below(O, o)) { harpy_retarget(O, o); break; }
        obj_run_script(O, o); en_move4(O, o);
        if (iabs(o->x - o->home[0]) < 0x10000) o->x = o->home[0]; else o->x += o->x < o->home[0] ? 0x10000 : -0x10000;
        if (iabs(o->z - o->home[2]) < 0x10000) o->z = o->home[2]; else o->z += o->z < o->home[2] ? 0x10000 : -0x10000;
        if (landed(O, o)) { harpy_reset(O, o); o->x = o->home[0]; o->z = o->home[2]; }
        break;
    case 7: {                                                       /* FUN_10010dc0: fly to the hunting spot */
        obj_run_script(O, o);
        int r = spring(o);
        (void)r;
        if (target_dist(o) < (getenv("LEFT_H8") ? 0xd0 : 0x20))   /* LEFTOVERS: LEFT_H8 test hook loosens the arrival test */ { o->d[4] = 0; o->v2a = 8; obj_set_anim(O, o, 0x15); o->b21 = 0; o->cool = 0; }
        /* FX: FUN_10010dc0 only swaps to anim 0xe / mirrors while the state is 3, which it never is here; doing it
         * in state 7 started anim 0xe, whose script sets state 6, so the harpies never reached state 8. */
        break;
    }
    case 8: {                                                       /* FUN_10010e70: hover, look for prey */
        obj_run_script(O, o);
        if (o->z < o->d[2]) o->z += 0x10000;
        if (o->d[2] < o->z) o->z -= 0x10000;
        if (o->b21) { o->b21--; mirror(o); break; }
        const Obj *p = &P_->o;
        if ((o->z <= 0x400000 || p->z >= 0) && (o->z >= 0x400000 || p->z <= 0x800000) && iabs(I(p->x - o->x)) < 0x61) {
            int dy = (p->y - o->y) >> 0x14;
            o->s9a = (int16_t)(dy ? I(p->x - o->x) / dy : 0);
            o->v2a = 0xb; obj_set_anim(O, o, 0x2c);                 /* FUN_10011180: dive at Hercules */
            return;
        }
        {   /* LEFTOVERS: FUN_10010e70 - otherwise look for a townsperson (group 0x55) under it */
            Object *f = leftovers_food_near(O, o);
            if (f) {
                int dx = f->x - o->x, dy = (f->y - o->y) >> 0x13;
                o->s9a = (int16_t)(dy ? (I(dx) / dy) : 0);
                o->victim = f; f->carrier = o;
                o->v2a = 9; obj_set_anim(O, o, 0x18);               /* FUN_10011020 */
                return;
            }
        }
        if (++o->cool > 0x5a) harpy_retarget(O, o);
        mirror(o);
        break;
    }
    case 9: {                                                       /* LEFTOVERS: FUN_10011040 - swoop onto the townsperson */
        if (!floor_below(O, o)) harpy_retarget(O, o);
        obj_run_script(O, o);
        Object *f = o->victim;
        if (f && f->used) {
            o->y += 0x80000;
            o->x += FX(o->s9a);
            int d = I(o->x - f->x);
            if (iabs(d) < 8) { o->x = f->x; o->z = f->z; }
            if (!o->facing) { if (d > 0) o->facing = 0xff; } else if (d < 0) o->facing = (uint8_t)~o->facing;
            int a[6], b[6];
            if (obj_box(O, f, 0, b) && obj_box(O, o, 0, a) && obj_boxes_overlap(a, b)) {   /* FUN_1002a3d0 */
                o->y -= 0x80000;
                obj_set_anim(O, o, 8); o->v2a = 10; o->movei = 0; o->b21 = (uint8_t)(rnd(O, 0x3c) + 0x1e);   /* FUN_10011290 */
                f->v2a = 1; obj_set_anim(O, f, 5);
                obj_sound(O, (uint16_t)f->s9a, f->x);
                mirror(o);
                break;
            }
        } else if (f) { o->victim = NULL; }
        if (landed(O, o)) {
            harpy_takeoff(O, o);
            if (o->victim) o->victim->carrier = NULL;
            o->victim = NULL;
        }
        mirror(o);
        break;
    }
    case 0xb: {                                                     /* FUN_100111a0: the dive */
        if (!floor_below(O, o)) { harpy_retarget(O, o); break; }
        obj_run_script(O, o);
        o->y += 0x100000;
        o->x += FX(o->s9a);
        int d = I(o->x - P_->o.x);
        if (!o->facing) { if (d < 0) o->facing = 0xff; } else if (d >= 0) o->facing = (uint8_t)~o->facing;
        if (en_hits_player(O, o, 1)) { en_hurt_player(O, o, 3); harpy_retarget(O, o); }
        else if (landed(O, o)) harpy_reset(O, o);
        mirror(o);
        break;
    }
    case 0xc: case 0xd: {                                           /* FUN_100113c0: flee */
        obj_run_script(O, o);
        int r = spring(o);
        if (o->v2a == 0xc && (r & 1) && o->v[0] < 0x60000) obj_set_anim(O, o, 0x2e);
        if (en_offscreen(O, o, 0x40) || o->y < 0x900000) { o->d[4] = 0; obj_kill(O, o, 1); return; }
        return;
    }
    case 10:                                                        /* FUN_100112d0: carry the victim off */
        obj_run_script(O, o); en_move4(O, o);
        if (o->b21) o->b21--; else harpy_gohome(O, o);
        break;
    case 0xe: obj_run_script(O, o); return;                         /* dying */
    case 0xf: obj_run_script(O, o); o->y = 0x1800000; return;
    }
    if (o->z < -0x800000) o->z = -0x800000;
    if (o->z > 0x2ff0000) o->z = 0x2ff0000;
    if (!o->inv) {
        int h = en_took_hit(O, o), dmg = 0;
        if (h == 1) { en_hit_sound(O, 0x10); dmg = en_blow_damage(O); }
        else if (h == 2) dmg = 100;
        if (dmg) {
            o->hp -= (int16_t)dmg;
            obj_set_anim(O, o, o->v2a == 1 ? 0x1b : 0x1e);
            o->inv = 0x14;
        }
    } else o->inv--;
    if (o->hp < 1) {
        obj_set_anim(O, o, 0x24);
        o->flags50 &= ~0x200u;
        o->d[4] = 0;
        o->v2a = 0xe;
        int diff = P_ ? P_->difficulty : 1;
        O->hud_boss_damage += 0x100 / (diff * 6 + 7) + 1;           /* DAT_10072184 */
        O->kills++;
    }
}

static void harpy47_tick(Objects *O, Object *o)                    /* FUN_10011690 */
{
    o->flags54 &= ~8u;
    if (o->v2a == 2) {
        spring(o);
        if (en_offscreen(O, o, 0x40) || o->y < 0x900000) { obj_kill(O, o, 1); return; }
    } else {
        o->y = 0x3a00000;
        if (iabs(I(o->x - P_->o.x)) < 0x200) {
            if (P_->o.y < 0x3a00000) { if (o->v2a == 0) obj_set_anim(O, o, 4); o->y = P_->o.y; }
            else if (o->v2a == 1) obj_set_anim(O, o, 0);
            if (harpies_gone) {                                     /* DAT_1006ffdc: the flock is beaten - it leaves */
                Level *L = (Level *)O->L;
                L->flags[0x19] = 0;                                 /* DAT_100713bc[0x19] = 0: the way on opens */
                o->v2a = 2; obj_set_anim(O, o, 5);
            }
        }
        if (en_hits_player(O, o, 1)) en_hurt_player(O, o, 3);
    }
    obj_run_script(O, o);
}


/* ---------------------------------------------------------------- Forest ambient actors */

static int attr2(Objects *O, int x, int y, int z) { return obj_attr_flags(O, x, y, z) & 2; }    /* DAT_1004f1e0[FUN_10020d30] & 2 */

static void fish_tick(Objects *O, Object *o)                       /* FUN_10008e10: 0x2b fish jumping out of the stream */
{
    if (o->v2a != 1) {
        o->x += o->facing ? -o->v[0] : o->v[0];
        o->z += o->v[8];
        if (obj_run_script(O, o)) { o->life = rnd(O, 0x78); o->v2a = 1; obj_set_anim(O, o, 0); }
        goto offs;
    }
    if (o->life-- >= 0) goto offs;
    o->v[0] = 0; o->v[8] = 0;
    uint32_t r = (uint32_t)rnd(O, 0x8000) << 8;
    switch ((int)(((int32_t)r >> 8) % 3)) {
    case 0:                                                         /* leap in place */
        o->v2a = 0x11; obj_set_anim(O, o, 1);
        o->x += (int32_t)((r & 0x7f0000) - 0x400000); o->z += (int32_t)((r & 0xf0000) - 0x80000);
        o->facing = rnd(O, 0x80) < 0x41 ? 0xff : 0;
        break;
    case 1:                                                         /* leap toward the camera */
        o->v2a = 0x12;
        o->x += (int32_t)((r & 0x3f0000) - 0x200000); o->z += (int32_t)(r & 0x3f0000);
        o->v[8] = -0x8000 - (int32_t)(r & 0xffff);
        obj_set_anim(O, o, 2);
        o->facing = (r & 0x100000) ? 0xff : 0;
        break;
    default:                                                        /* leap along the stream */
        o->v2a = 0x13; obj_set_anim(O, o, 3);
        o->v[0] = -0x10000 - (int32_t)(r & 0xffff);
        if (!(r & 0x100000)) { o->x -= (int32_t)(r & 0x3f0000); o->facing = 0xff; }
        else { o->facing = 0; o->x += (int32_t)(r & 0x3f0000); }
        break;
    }
    mirror(o);
    obj_run_script(O, o);
offs:
    if (en_offscreen(O, o, 0x200)) obj_kill(O, o, 0);
}

static void fish4d_tick(Objects *O, Object *o)                     /* FUN_1000e200: climbs out up the bank */
{
    switch (o->v2a) {
    case 1:
        if (o->life-- < 0) { o->life = rnd(O, 0x3c); o->v2a = 0x21; obj_set_anim(O, o, 1); }
        else obj_run_script(O, o);
        break;
    case 2:
        o->y -= 0x40000;
        if (!attr2(O, I(o->x) + (o->facing ? -0x20 : 0x20), I(o->y), I(o->z)) || rnd(O, 0x8000) < 0x600) {
            o->v2a = 0x1d; obj_set_anim(O, o, 3);
        }
        obj_run_script(O, o);
        break;
    case 0x1d: if (obj_run_script(O, o)) { o->v2a = 1; obj_set_anim(O, o, 0); } break;
    case 0x21: if (obj_run_script(O, o)) { o->v2a = 2; obj_set_anim(O, o, 2); } break;
    default: o->v2a = 1; obj_set_anim(O, o, 0); break;
    }
    if (en_offscreen(O, o, 0x200)) obj_kill(O, o, 0);
}

static void bird_tick(Objects *O, Object *o)                       /* FUN_1000e470 (0x51) / FUN_1000e560 (0x52): calling birds */
{
    obj_run_script(O, o);
    if (o->life-- < 0) {
        o->life = ((rnd(O, 0x30) >> 4) + 3) * 0x1e;
        int a = o->variant ? 0x58c6 : 0x58c5, b = o->variant ? 0x58c8 : 0x58c7;
        obj_sound(O, rnd(O, 0x8000) < 0x2700 ? a : rnd(O, 0x8000) < 0x4000 ? b : 0x58c9, o->x);
    }
    if (en_offscreen(O, o, o->variant ? 300 : 0x200)) obj_kill(O, o, 0);
}


/* ---------------------------------------------------------------- Thebes townsfolk
 * Walkers share one pattern: movement table, ground follow, and a wall test ahead (attribute flags & mask);
 * on a wall they step back and play their "turn" animation. variant packs {probe dx, mask, turn anim, flip}. */
typedef struct { int probe, mask, turn, flip, range, walk, eq; } Town;
static const Town TOWN[] = {
    {0, 0, 0, 0, 0x200, 0, 0},          /* 0 fangirls 0x30-0x32 (FUN_10034350): script only */
    {0x18, 7, 0xc, 0, 0x40, 1, 0},      /* 1 THFLOOD 0x2c (FUN_10034910) */
    {0x10, 6, 0, 1, 0x140, 1, 0},       /* 2 THOLD 0x33 (FUN_10034d00) */
    {0x18, 6, 8, 0, 0x200, 1, 0},       /* 3 THFAT 0x44 (FUN_10034210) */
    {0x18, 7, 3, 0, 0x200, 0, 1},       /* 4 THEQ 0x2b (FUN_100343e0): ground follow without the table */
};

static void town_tick(Objects *O, Object *o)
{
    const Town *t = &TOWN[o->variant];
    if (o->airborne) { obj_drop(O, o); goto done; }
    int32_t px = o->x, py = o->y;
    if (t->eq) obj_run_script(O, o);
    else if (!t->walk) { obj_run_script(O, o); goto done; }
    else { obj_run_script(O, o); en_move4(O, o); }
    {
        int gf = obj_ground_follow(O, o);
        int zz = t->mask == 6 || t->eq ? ((I(o->z) + 0x40) & 0xf80) : I(o->z);
        if (!gf || (obj_attr_flags(O, I(o->x) + (o->facing ? -t->probe : t->probe), I(o->y), zz) & t->mask)) {
            o->x = px; o->y = py;
            if (t->flip) o->facing = (uint8_t)~o->facing;
            obj_set_anim(O, o, t->turn);
        }
    }
    if (t->eq && o->v2a == 4 && o->frame - 0x22 > 0 && o->frame - 0x22 < 0x28) {   /* stomping (FUN_100343e0) */
        /* LEFTOVERS: camera shake DAT_10071680, scaled by table 0x1005ef88 and the distance to the camera target */
        int dx = iabs(I(P_->o.x - o->x)), dy = iabs(I(P_->o.y - o->y)), dz = iabs(I(P_->o.z - o->z));
        int lo = dx < dy ? dx : dy, hi = dx < dy ? dy : dx, d2 = (lo >> 3) + hi + (lo >> 2);
        lo = d2 < dz ? d2 : dz; hi = d2 < dz ? dz : d2;
        int dist = (lo >> 3) + hi + (lo >> 2), w = 0x200 - dist;
        if (w < 1) w = 0;
        int amt = pe_u16(O->pe, 0x1005ef88u + 2u * (uint32_t)(o->frame - 0x22)) * w;
        if (amt) { O->shake_n++; O->shake_sum += (rnd(O, amt * 2) - amt) * 0x40; }
        if (!rnd(O, 10)) { Object *d = obj_spawn(O, 0x402a, o->x, o->y, o->z); if (d) d->spawn = -1; }   /* dust 0x402a */
    }
done:
    mirror(o);
    if (en_offscreen(O, o, t->range)) obj_kill(O, o, 0);
}

static void char_tick(Objects *O, Object *o)                       /* FUN_10033f30: THCHAR sings along with the hook */
{
    if (o->airborne) obj_drop(O, o);
    else {
        if (thebes_sync != o->v2a) { o->v2a = (uint16_t)thebes_sync; obj_set_anim(O, o, thebes_sync ? 0 : 2); obj_sound(O, 0x5148, o->x); }
        obj_run_script(O, o);
    }
    mirror(o);
    if (en_offscreen(O, o, 0x140)) obj_kill(O, o, 0);
}

static int town_init(Objects *O, Object *o, uint32_t init)
{
    int v = -1, nanim = 0;
    switch (init) {
    case H_FANGIRL: v = 0; nanim = 0xc; break;
    case H_THFLOOD: v = 1; o->movei = 4; o->cbid = 2; break;
    case H_THOLD: v = 2; nanim = 0x17; o->facing = (uint8_t)-rnd(O, 2); o->movei = rnd(O, 3) & 1; break;
    case H_THFAT: v = 3; nanim = 6; o->facing = (uint8_t)-rnd(O, 2); o->movei = 2; break;
    case H_THEQ: v = 4; o->cbid = 2; break;
    default: return 0;
    }
    o->ai = AI4_TOWN; o->variant = v;
    o->anim = -1; obj_set_anim(O, o, nanim ? rnd(O, nanim) : 0);
    {   /* LEFTOVERS: palette variant +0x1c (tables 0x100602a8 / b8 / bc; fangirls take it from the spawn flags) */
        int pal = 0xff;
        if (v == 0) { if ((O->spawn_flags & 0x3f) != 0x3f) pal = O->spawn_flags & 0x3f; }
        else if (v == 1) pal = pe_ptr(O->pe, 0x100602b8, 2)[rnd(O, 2)];
        else if (v == 2) pal = pe_ptr(O->pe, 0x100602a8, 5)[rnd(O, 5)];
        else if (v == 4) pal = pe_ptr(O->pe, 0x100602bc, 3)[rnd(O, 3)];
        o->palx = pal == 0xff ? 0 : (uint8_t)(pal + 1);
    }
    if (TOWN[v].walk) o->moving = 1;
    o->flags54 |= 8;
    obj_drop(O, o);
    return 1;
}

int enemies4_init(Objects *O, Object *o, uint32_t init)
{
    if (town_init(O, o, init)) return 1;
    switch (init) {
    case H_HARPY: harpy_init(O, o); return 1;
    case H_HARPY47:                                                 /* FUN_100115f0 */
        o->ai = AI4_HARPY47;
        o->anim = -1; obj_set_anim(O, o, 0);
        o->cbid = 0;
        o->v[0] = 0; o->v[4] = 0; o->v[6] = 0x2000; o->v[7] = 0x2000;
        o->facing = 0xff; o->flags54 |= 0x10;
        o->v[1] = 0x140000; o->v[2] = 0xb000; o->v[3] = 0x4000; o->v[5] = 0x10000; o->b95 = 0xff;
        o->d[0] = 0x22a00000; o->d[1] = 0x800000; o->d[2] = o->z;
        obj_drop(O, o);
        return 1;
    case H_FISH:                                                    /* FUN_10008dd0 */
        o->ai = AI4_FISH; o->life = rnd(O, 0x3c); o->v2a = 1; o->anim = -1; obj_set_anim(O, o, 0);
        return 1;
    case H_FISH4D:                                                  /* FUN_1000e170 */
        o->ai = AI4_FISH4D;
        o->facing = attr2(O, I(o->x) - 0x20, I(o->y) - 0x80, I(o->z)) ? 0 : 0xff;
        o->life = rnd(O, 0x3c); o->v2a = 1; obj_set_anim(O, o, 0);
        return 1;
    case H_BIRD:                                                    /* FUN_1000e520: scale 2 */
        o->ai = AI4_BIRD; o->variant = o->type == 0x52; o->life = 1; o->scale = 0x200;
        obj_set_anim(O, o, 0);
        return 1;
    case H_MEG:                                                     /* FUN_1000e3b0: Meg, update FUN_1000c910 runs her script */
        o->ai = AI4_SCRIPT; o->z += 0x180000; o->facing = 0xff; o->flags54 |= 0x10;
        o->anim = -1; obj_set_anim(O, o, 0);
        return 1;
    case H_THCHAR:                                                  /* FUN_10033ee0 */
        o->ai = AI4_CHAR; o->anim = -1; obj_set_anim(O, o, thebes_sync ? 0 : 2);
        o->v2a = (uint16_t)thebes_sync; o->flags54 |= 8; obj_drop(O, o);
        return 1;
    case H_MARK24:                                                  /* FUN_10001010: target marker (flags50 0x81, tag 0x2a) */
        o->ai = AI4_STATIC; o->flags54 = 0; o->flags50 = 0x81; o->tag = 0x2a; o->frame = 0;
        return 1;
    case H_POLE5:                                                   /* FUN_10013c90: a swing pole (flags50 0x41) */
        o->ai = AI4_STATIC; o->flags50 = 0x41;
        return 1;
    }
    return 0;
}

int enemies4_tick(Objects *O, Object *o)
{
    switch (o->ai) {
    case AI4_HARPY: harpy_tick(O, o); return 1;
    case AI4_HARPY47: harpy47_tick(O, o); return 1;
    case AI4_FISH: fish_tick(O, o); return 1;
    case AI4_FISH4D: fish4d_tick(O, o); return 1;
    case AI4_BIRD: bird_tick(O, o); return 1;
    case AI4_SCRIPT: obj_run_script(O, o); if (en_offscreen(O, o, 0x200)) obj_kill(O, o, 0); return 1;
    case AI4_TOWN: town_tick(O, o); return 1;
    case AI4_CHAR: char_tick(O, o); return 1;
    case AI4_STATIC: if (en_offscreen(O, o, 0x200)) obj_kill(O, o, 0); return 1;
    }
    return 0;
}

/* LV_THEBES: AI callback 2 = FUN_10034570, set by THEQ 0x2b (FUN_100343a0) and THFLOOD 0x2c (FUN_100348a0). It was swallowed
 * (enemies4_callback returned without doing anything), so the earthquake girls never started their stomp animations (v2a 4,
 * anims 6 / 0xb), the screen never shook and no 0x402a rocks fell. +0xc = projected screen x, +0x1f & 2 = on screen. */
static void town_stomp_ai(Objects *O, Object *o)
{
    const Player *P = O->P;
    if (o->v2a == 4) { o->v2a = 1; obj_set_anim(O, o, 0); return; }
    int r = rnd(O, 0x28), dz = I(P->o.z - o->z);
    int sx = 160 + I(o->x) - O->camx;
    int a = -1;
    if (sx < 0x15 || sx > 299) {                    /* off to the side */
        if (r < 0x19) return;
        if (r < 0x26) a = 0x10;
        else if (dz > 0x20) return;
        else if (r < 0x27) a = 6;
    } else {
        if (r < 10) return;
        if (r < 0xf) a = 0x10;
        else if (dz > 0x20) return;
        else if (r < 0x1c) a = 6;
    }
    if (a == 0x10) { if (pe_u32(O->pe, o->anims + 4u * 0x10)) obj_set_anim(O, o, 0x10); return; }
    if (a < 0) a = 0xb;
    if (!pe_u32(O->pe, o->anims + 4u * (uint32_t)a)) return;     /* THFLOOD's list has no such animation */
    obj_set_anim(O, o, a); o->v2a = 4;
}

int enemies4_callback(Objects *O, Object *o, int id)
{
    (void)id;
    if (o->ai == AI4_HARPY) { harpy_ai(O, o); return 1; }
    if (o->ai == AI4_TOWN && (o->variant == 4 || o->variant == 1)) { town_stomp_ai(O, o); return 1; }   /* LV_THEBES */
    return o->ai > AI4_BASE;
}

int leftovers_sync(void) { return thebes_sync; }            /* LEFTOVERS: DAT_10070802 for the THCHAR walkers */

void enemies4_frame(Objects *O)
{
    if (O->L->index != 5) return;
    if (--thebes_sync_t < 0) {                                      /* FUN_10033720: the singers' timer */
        if (!thebes_sync) { thebes_sync = 1; thebes_sync_t = rnd(O, 0x1e) + 0x5a; }
        else { thebes_sync = 0; thebes_sync_t = rnd(O, 0x1e) + 0xb4; }
    }
    int alive = 0, seen = 0;
    for (int i = 0; i < OBJ_MAX; i++) {
        const Object *o = &O->obj[i];
        if (o->used && o->ai == AI4_HARPY) { seen++; if (o->hp > 0) alive++; }
    }
    static int had;
    if (seen) had = 1;
    if (!harpies_ctl) harpies_gone = had && !alive;   /* LEFTOVERS: stand-in only without a 0x3d controller */
    if (getenv("HARPY_LOG")) {
        static int t;
        if (++t % atoi(getenv("HARPY_LOG")) == 0)
            for (int i = 0; i < OBJ_MAX; i++) {
                const Object *o = &O->obj[i];
                if (o->used && (o->ai == AI4_HARPY || o->ai == AI4_HARPY47))
                    printf("  harpy %04x pos %d,%d,%d v2a %d anim %d hp %d frame %d\n", o->type, I(o->x), I(o->y), I(o->z), o->v2a, o->anim, o->hp, o->frame);
            }
    }
}
