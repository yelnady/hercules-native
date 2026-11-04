/* LEFTOVERS: the ENG1 actors the earlier passes left on the generic "play the animation list" behaviour.
 * See analysis/objects_notes.md "Enemies, part 4".
 *
 *   Thebes 0x28 THFLASH      init 10033fc0, update2 10034000 (opens up and showers 8 pickups, table 0x1005e9c0)
 *   Thebes 0x2a THSNOW       init 10033910, update2 10033990 (+ partner 0x45 10033cc0 / 10033cf0, puff 0x4a)
 *   Thebes 0x34 THEWGUY      init 10034de0, update2 10034e20 (walks between depth planes, bumps Hercules)
 *   Thebes 0x3c              init 10011950, update 100119a0 (group 0x55: spawns the harpies' prey, table 0x10052100)
 *   Thebes 0x3d              init 10011a80, update 10011ac0 (the harpy fight: boss bar, DAT_1006ffdc)
 *   Thebes 0x39-0x3b THHAFOOD init 10011c10 / 10011c70 / 10011cd0 (FUN_10011d30), update2 10011df0
 *   Thebes 0x3f / 0x40 THCHAR init 10035c60 / 10035cf0, update2 10035d70 (+ shadow 0x202e 10035fb0 / 10035fe0)
 *   Thebes 0x48, Training 0x4c  init 10010380, update2 100103c0 (keeps a carryable rock alive near the player)
 *   Thebes 0x202c            init 100253b0, update 100253d0 (rubble shower 0x4028)
 *   Thebes 0x202d            init 10036b60, update 10036b90 (camera height bias zone, DAT_10070d04)
 */
#include "objects_int.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum { AI5_BASE = 500, AI5_FLASH, AI5_SNOW, AI5_SNOW45, AI5_PUFF, AI5_EWGUY, AI5_FOODSPAWN, AI5_HARPYCTL, AI5_FOOD,
       AI5_CHAR, AI5_SHADOW, AI5_SPAWNER, AI5_RUBBLE, AI5_CAMZONE, AI5_SCRIPTONLY };
enum { H_FLASH = 0x10033fc0, H_SNOW = 0x10033910, H_SNOW45 = 0x10033cc0, H_PUFF = 0x10033e10, H_EWGUY = 0x10034de0,
       H_FOODSPAWN = 0x10011950, H_HARPYCTL = 0x10011a80, H_FOOD39 = 0x10011c10, H_FOOD3A = 0x10011c70,
       H_FOOD3B = 0x10011cd0, H_CHAR3F = 0x10035c60, H_CHAR40 = 0x10035cf0, H_SHADOW = 0x10035fb0,
       H_SPAWNER = 0x10010380, H_RUBBLE = 0x100253b0, H_CAMZONE = 0x10036b60 };

#define P_ (O->P)
static int iabs(int v) { return v < 0 ? -v : v; }
static int rnd(Objects *O, int n) { return obj_rnd(O, n); }
static void mirror(Object *o) { o->flags54 = (o->flags54 & ~0x10u) | (o->facing & 0x10); }

static int ffd8;                /* DAT_1006ffd8 low word: which pickup row the next flasher showers */
static int boss_state, boss_bar;   /* DAT_10072188 / DAT_10070832 for the harpy fight */
static int harpy_done;          /* DAT_1006ffdc */

void leftovers_reset(Objects *O)
{
    O->cam_xmax = 0;
    ffd8 = 0; boss_state = 0; boss_bar = 0; harpy_done = 0;
    enemies3_set_harpies_done(-1);
}

static void first_anim(Objects *O, Object *o)                      /* FUN_10029270 */
{
    o->anim = 0; o->idx = -1; o->timer = 0;
    if (!o->anims) return;
    o->script = pe_u32(O->pe, o->anims);
    if (!o->script) { o->anim = (int16_t)pe_u16(O->pe, o->anims + 4); o->script = pe_u32(O->pe, o->anims + 4u * (uint32_t)o->anim); }
}

static int on_screen(Objects *O, const Object *o) { return !en_offscreen(O, o, 0); }   /* +0x1f bit 1 (FUN_1002ae50) */
static int wall_ahead(Objects *O, const Object *o, int dx, int zmask, uint16_t bits)  /* DAT_100713bc[FUN_1002a170] */
{
    int z = zmask ? ((I(o->z) + 0x40) & 0xf80) : I(o->z);
    return obj_attr_flags(O, I(o->x) + (o->facing ? -dx : dx), I(o->y), z) & bits;
}

/* ---------------------------------------------------------------- Thebes 0x28 THFLASH */

static void flash_tick(Objects *O, Object *o)                      /* FUN_10034000 */
{
    if (o->airborne) { obj_drop(O, o); goto done; }
    {
        int dx = iabs(I(P_->o.x - o->x)), dz = iabs(I(P_->o.z - o->z));
        if (o->tag == 2) {
            if (dx > 0x80 || (dz < 0x5a && dz > 0x10)) { obj_set_anim(O, o, 9); o->tag = 4; }
        } else if (dz < 0x10) {
            if (dx < 0x30) {
                obj_set_anim(O, o, 4); o->tag = 2;
                if (o->v2a == 0) {                                  /* the shower of pickups, row ffd8 & 7 of 0x1005e9c0 */
                    int32_t y = o->y; o->y -= 0x200000;
                    for (int i = 0; i < 8; i++)
                        en_drop_pickup(O, o, (int)pe_u32(O->pe, 0x1005e9c0u + 4u * (uint32_t)(i + (ffd8 & 7) * 8)));
                    o->y = y;
                    ffd8++; o->v2a++;
                }
            }
        } else if (dz > 0x5a && dx < 0x60) { obj_set_anim(O, o, 4); o->tag = 2; }
        /* the script runs with +0x2a and +0x97 swapped: its state words land in +0x97 */
        uint16_t sv = o->v2a; o->v2a = o->tag; o->tag = (uint8_t)sv;
        if (obj_run_script(O, o) && o->v2a == 1 && rnd(O, 5) == 0) { obj_set_anim(O, o, 1); o->v2a = 4; }
        sv = o->v2a; o->v2a = o->tag; o->tag = (uint8_t)sv;
    }
done:
    mirror(o);
    if (en_offscreen(O, o, 0x140)) obj_kill(O, o, 0);
}

/* ---------------------------------------------------------------- Thebes 0x2a THSNOW and its partner 0x45 */

static void snow_tick(Objects *O, Object *o)                       /* FUN_10033990 */
{
    if (o->airborne) { obj_drop(O, o); return; }
    int32_t px = o->x, py = o->y;
    obj_run_script(O, o);
    en_move4(O, o);
    if (o->cool) o->cool--;
    int dx = I(o->x - P_->o.x), dz = I(o->z - P_->o.z);
    switch (o->v2a) {
    case 0: case 1:
        if (o->cool == 0) {
            if (iabs(dz) < 0x20) obj_sound(O, 0x5140, o->x);
            o->cool = rnd(O, 0x3c) + 0x5a;
        }
        if (o->tag == 0) {
            if (iabs(dx) < 0x80 && iabs(dz) < 8) {
                Object *p = obj_spawn(O, 0x4a, o->x, o->y - 0x100000, o->z);
                if (p) p->spawn = -1;
                o->child = 0x25; o->child_bright = 0x80; o->child_semi = 1;   /* FUN_10027d40(0xa001) overlay */
                obj_sound(O, 0x4012, o->x);
                obj_set_anim(O, o, 7);
                o->hp = 0xa0;
            }
        } else if (!on_screen(O, o)) o->tag--;
        if (o->anim == 0 && ((o->facing == 0 && dx > 0) || (o->facing != 0 && dx < 0))) {
            obj_set_anim(O, o, 1);
            o->hp += 100;
        }
        if (o->hp-- < 0) { obj_set_anim(O, o, 4); o->hp = (int16_t)(rnd(O, 0x50) + 0x32); }
        break;
    case 3:
        if (!obj_ground_follow(O, o) && o->anim < 0xb) { o->x = px; o->y = py; o->facing = (uint8_t)~o->facing; }
        else if (!wall_ahead(O, o, 0x10, 0, 7)) {
            if (o->hp-- < 0) { obj_set_anim(O, o, 4); o->tag = 0x14; }
        } else { o->x = px; o->y = py; o->facing = (uint8_t)~o->facing; }
        break;
    case 5:
        if ((o->facing == 0 && dx > 0) || (o->facing != 0 && dx < 0)) o->facing = (uint8_t)~o->facing;
        if (dz > -0x20) { obj_sound(O, 0x5941, o->x); obj_sound(O, 0x5942, o->x); }
        o->v2a = 3;
        o->child = 0;                                               /* flags54 &= ~0x800000: overlay off */
        break;
    }
    mirror(o);
    if (en_offscreen(O, o, 0x400)) obj_kill(O, o, 0);
}

static void snow45_tick(Objects *O, Object *o)                     /* FUN_10033cf0: follows its leader, shown in state 3 */
{
    Object *l = o->parent;
    if (!l || !l->used) { obj_kill(O, o, 0); return; }
    if (l->v2a != 3) { o->hidden = 1; return; }
    o->hidden = 0;
    obj_run_words(O, o);
    o->x = l->x; o->y = l->y; o->z = l->z;
    o->facing = l->facing; mirror(o);
}

static void puff_tick(Objects *O, Object *o)                       /* FUN_10033e80: a short-lived puff */
{
    if (o->cool-- <= 0) { obj_kill(O, o, 1); return; }
    obj_run_words(O, o);
}

/* ---------------------------------------------------------------- Thebes 0x34 THEWGUY */

static void ewguy_tick(Objects *O, Object *o)                      /* FUN_10034e20 */
{
    if (o->airborne) { obj_drop(O, o); goto done; }
    int32_t px = o->x, py = o->y;
    obj_run_script(O, o);
    obj_move_table(O, o);
    switch (o->v2a) {
    case 1: case 0xb:
        obj_ground_follow(O, o);
        if (o->d[2] <= o->z) goto done;
        if (obj_attr_flags(O, I(o->x), I(o->y), I(o->z)) & 0x20) { o->d[2] -= 0x800000; goto done; }
        o->z = o->d[2]; o->facing = (uint8_t)~o->facing;
        obj_set_anim(O, o, 4); o->v2a = 0;
        goto done;
    case 2: case 0xc:
        obj_ground_follow(O, o);
        if (o->z <= o->d[2]) goto done;
        if (obj_attr_flags(O, I(o->x), I(o->y), I(o->z)) & 0x10) { o->d[2] += 0x800000; goto done; }
        o->z = o->d[2]; o->facing = (uint8_t)~o->facing;
        obj_set_anim(O, o, 4); o->v2a = 0;
        goto done;
    case 3:
        obj_ground_follow(O, o);
        goto done;
    default:
        if (!obj_ground_follow(O, o)) {
            o->x = px; o->y = py - 0x80000; obj_drop(O, o);
            obj_set_anim(O, o, 7);
        } else if (en_hits_player(O, o, 1)) {
            o->v2a = 3; player_damage(P_, 1, o->x, o->y, o->z, 0);
            obj_set_anim(O, o, 0x16);
        } else {
            uint16_t f = (uint16_t)wall_ahead(O, o, 0x10, 0, 6);
            if (f & 2) { o->v2a = 3; obj_set_anim(O, o, 0x16); }
            else if (f & 4) { o->x = px; o->y = py; obj_set_anim(O, o, 7); }
        }
    }
done:
    mirror(o);
    if (iabs(I(o->x) - O->camx) > 0x1000) obj_kill(O, o, 0);
}

/* ---------------------------------------------------------------- harpy fight (0x3c / 0x3d) and the prey (0x39-0x3b) */

static int count_food(Objects *O)
{
    int n = 0;
    for (int i = 0; i < OBJ_MAX; i++) if (O->obj[i].used && O->obj[i].ai == AI5_FOOD) n++;
    return n;
}

static void foodspawn_tick(Objects *O, Object *o)                  /* FUN_100119a0 (the THHAFOOD bank is loaded = state 3) */
{
    int n = count_food(O);
    o->count126 = (int16_t)n;
    if (o->timer-- >= 0) return;
    int k = rnd(O, 0xc);
    if (n > 0x13) { o->timer = (int16_t)(rnd(O, 0x14) + 0x14); return; }
    o->timer = (int16_t)(n < 0xb ? rnd(O, n < 6 ? 10 : 0x14) : rnd(O, 0x14) + 0x14);
    uint32_t e = 0x10052100u + 8u * (uint32_t)k;
    Object *f = obj_spawn(O, pe_u16(O->pe, e + 6), FX(pe_u16(O->pe, e)), FX(pe_u16(O->pe, e + 2)), FX(pe_u16(O->pe, e + 4)));
    if (f) f->spawn = -1;
}

static void harpyctl_tick(Objects *O, Object *o)                   /* FUN_10011ac0 */
{
    (void)o;
    const Obj *p = &P_->o;
    if (boss_state == 0 && !harpy_done) {
        if (p->x > 0x23000000 && p->x < 0x2a000000 && p->y > 0x4000000) {
            boss_state = 2; boss_bar = 0xff; O->hud_boss_cmd = 2;   /* DAT_10072188 = 2: the boss bar fades in */
        }
    } else if (boss_state && boss_bar <= 0) harpy_done = 99;
    if (!harpy_done) {
        int alive = 0;
        for (int i = 0; i < OBJ_MAX; i++) {
            const Object *h = &O->obj[i];
            if (h->used && h->type == 0x38 && h->v2a < 0xe) alive++;
        }
        if (alive < 3)
            for (int i = 0; i < OBJ_MAX; i++) {
                Object *h = &O->obj[i];
                if (h->used && h->type == 0x38 && h->v2a == 0xf) { h->hp = 3; h->v2a = 4; }   /* FUN_10010a40: back into the fight */
            }
        return;
    }
    enemies3_set_harpies_done(1);
    O->hud_boss_cmd = 7;                                            /* the bar goes away */
    for (int i = 0; i < OBJ_MAX; i++) {
        Object *h = &O->obj[i];
        if (!h->used || h->type != 0x38) continue;
        if (h->v2a == 0xf) { obj_kill(O, h, 1); continue; }
        if (h->v2a < 0xc) {                                         /* FUN_10011300: flee back up */
            h->v[0] = h->v[4] = h->v[8] = 0; h->v2a = 0xc;
            h->v[1] = 0xf0000; h->v[2] = 0xb000; h->v[3] = 0xa000;
            h->v[5] = 0x20000; h->v[6] = 0x6000; h->v[7] = 0x4000; h->b95 = 0xff;
            h->v[9] = 0xf0000; h->v[10] = 0xb000; h->v[11] = 0xa000; h->zface = 0xff;
            h->d[0] = h->home[0]; h->d[1] = 0x800000; h->d[2] = h->home[2];
            obj_set_anim(O, h, 9);
        }
    }
    obj_kill(O, o, 1);
}

static int food_init(Objects *O, Object *o)                        /* FUN_10011c10 / c70 / cd0 -> FUN_10011d30 */
{
    o->ai = AI5_FOOD;
    first_anim(O, o);
    o->movei = 0; o->moving = 1;
    if (!(o->z & 0x800000)) o->z -= FX(rnd(O, 0x18)); else o->z += FX(rnd(O, 0x30));
    o->facing = o->x < 0x25000000 ? 0 : 0xff;
    obj_drop(O, o);
    o->v2a = 0; o->b95 = 0xff;
    o->v[5] = 0x60000; o->v[4] = 0; o->v[6] = 0; o->v[7] = 0x6000;
    o->movei = rnd(O, 4) + 2;
    int pal = pe_ptr(O->pe, 0x10052b38, 8)[rnd(O, 8)];
    o->palx = pal == 0xff ? 0 : (uint8_t)(pal + 1);
    o->flags50 |= 0x80000;
    o->s9a = (int16_t)((rnd(O, 2) + 0x146) | 0x5800);               /* its scream */
    o->flags54 |= 8;
    return 1;
}

static void food_tick(Objects *O, Object *o)                       /* FUN_10011df0 */
{
    int32_t px = o->x, py = o->y;
    obj_run_script(O, o);
    if (o->v2a == 0) {
        en_move4(O, o);
        if (!obj_ground_follow(O, o) ||
            (obj_attr_flags(O, I(o->x) + (o->facing ? -0x10 : 0x10), I(o->y), I((o->z + 0x400000) & ~0x7fffff)) & 4)) {
            o->x = px; o->y = py; obj_set_anim(O, o, 2);
        }
    } else if (o->v2a == 1) {
        Object *h = o->carrier;
        if (!h || !h->used || h->victim != o) {                     /* let go: fall */
            o->carrier = NULL;
            o->v2a = 2; obj_set_anim(O, o, 8);
            int32_t z = (o->z + 0x400000) & 0xf800000;
            o->z = z;
            if (z == 0 && o->y > 0x45e0000) o->y = 0x45e0000;
            if (z == 0x800000 && o->y > 0x3de0000) o->y = 0x3de0000;
            obj_sound(O, (uint16_t)((o->s9a + 1) & 0xfffe), o->x);
        } else {                                                    /* FUN_10031000: hang from the harpy's grip point */
            o->x = h->x; o->y = h->y + 0x200000; o->z = h->z;
        }
    } else if (o->v2a == 2) {
        o->y += o->v[4]; o->v[4] += 0x6000; if (o->v[4] > 0x60000) o->v[4] = 0x60000;
        int up, down;
        level_probe(O->L, I(o->x), I(o->y), I(o->z), &up, &down, NULL);
        if (down == 0 || (up != 0x7fff && up < 0x20)) {            /* FUN_1002bc50 landed */
            if (up != 0x7fff && up < 0x20) o->y -= FX(up);
            o->v[4] = 0; o->v2a = 3; obj_set_anim(O, o, 10);
            o->z = (o->z + 0x400000) & ~0x7fffff;
        }
    }
    mirror(o);
    int x = o->x;
    if (o->z < 0x7f0000) { if (x > 0x2a700000 || x < 0x22a00000) obj_kill(O, o, 1); }
    else if (x > 0x28f00000 || x < 0x24100000) obj_kill(O, o, 1);
}

Object *leftovers_food_near(Objects *O, const Object *h)          /* FUN_10010e70: the group 0x55 search */
{
    for (int i = 0; i < OBJ_MAX; i++) {
        Object *f = &O->obj[i];
        if (!f->used || f->ai != AI5_FOOD || f->carrier || f->v2a != 0) continue;
        if (!(h->z < 0x400001 || f->z >= 0)) continue;
        if (!(h->z > 0x3fffff || f->z < 0x800001)) continue;
        if (iabs(I(f->x - h->x)) < 0x61) return f;
    }
    return NULL;
}

/* ---------------------------------------------------------------- Thebes 0x3f / 0x40 THCHAR walkers and their shadows */

static int char_init(Objects *O, Object *o, int dir)               /* FUN_10035c60 / FUN_10035cf0 */
{
    o->ai = AI5_CHAR;
    first_anim(O, o);
    if (dir) obj_set_anim(O, o, 1);
    o->movei = 0; o->moving = 1;
    o->flags54 |= 8;
    int pal = pe_ptr(O->pe, 0x100602b4, 4)[rnd(O, 4)];
    o->palx = pal == 0xff ? 0 : (uint8_t)(pal + 1);
    o->y = 0x4e00000;
    o->movei = rnd(O, 0xe) + 1;
    o->zface = dir ? 0xff : 0;
    o->flags50 |= 0x800;
    o->home[0] = o->x;
    o->v2a = dir ? 2 : 1;
    o->b23 = 8;
    Object *s = obj_spawn(O, 0x202e, o->x, o->y, o->z);            /* FUN_10027cd0: a shadow child */
    if (s) { s->spawn = -1; s->parent = o; s->ai = AI5_SHADOW; s->kind = K_ENEMY; }
    return 1;
}

static void char_tick(Objects *O, Object *o, int sync)             /* FUN_10035d70 */
{
    if (o->anim == 0 && o->frame && (o->movei < 7)) {}
    obj_run_script(O, o);
    if (o->v2a) obj_move_table(O, o);
    int z = I(o->z);
    /* FX3: FUN_10035d70 sets flags54 | 0x105 (semi, abr 1, dissolve) and +0x4a near the camera, and clears
     * 0x107 otherwise; draw mode 8 (FUN_100310e0) turns that into the two-pass dissolve. */
    if (z < -0x30) { int b = z * 2 + 0xe0; o->bright = (int16_t)(b < 0 ? 0 : b); o->flags54 |= 0x105; o->semi = 0; }
    else { o->bright = 0x80; o->flags54 &= 0xfffffef8u; o->semi = 0; }
    if (sync == 0 && o->v2a == 0) { o->v2a = 1; if (o->anim > 1) obj_set_anim(O, o, o->anim - 2); }
    int stop = 0;
    if (o->zface == 0) {
        if (z > 0x180) o->x = FX(z - 0x180) + o->home[0];
        if (z < -0x6c) {
            o->z = 0x2400000; o->x = o->home[0];
            int m = rnd(O, 0xe) + 1; o->movei = m; o->frame = 0;
            o->v2a = m > 8 ? 3 : m > 4 ? 2 : 1;
        }
        stop = o->movei >= 0xb && z >= 0x51 && z <= 0x7f;
    } else {
        if (z > 0x180) o->x = o->home[0] - FX(z) + 0x1800000;
        if (z > 0x240) {
            o->z = -0x800000; o->x = o->home[0];
            int m = rnd(O, 0xe) + 1; o->movei = m; o->frame = 0;
            if (m > 8) o->v2a = 2; else if (m > 4) o->v2a = 3;
        }
        stop = o->movei >= 0xb && z <= -0x21 && z >= -0x3f;
    }
    if (stop && sync == 1) o->v2a = 0;                              /* they stop for the singers */
    if (o->v2a == 0 && o->anim < 2) obj_set_anim(O, o, o->anim + 2);
    if (en_hits_player(O, o, 1)) player_damage(P_, 2, o->x, o->y, o->z, 0);
}

static void shadow_tick(Objects *O, Object *o)                     /* FUN_10035fe0 */
{
    Object *p = o->parent;
    if (!p || !p->used) { obj_kill(O, o, 1); return; }
    o->x = p->x; o->y = p->y; o->z = p->z;
    obj_run_words(O, o);
    o->hidden = p->v2a == 0;
    o->semi = 1; o->bright = 0x20;
}

/* ---------------------------------------------------------------- spawners and zones */

static void spawner_tick(Objects *O, Object *o)                    /* FUN_100103c0 */
{
    Object *c = o->kid[0];
    if (c && (!c->used || c->type != (uint16_t)o->d[4])) c = o->kid[0] = NULL;
    if (O->L->index == 5 && O->mino_v2a == 9 && P_->o.x > 0x700000) {   /* the minotaur is beaten: the rocks go */
        if (c) obj_kill(O, c, 0);
        obj_kill(O, o, 1);
        return;
    }
    if (!c) {
        if (!en_offscreen(O, o, 0x200)) {
            if (en_offscreen(O, o, 0x30)) {
                Object *n = obj_spawn(O, (uint16_t)o->d[4], o->x, o->y, o->z);
                if (n) {
                    n->spawn = -1;
                    o->kid[0] = n; o->nkids = 1;
                    n->frame = (int16_t)pe_u16(O->pe, o->script + 4 + 2u * (uint32_t)rnd(O, 8));
                }
                if (o->d[5]) { Object *m = obj_spawn(O, (uint16_t)o->d[5], o->x, o->y, o->z); if (m) m->spawn = -1; }
            }
            if (!en_offscreen(O, o, 0x200)) return;
        }
    } else {
        if (!en_offscreen(O, o, 0x200) || !en_offscreen(O, c, 0x200)) return;
        obj_kill(O, c, 0);
    }
    obj_kill(O, o, 0);
}

static void rubble_tick(Objects *O, Object *o)                     /* FUN_100253d0 */
{
    if (o->cool == 0) { obj_kill(O, o, 0); return; }
    o->cool--;
    if (rnd(O, 6)) return;
    Object *r = obj_spawn(O, 0x4028, FX(rnd(O, 0xa0) - 0x50) + o->x, o->y - 0x1000000, o->z);
    if (!r) return;
    r->spawn = -1;
    r->frame = rnd(O, 2) ? 9 : 10;
    r->scale = (uint16_t)(rnd(O, 0xa0) + 0x40);
    r->facing = (uint8_t)-rnd(O, 2); r->zface = 0xff; r->b95 = 0xff;
    r->v[4] = 0; r->v[5] = (int32_t)(((rnd(O, 0x8000) & 0x1ff) + 0xa00) * 0x100); r->v[6] = 0; r->v[7] = rnd(O, 0x2000) + 0x4000;
    r->v[8] = r->v[9] = (rnd(O, 0x8000) & 0x1ff) << 8; r->v[10] = rnd(O, 0x1000); r->v[11] = 0;
    r->v[0] = r->v[1] = (rnd(O, 0x8000) & 0x1ff) << 8; r->v[2] = rnd(O, 0x1000); r->v[3] = 0;
    r->d[0] = rnd(O, 8) << 2; r->d[1] = rnd(O, 8) << 2; r->d[2] = rnd(O, 8) << 2;
    r->v2a = 0;
}

static void camzone_tick(Objects *O, Object *o)                    /* FUN_10036b90: raise the camera near a ledge */
{
    int lo = o->box88[0], hi = o->box88[1];                         /* +0x88 / +0x8a */
    int dx = iabs(I(o->x - P_->o.x)), dy = iabs(I(o->y - P_->o.y)), dz = iabs(I(o->z - P_->o.z));
    if (dz < 0x41 && dy < 0x81) {
        int d = dx;
        if (d < hi) O->cam_bias = d < lo ? -0x20 : ((lo - d) * 0x20) / (hi - lo) - 0x20;
        else O->cam_bias = -0x40;
        O->cam_bias_set = 1;
        if (en_offscreen(O, o, hi + 0x80)) obj_kill(O, o, 0);
    }
}

/* Nessus follower (FUN_10014e10): once the camera is left of the arena line it may not scroll further right. */
void leftovers_cam_lock(Objects *O, int32_t arena_x)
{
    if (O->camx < I(arena_x) + 0x50) O->cam_xmax = O->camx;
}

/* ---------------------------------------------------------------- dispatch */

int leftovers_init(Objects *O, Object *o, uint32_t init)
{
    switch (init) {
    case H_FLASH:                                                   /* FUN_10033fc0 */
        o->ai = AI5_FLASH; first_anim(O, o); o->cbid = 3; obj_drop(O, o); o->tag = 1; o->flags54 |= 8;
        return 1;
    case H_SNOW:                                                    /* FUN_10033910 */
        o->ai = AI5_SNOW; o->facing = 0xff; o->flags54 |= 8;
        first_anim(O, o); o->movei = 0; o->moving = -1; o->movei = 10;
        o->hp = (int16_t)(rnd(O, 0x32) + 0x1e); o->tag = 0; o->v2a = 0;
        obj_drop(O, o);
        {   Object *p = obj_spawn(O, 0x45, o->x, o->y, o->z);       /* FUN_10030180: the partner rides along */
            if (p) { p->spawn = -1; p->parent = o; p->facing = o->facing; } }
        return 1;
    case H_SNOW45:                                                  /* FUN_10033cc0 */
        o->ai = AI5_SNOW45; o->bright = 0xa0; o->semi = 1; o->hidden = 1; o->pairs = 0;
        return 1;
    case H_PUFF:                                                    /* FUN_10033e10 */
        o->ai = AI5_PUFF; o->bank = 2; o->cool = 6; o->bright = 0xe0; o->pairs = 0;
        o->script = 0x100554f0;
        o->b23 = 4; o->rot[1] = 0;                                  /* FX3: sprite mesh, FUN_10026700(0x1005e688) */
        o->mesh = 0x1005e688; o->mesh_n = 0;
        while (o->mesh_n < 64 && pe_s16(O->pe, o->mesh + 16u * (uint32_t)o->mesh_n + 14) != -1) o->mesh_n++;
        return 1;
    case H_EWGUY:                                                   /* FUN_10034de0 */
        o->ai = AI5_EWGUY; o->cbid = 1; o->zface = 0xff; o->flags54 = 8;
        first_anim(O, o); o->movei = 0; o->moving = 1; o->d[2] = o->z;
        obj_drop(O, o);
        return 1;
    case H_FOODSPAWN:                                               /* FUN_10011950 */
        o->ai = AI5_FOODSPAWN; o->flags54 = 0; o->flags50 = 0x81; o->tag = 0x55; o->frame = 0;
        { Object *z = obj_spawn(O, 0x2029, 0x26a00000, 0x4600000, 0); if (z) z->spawn = -1; }
        return 1;
    case H_HARPYCTL:                                                /* FUN_10011a80 */
        o->ai = AI5_HARPYCTL; o->flags54 = 0; o->flags50 = 0x81; o->tag = 0x33; o->frame = 0;
        O->harpy_slot = 0;
        enemies3_set_harpies_done(0);
        return 1;
    case H_FOOD39: case H_FOOD3A: case H_FOOD3B:
        return food_init(O, o);
    case H_CHAR3F: return char_init(O, o, 0);
    case H_CHAR40: return char_init(O, o, 1);
    case H_SHADOW:                                                  /* FUN_10035fb0 */
        o->ai = AI5_SHADOW; o->semi = 1; o->bright = 0x20; o->pairs = 0;
        return 1;
    case H_SPAWNER:                                                 /* FUN_10010380 */
        o->ai = AI5_SPAWNER; o->frame = 0; o->flags50 |= 0x40000000;
        o->d[4] = pe_u16(O->pe, o->script); o->d[5] = pe_u16(O->pe, o->script + 2);
        o->kid[0] = NULL;
        return 1;
    case H_RUBBLE:                                                  /* FUN_100253b0 */
        o->ai = AI5_RUBBLE; o->flags54 = 0; o->cool = 0x3c; o->frame = 0;
        return 1;
    case 0x1000c8c0:                                                /* Forest 0x54: Nessus' defeat pose (FUN_1000c8c0 / c910) */
        o->ai = AI5_SCRIPTONLY; o->flags50 = 2; first_anim(O, o);
        o->facing = 0xff; o->flags54 |= 0x15; o->z = P_->o.z - 0x140000; o->flags50 |= 0x20000;
        return 1;
    case H_CAMZONE:                                                 /* FUN_10036b60 */
        o->ai = AI5_CAMZONE; o->frame = 0; o->flags50 = 2; o->box88[0] = 0xa0; o->box88[1] = 200;
        return 1;
    }
    return 0;
}

int leftovers_tick(Objects *O, Object *o)
{
    extern int leftovers_sync(void);
    switch (o->ai) {
    case AI5_FLASH: flash_tick(O, o); return 1;
    case AI5_SNOW: snow_tick(O, o); return 1;
    case AI5_SNOW45: snow45_tick(O, o); return 1;
    case AI5_PUFF: puff_tick(O, o); return 1;
    case AI5_EWGUY: ewguy_tick(O, o); return 1;
    case AI5_FOODSPAWN: foodspawn_tick(O, o); return 1;
    case AI5_HARPYCTL: harpyctl_tick(O, o); return 1;
    case AI5_FOOD: food_tick(O, o); return 1;
    case AI5_CHAR: char_tick(O, o, leftovers_sync()); mirror(o); if (en_offscreen(O, o, 0x300)) obj_kill(O, o, 0); return 1;
    case AI5_SHADOW: shadow_tick(O, o); return 1;
    case AI5_SPAWNER: spawner_tick(O, o); return 1;
    case AI5_RUBBLE: rubble_tick(O, o); return 1;
    case AI5_CAMZONE: camzone_tick(O, o); return 1;
    case AI5_SCRIPTONLY: obj_run_script(O, o); return 1;          /* FUN_1000c910 */
    }
    return 0;
}

int leftovers_callback(Objects *O, Object *o, int id)
{
    (void)O; (void)id;
    return o->ai > AI5_BASE;
}

void leftovers_frame(Objects *O)
{
    if (getenv("LEFT_LOG")) {
        static int t; if (++t % atoi(getenv("LEFT_LOG")) == 0) {
            int food = 0, carried = 0, harp = 0;
            for (int i = 0; i < OBJ_MAX; i++) { const Object *o = &O->obj[i]; if (!o->used) continue;
                if (o->ai == AI5_FOOD) { food++; if (o->carrier) carried++; } if (o->type == 0x38) harp++; }
            printf("  leftovers t=%d food %d carried %d harpies %d boss %d bar %d done %d\n", t, food, carried, harp, boss_state, boss_bar, harpy_done);
        }
    }
    if (boss_state && O->hud_boss_damage > 0) boss_bar -= O->hud_boss_damage;   /* DAT_10070832 follows the bar */
}
