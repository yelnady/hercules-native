/* Enemy behaviours: ports of ENG1.DLL actor handlers (update2 = level handler table +8, called while
 * flags50 & 2) and their AI script callbacks (level table +0x34, script ops -10 / -11). See
 * analysis/objects_notes.md "Enemies".
 *
 * Shared toolkit used by every enemy:
 *   FUN_1002a9e0  off-screen test (screen x ± range)          -> offscreen()
 *   FUN_10029160  4-byte movement table {dx, dy}, dz = +0x98   -> move4()
 *   FUN_10026de0  "who hit me": player attack box vs body box 1 (0x80) or a projectile object -> took_hit()
 *   FUN_10019b40  player_hit_damage(), FUN_100266a0 player_damage()
 *   FUN_1002a880 / FUN_1002a7a0  frame box k vs the player's body box (+0x88)    -> hits_player()
 *   FUN_1002a710  touching the player with box 0 (= box 1)                       -> touching()
 *   FUN_1002ac10  distance record {|dx|, |dy|, |dz|, ...}                        -> dist()
 */
#include "objects_int.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum { AI_NONE, AI_THUG, AI_SKELETON, AI_MINOTAUR, AI_HARPY, AI_GENERIC, AI_PROJ, AI_DUMMY };

/* Init handlers (level handler table +0). */
enum {
    H_THUG_A = 0x10001040,      /* THUGS 0x26: callback 7, 9 hp */
    H_THUG_B = 0x10001310,      /* THUGS 0x27: callback 8, 5 hp, throws 0x4027 */
    H_SKELETON = 0x10035200,    /* SKELETON 0x3e: callback 4 */
    H_HARPY = 0x10010540,       /* HARPIES 0x38: callback 5 (simplified) */
    H_MINOTAUR = 0x10024270,    /* MINOTAUR 0x37 (simplified) */
    H_ROCKC = 0x10008660,       /* Forest rock centaur (generic) */
    H_WRESTLEC = 0x10009190,    /* Forest wrestling centaur (generic) */
    H_GRIFFON = 0x1000d760,     /* Forest griffon (generic) */
    H_DUMMY = 0x1003e6d0,       /* Training dummy 0x36 (TRAINING bank) */
};

/* Hostile actors without a ported handler, recognised by sprite bank (0x1005cee4 path, last component).
 * They get the generic behaviour below. Ambient banks (birds, fish, crowds, gods, Meg, Phil) are left alone. */
static const char *const HOSTILE_BANKS[] = {
    "ROCKC", "WRESTLEC", "Griffon", "HARPIES", "MINOTAUR",        /* Forest / Thebes */
    "RockT", "IceT", "LavaT", "TornadoT", "Harp", "Spy",           /* Titans */
    "SHARK", "TOPS",                                               /* Training targets */
    NULL
};

static int hostile_bank(Objects *O, int bank)
{
    if (bank <= 0) return 0;
    uint32_t p = pe_u32(O->pe, 0x1005cee4 + 0x14 * (uint32_t)bank - 4);
    const char *s = p ? (const char *)pe_ptr(O->pe, p, 1) : NULL;
    if (!s) return 0;
    const char *b = strrchr(s, '\\');
    b = b ? b + 1 : s;
    for (int i = 0; HOSTILE_BANKS[i]; i++) if (!strcmp(b, HOSTILE_BANKS[i])) return 1;
    return 0;
}

static int iabs(int v) { return v < 0 ? -v : v; }

/* ---------------------------------------------------------------- toolkit */

static int offscreen(Objects *O, const Object *o, int range)        /* FUN_1002a9e0 */
{
    int sx = 160 + I(o->x) - O->camx;
    return sx + range < 0 || sx - range > 0x13f;
}

static void move4(Objects *O, Object *o)                            /* FUN_10029160 */
{
    if (o->moving < 0 || !o->movetab) return;
    uint32_t e = o->movetab + 4u * (uint32_t)o->movei;
    if (!pe_ptr(O->pe, e, 4)) return;
    int dx = pe_s16(O->pe, e), dy = pe_s16(O->pe, e + 2);
    o->x += o->facing ? -FX(dx) : FX(dx);
    o->y += FX(dy);
}

static void player_box6(const Player *P, int b[6])
{
    Box pb;
    if (player_body_box(P, &pb)) { b[0] = pb.x0; b[1] = pb.y0; b[2] = pb.z0; b[3] = pb.x1; b[4] = pb.y1; b[5] = pb.z1; }
    else {
        b[0] = I(P->o.x) - 14; b[3] = I(P->o.x) + 14; b[1] = I(P->o.y) - 72; b[4] = I(P->o.y);
        b[2] = I(P->o.z) - 16; b[5] = I(P->o.z) + 16;
    }
}

/* Projectiles hitting enemies: player weapon objects (AI_PROJ) overlapping box 1. */
static Object *proj_hit(Objects *O, const int box[6])
{
    for (int i = 0; i < OBJ_MAX; i++) {
        Object *p = &O->obj[i];
        if (!p->used || p->ai != AI_PROJ || p->life <= 0) continue;
        int X = I(p->x), Y = I(p->y), Z = I(p->z);
        int pb[6] = {X - 10, Y - 10, Z - 24, X + 10, Y + 10, Z + 24};
        if (p->type == 0x4001) {    /* ENEMIES2: fireball stream segment box (FUN_1001a5a0): x +-8, y -8..+0x18, z +-0x10 */
            pb[0] = X - 8; pb[3] = X + 8; pb[1] = Y - 8; pb[4] = Y + 0x18; pb[2] = Z - 0x10; pb[5] = Z + 0x10;
        }
        { int fb = fxw_proj_box(O, p, pb); if (fb == 0) continue; }   /* FXW: sonic orb: frame box 2 of the first orb only */
        if (obj_boxes_overlap(pb, box)) return p;
    }
    return NULL;
}

static int hit_log_on(void) { static int v = -1; if (v < 0) v = getenv("HIT_LOG") != NULL; return v; }   /* COMBATFIX */

/* FUN_10026de0: 0 = not hit, 1 = the player's blow (0x80), 2 = a projectile. */
static int took_hit(Objects *O, Object *o)
{
    int b[6];
    int have = obj_box(O, o, 1, b);
    if (!have && o->ai != AI_GENERIC) return 0;
    /* SOLIDS: dormant frames (rock centaur's rock) have no box or keep it under the floor.  COMBATFIX: the under-floor
     * substitute is only for those rocks; applying it to every object whose box legitimately hangs below its hotspot
     * (the training sacks, anything on a rope) replaced the real box by one ABOVE the hotspot, so blows never landed. */
    int rock = o->init_fn == 0x10008660 || o->init_fn == 0x100099e0;
    if (!have || (rock && b[4] > I(o->y) + 16)) {
        int X = I(o->x), Y = I(o->y), Z = I(o->z);
        b[0] = X - 28; b[3] = X + 28; b[1] = Y - 64; b[4] = Y; b[2] = Z - 32; b[5] = Z + 32;
    }
    Object *p = proj_hit(O, b);
    if (p) { if (p->variant != 2) p->life = 0; return 2; }     /* FUN_10030290: the projectile is spent */
    Box ab;
    if (player_attack_box(O->P, &ab)) {
        int a[6] = {ab.x0, ab.y0, ab.z0, ab.x1, ab.y1, ab.z1};
        if (hit_log_on()) printf("HIT_LOG obj type %x atk (%d,%d,%d)-(%d,%d,%d) body (%d,%d,%d)-(%d,%d,%d) %s\n", o->type, a[0], a[1], a[2], a[3], a[4], a[5], b[0], b[1], b[2], b[3], b[4], b[5], obj_boxes_overlap(a, b) ? "OVERLAP" : "miss");
        if (obj_boxes_overlap(a, b)) return 1;
    }
    return 0;
}

static int hits_player(Objects *O, const Object *o, int k)          /* FUN_1002a880(k) + FUN_1002a7a0 */
{
    int b[6], pb[6];
    if (!obj_box(O, o, k, b)) return 0;
    player_box6(O->P, pb);
    return obj_boxes_overlap(b, pb);
}

static int touching(Objects *O, const Object *o) { return hits_player(O, o, 1); }   /* FUN_1002a710 */

static void hurt_player(Objects *O, const Object *o, int level)
{
    player_damage(O->P, level, o->x, o->y, o->z, 0);
}

static void dist(Objects *O, const Object *o, int *dx, int *dy, int *dz)   /* FUN_1002ac10 */
{
    *dx = iabs(I(O->P->o.x - o->x)); *dy = iabs(I(O->P->o.y - o->y)); *dz = iabs(I(O->P->o.z - o->z));
}

static void hit_sound(Objects *O, int sword_id)                     /* FUN_1001ab50 / FUN_1001aba0 */
{
    if (O->P->hitflags & 0x18) obj_sound(O, sword_id, O->P->o.x);
    else obj_sound(O, obj_rnd(O, 0x8000) < 0x4000 ? 0x24 : 0x25, O->P->o.x);
}

/* Damage from the player's blow (FUN_10019b40 with ~DAT_10072168 & 0x5f). */
static int blow_damage(Objects *O) { return player_hit_damage(O->P, (uint16_t)(0x5f & ~O->P->consumed)); }

/* Pick-up dropped by a defeated enemy (FUN_100131c0 spawns a bouncing 0x4012 object; simplified to the
 * matching pickup type). */
static Object *drop_pickup(Objects *O, const Object *o, int id)   /* TITANS3: returns the pickup */
{
    /* ENEMIES3: FUN_100131c0 id rules; the pickup type is the +0x2a word of table 0x10052b90 (8 bytes / id) */
    int k = id & 0xf;
    if (k == 4 && O->L->index == 9) k = 3;
    if (k == 4) {
        int n = enemies3_counter(0), diff = O->P ? O->P->difficulty : 1;
        if ((diff == 0 && n > 1) || (diff > 0 && n != 0)) k = 3;
        if (n < 6) enemies3_counter_set(0, n + 1);
    }
    if (k == 10 && O->health_max == 0xff) k = 3;
    if (k == 9) {                                       /* the next password vase in order, then coins */
        int s = enemies3_counter(1);
        if (s < 2) { k = 5; s = 2; } else if (s == 2) { k = 6; s = 4; } else if (s == 4) { k = 7; s = 8; }
        else if (s == 8) { k = 8; s = 0x10; } else k = 2;
        enemies3_counter_set(1, s);
    }
    { int bit = k == 5 ? 1 : k == 6 ? 2 : k == 7 ? 4 : k == 8 ? 8 : 0; if (O->letters & bit) k = 2; }
    if (k == 1) O->coins14_total++;
    if (k == 2) O->coins18_total++;
    uint16_t t = pe_u16(O->pe, 0x10052b90 + 8u * (uint32_t)k);
    if ((t & 0xf000) != 0x2000) t = 0x2018;
    Object *p = obj_spawn(O, t, o->x, o->y - FX(0x20), o->z);
    if (p) { p->vy = -FX(4); p->ai = AI_NONE; }
    return p;
}

/* Count the attackers (v2a == 4) of the same behaviour - the group list of FUN_10029470. */
static int attackers(Objects *O, const Object *self)
{
    if (O->P->dying) return 999;
    int n = 0;
    for (int i = 0; i < OBJ_MAX; i++) {
        const Object *e = &O->obj[i];
        if (e->used && e != self && e->kind == K_ENEMY && e->ai == self->ai && e->v2a == 4) n++;
    }
    return n;
}

static void mirror(Object *o) { o->flags54 = (o->flags54 & ~0x10u) | (o->facing & 0x10); }

/* ---------------------------------------------------------------- init */

int enemy_init(Objects *O, Object *o, uint32_t init)
{
    o->facing = (O->spawn_flags & 0x1000) ? 0xff : 0;
    o->movei = 0; o->moving = 1;                                  /* FUN_100292c0 */
    o->anim = -1;
    if (fx_birds_init(O, o, init)) return 1;     /* FX: Stymphalian birds */
    if (nessus_init(O, o, init)) return 1;       /* NESSUS */
    if (leftovers_init(O, o, init)) return 1;  /* LEFTOVERS */
    if (enemies3_init(O, o, init) || enemies4_init(O, o, init)) return 1;   /* ENEMIES3 */
    if (enemies2_init(O, o, init)) return 1;     /* ENEMIES2: real AI ports */
    switch (init) {
    case H_THUG_A: case H_THUG_B:                                  /* FUN_10001040 / FUN_10001310 */
        o->ai = AI_THUG;
        o->variant = init == H_THUG_B;
        o->facing = obj_rnd(O, 2) ? 0xff : 0;
        o->flags50 |= 0x200; o->flags54 |= 8;
        { /* FUN_10029270: first animation of the list */
            o->anim = 0; o->script = pe_u32(O->pe, o->anims);
            if (!o->script) { o->anim = (int16_t)pe_u16(O->pe, o->anims + 4); o->script = pe_u32(O->pe, o->anims + 4u * (uint32_t)o->anim); }
            o->idx = -1; o->timer = 0;
        }
        o->movei = 2;
        o->cbid = o->variant ? 8 : 7;
        o->hp = o->variant ? 5 : 9;
        obj_drop(O, o);
        return 1;
    case H_SKELETON:                                               /* FUN_10035200 */
        o->ai = AI_SKELETON;
        o->flags54 |= 8;
        o->tag = O->spawn_flags & 0x3f;
        o->v2a = 0;
        obj_set_anim(O, o, 0x22);                                  /* buried, rises when Hercules is near */
        o->cool = 0;
        o->hp = 3; o->flags50 = 0x202;
        o->cbid = 4;
        o->inv = 0x3c;
        obj_drop(O, o);
        return 1;
    case H_MINOTAUR:                                               /* FUN_10024270 (behaviour simplified) */
        o->ai = AI_GENERIC; o->variant = 0;
        o->facing = 0xff; o->flags50 = 2;
        o->anim = 0; o->script = pe_u32(O->pe, o->anims); o->idx = -1; o->timer = 0;
        o->hp = 6 + 2 * (O->P ? O->P->difficulty : 1);
        o->moving = -1;
        obj_drop(O, o);
        return 1;
    case H_DUMMY:                                                  /* FUN_1003e6d0 */
        o->ai = AI_DUMMY;
        o->anim = 0; obj_set_anim(O, o, 0xd);
        o->v2a = 0; o->moving = -1; o->hp = 1;
        o->b95 = 0xff;                                             /* FUN_1003e6d0: a small hop, then it falls when released */
        o->v[5] = ((obj_rnd(O, 0x10000) & 0x1ff) + 0xe00) * 0x100;
        o->v[6] = 0; o->v[4] = o->v[5] >> 4;
        o->v[7] = obj_rnd(O, 0x2000) + 0x8000;
        return 1;
    case H_HARPY: case H_ROCKC: case H_WRESTLEC: case H_GRIFFON:  /* hittable, animated in place */
        o->ai = AI_GENERIC; o->variant = init == H_HARPY;
        o->flags50 |= 0x200;
        o->anim = 0; o->script = pe_u32(O->pe, o->anims); o->idx = -1; o->timer = 0;
        if (!o->script && o->anims) { o->anim = (int16_t)pe_u16(O->pe, o->anims + 4); o->script = pe_u32(O->pe, o->anims + 4u * (uint32_t)o->anim); }
        o->hp = 3;
        o->moving = -1;
        if (init != H_HARPY) obj_drop(O, o);
        return 1;
    }
    if (o->anims && hostile_bank(O, o->bank)) {                    /* other hostile actors: generic */
        o->ai = AI_GENERIC; o->variant = 0;
        o->anim = 0; o->script = pe_u32(O->pe, o->anims); o->idx = -1; o->timer = 0;
        if (!o->script) { o->anim = (int16_t)pe_u16(O->pe, o->anims + 4); o->script = pe_u32(O->pe, o->anims + 4u * (uint32_t)o->anim); }
        o->hp = 3; o->moving = -1;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- thugs */

/* FUN_100010f0 (0x26) / FUN_100013c0 (0x27). */
static void thug_tick(Objects *O, Object *o)
{
    if (offscreen(O, o, 0x17c)) { obj_kill(O, o, 0); return; }
    if (o->airborne) { obj_drop(O, o); goto attack; }
    obj_run_script(O, o);
    if (!o->used || o->v2a > 8) return;
    move4(O, o);
    int h;
    if (o->inv == 0 && (h = took_hit(O, o)) != 0) {
        if (h == 1) {
            int d = blow_damage(O);
            if (d) {
                hit_sound(O, 0x10);
                if (o->v2a < 8) { o->hp -= (int16_t)d; obj_set_anim(O, o, 0xf); }
                else obj_set_anim(O, o, 0x13);
            }
        } else if (o->v2a < 8) { o->hp -= 100; obj_set_anim(O, o, 0xf); }
        o->inv = 5;
    }
    if (o->v2a == 8) return;
    if (o->hp < 1) { obj_set_anim(O, o, 0x12); o->flags50 &= ~0x200u; O->kills++; return; }
    if (!obj_ground_follow(O, o)) { o->x = o->px; o->y = o->py; obj_set_anim(O, o, 1); }
    else {
        int ax = I(o->x) + (o->facing ? -0x30 : 0x30);
        if (obj_attr_flags(O, ax, I(o->y), I(o->z)) & 0xc2) { o->x = o->px; o->y = o->py; obj_set_anim(O, o, 1); }
    }
attack:
    if (o->v2a == 2) { if (touching(O, o)) obj_set_anim(O, o, 6); }
    else if (hits_player(O, o, 2)) hurt_player(O, o, (!o->variant && o->anim == 0xc) ? 0x23 : 3);
    mirror(o);
}

/* FUN_100015e0 (callback 7) and the shared part of FUN_10001910 (callback 8). */
static void thug_ai(Objects *O, Object *o)
{
    const Player *P = O->P;
    int B = o->variant;                         /* 0 = callback 7, 1 = callback 8 */
    int sdx = I(o->x - P->o.x), adx = iabs(sdx);
    uint16_t f = obj_attr_flags(O, I(o->x) + (o->facing ? 0x30 : -0x30), I(o->y), I(o->z));
    if (o->v2a > 2) {
        if (B && o->v2a == 5) {                 /* throw: spawn the 0x4027 projectile at the frame's emit point */
            Object *p = obj_spawn(O, 0x4027, o->x + (o->facing ? -FX(16) : FX(16)), o->y - FX(40), o->z);
            if (p) { p->kind = K_EFFECT; p->ai = AI_PROJ; p->variant = 1; p->facing = o->facing; p->vx = o->facing ? -FX(5) : FX(5); p->life = 1; p->cool = 60; }
        } else if (B && o->v2a == 9) {          /* pick the open side */
            uint16_t l = obj_attr_flags(O, I(o->x) - 0x40, I(o->y), I(o->z)), r = obj_attr_flags(O, I(o->x) + 0x40, I(o->y), I(o->z));
            if (!o->facing) { if (r & 0xc2) o->facing = 0xff; }
            else if (l & 0xc2) o->facing = 0;
        }
        mirror(o);
        return;
    }
    int n = attackers(O, o);
    if (iabs(I(o->z - P->o.z)) > 0x10) n = 99;
    int far1 = B ? 0x60 : 0x40;
    if (adx > far1 && obj_rnd(O, 9) == 0) { obj_set_anim(O, o, 0); return; }
    if (sdx < (B ? 0x80 : 0x56) && (f & 0xc2)) {
        if (!B) { if (obj_rnd(O, 2)) { obj_set_anim(O, o, 9); return; } obj_set_anim(O, o, 0xc); return; }
        obj_set_anim(O, o, obj_rnd(O, 2) ? 9 : 0xc); return;
    }
    /* facing away from Hercules: turn (anim 1) or attack if he is right behind */
    int away = (sdx < 0) ? (o->facing == 0xff) : (o->facing == 0);
    if (away) { obj_set_anim(O, o, adx > 0x13 ? 1 : 0x16); return; }
    if (!B) {
        if (n < 3) {
            if (adx < 0x10) { obj_set_anim(O, o, 0x16); return; }
            if (adx < 0x22) { obj_set_anim(O, o, 0x1b); return; }
        } else obj_set_anim(O, o, 0x16);
        if (adx < 0x2c) {
            if (n < 4) {
                if (obj_rnd(O, 3) == 0) { obj_set_anim(O, o, 0x1b); return; }
                obj_set_anim(O, o, obj_rnd(O, 3) ? 9 : 0x16); return;
            }
            if (obj_rnd(O, 3) == 0) { obj_set_anim(O, o, 0x1b); return; }
        } else if (adx > 0x55) {
            obj_set_anim(O, o, (o->anim == 4 || o->anim == 5) ? 5 : 4); return;
        } else if (n < 2) {
            if (adx > 0x32 && obj_rnd(O, 6) == 0) { obj_set_anim(O, o, 0x1e); return; }
            if (obj_rnd(O, 8) == 0) { obj_set_anim(O, o, 0x16); return; }
            if (obj_rnd(O, 6) == 0) { obj_set_anim(O, o, 0); return; }
            obj_set_anim(O, o, 0xc); return;
        } else {
            if (obj_rnd(O, 6) == 0) { obj_set_anim(O, o, 0x1e); return; }
            if (obj_rnd(O, 8) != 0) { obj_set_anim(O, o, 0); return; }
        }
        obj_set_anim(O, o, 0x16);
        return;
    }
    /* callback 8 (FUN_10001910) */
    if (adx < 0x2c) { obj_set_anim(O, o, 0x16); return; }
    if (adx < 0x4c) {
        if (n < 4 && obj_rnd(O, 6) == 0) { obj_set_anim(O, o, 0x16); return; }
    } else if (adx > 0x7f) {
        obj_set_anim(O, o, (o->anim == 4 || o->anim == 5) ? 5 : 4); return;
    } else if (n < 2) {
        if (obj_rnd(O, 6) == 0) obj_set_anim(O, o, 4);
        else if (obj_rnd(O, 8) == 0) obj_set_anim(O, o, 0x16);
        else obj_set_anim(O, o, obj_rnd(O, 6) == 0 ? 0 : 9);
        return;
    } else {
        if (obj_rnd(O, 6) == 0) { obj_set_anim(O, o, 4); return; }
        if (obj_rnd(O, 8) != 0) { obj_set_anim(O, o, 0); return; }
    }
    obj_set_anim(O, o, 0x16);
}

/* ---------------------------------------------------------------- skeletons */

/* FUN_100352f0 */
static void skeleton_tick(Objects *O, Object *o)
{
    if (o->cool) o->cool--;
    if (offscreen(O, o, 0x80)) { obj_kill(O, o, 0); return; }
    if (o->airborne) { obj_drop(O, o); goto attack; }
    obj_run_script(O, o);
    if (!o->used || o->hp < 1) return;
    move4(O, o);
    int h = took_hit(O, o);
    if (h && o->inv == 0) {
        if (h == 1) {
            int d = blow_damage(O);
            if (d) { hit_sound(O, 0x27a); o->hp -= (int16_t)d; obj_set_anim(O, o, 0x12); }
        } else { o->hp -= 100; obj_set_anim(O, o, 0x12); }
        o->inv = 0x28;
    }
    if (o->hp < 1) {
        obj_set_anim(O, o, 0x15);
        obj_spawn(O, 0x2027, o->x, o->y - FX(0x20), o->z);
        o->flags50 &= ~0x200u;
        O->kills++;
        return;
    }
    if (o->v2a > 1) {
        if (!obj_ground_follow(O, o)) { o->x = o->px; o->y = o->py; obj_set_anim(O, o, 1); }
        else if (obj_attr_flags(O, I(o->x), I(o->y), I(o->z)) & 2) { o->x = o->px; o->y = o->py; obj_set_anim(O, o, 1); }
    }
attack:
    if (o->v2a == 3 && touching(O, o)) obj_set_anim(O, o, 6);
    if (hits_player(O, o, 3) || hits_player(O, o, 2)) {
        hurt_player(O, o, 3);
        if (!o->cool) { obj_sound(O, 0x51dd, o->x); o->cool = 0x14; }
    }
    mirror(o);
}

/* Bone pieces thrown when a skeleton falls apart (table 0x1005ff60: 12-byte {dx, dy, dz, type, frame, facing}). */
static void skeleton_pieces(Objects *O, Object *o)
{
    for (uint32_t a = 0x1005ff60; pe_ptr(O->pe, a, 12); a += 12) {
        uint16_t type = pe_u16(O->pe, a + 6);
        if (!type) break;
        Object *p = obj_spawn(O, type, o->x + FX(pe_s16(O->pe, a)), o->y + FX(pe_s16(O->pe, a + 2)), o->z + FX(pe_s16(O->pe, a + 4)));
        if (!p) continue;
        int fc = pe_s16(O->pe, a + 10) & 0xff;
        p->facing = fc == 'i' ? o->facing : fc == 0x99 ? (o->facing ? 0 : 0xff) : (uint8_t)fc;
        p->kind = K_DEBRIS;
        p->frame = pe_s16(O->pe, a + 8);
        p->script = 0;
        p->vx = (p->facing ? -1 : 1) * (FX(1) + obj_rnd(O, FX(3)));
        p->vy = -FX(4) - obj_rnd(O, FX(3));
        p->life = 45;
        p->flags54 = (p->flags54 & ~0x10u) | (p->facing & 0x10);
    }
}

/* FUN_10035590 (callback 4) */
static void skeleton_ai(Objects *O, Object *o)
{
    const Player *P = O->P;
    int sdx = I(o->x - P->o.x), adx = iabs(sdx);
    int members = 0, n = 0;
    for (int i = 0; i < OBJ_MAX; i++) {
        const Object *e = &O->obj[i];
        if (!e->used || e == o || e->ai != AI_SKELETON || !e->v2a) continue;
        if (e->v2a < 10) members++;
        if (e->v2a == 4) n++;
    }
    if (P->dying) n = 999;
    int dx, dy, dz;
    dist(O, o, &dx, &dy, &dz);
    switch (o->v2a) {
    case 0:                                         /* buried: rise when Hercules is near and on screen */
        if (dz > 0x10 || dy > 0x10 || dx < 0x20) return;
        if (members > 0 && (unsigned)(obj_rnd(O, 3) + 2) <= (unsigned)members) return;
        if (dx > 0x100) return;
        if (!offscreen(O, o, -0x20)) obj_set_anim(O, o, 0x1f);
        else if (members == 0) obj_set_anim(O, o, 0);
        return;
    case 1: case 2: case 3: break;
    case 8:                                         /* falling apart */
        o->flags50 &= ~0x200u;
        if (o->tag) drop_pickup(O, o, o->tag - 1);
        skeleton_pieces(O, o);
        return;
    default: return;
    }
    if (adx > 0x40 && obj_rnd(O, 9) == 0) { obj_set_anim(O, o, 0); return; }
    int away = (sdx < 0) ? (o->facing == 0xff) : (o->facing == 0);
    if (away) { obj_set_anim(O, o, adx < 0x14 ? 0x19 : 1); return; }
    if (n < 3) {
        if (adx < 0x10) { obj_set_anim(O, o, 0x19); return; }
        if (adx < 0x22) { obj_set_anim(O, o, 0x1c); return; }
    } else obj_set_anim(O, o, 0x19);
    if (adx < 0x2c) {
        if (n > 4) { obj_set_anim(O, o, obj_rnd(O, 5) == 0 ? 0x1c : 0x19); return; }
        if (obj_rnd(O, 5) == 0) { obj_set_anim(O, o, 0x19); return; }
        if (obj_rnd(O, 2)) { obj_set_anim(O, o, 9); return; }
    } else if (adx > 0x55) {
        obj_set_anim(O, o, (o->anim == 4 || o->anim == 5) ? 5 : 4); return;
    } else if (n > 1) {
        if (obj_rnd(O, 6) == 0) { obj_set_anim(O, o, 0x1c); return; }
        obj_set_anim(O, o, obj_rnd(O, 8) ? 0 : 0x19); return;
    } else {
        if (adx > 0x3c && obj_rnd(O, 4) == 0) { obj_set_anim(O, o, 0xf); return; }
        if (obj_rnd(O, 10) == 0) { obj_set_anim(O, o, 0x19); return; }
        if (obj_rnd(O, 8) == 0) { obj_set_anim(O, o, 0x1c); return; }
    }
    obj_set_anim(O, o, 0xc);
}

/* ---------------------------------------------------------------- simplified enemies */

/* Harpies, minotaur, centaurs, griffons: their own handlers are not ported yet. They play their animation
 * list, take hits from Hercules (3 hp, harpy death anim 0x24), hurt him with attack boxes 2/3 and vanish
 * in a puff when defeated. */
static void generic_tick(Objects *O, Object *o)
{
    if (offscreen(O, o, 0x180)) { obj_kill(O, o, 0); return; }
    if (o->life > 0) {                              /* dying: flash, then puff */
        if (--o->life == 0) { obj_spawn(O, 0x2027, o->x, o->y - FX(0x20), o->z); obj_kill(O, o, 1); return; }
        if (o->variant == 1) o->y += FX(4);
        obj_run_script(O, o);
        return;
    }
    obj_run_script(O, o);
    if (!o->used) return;
    int h;
    if (o->inv == 0 && (h = took_hit(O, o)) != 0) {
        int d = h == 1 ? blow_damage(O) : 100;
        if (d) { hit_sound(O, 0x10); o->hp -= (int16_t)d; }
        o->inv = 0x14;
        if (o->hp < 1) {
            o->flags50 &= ~0x200u; O->kills++;
            if (o->variant == 1) obj_set_anim(O, o, 0x24);
            o->life = 24;
            return;
        }
    }
    if (hits_player(O, o, 2) || hits_player(O, o, 3)) hurt_player(O, o, 3);
}

/* ---------------------------------------------------------------- training dummy */

/* FUN_1003e770: stands (anim 0xd) until Hercules comes close (anim 0x17), breaks on the first blow:
 * anim 0xb, drops a coin (FUN_100131c0 id 2), floats up (state 9 -> 0x13, sound 0x4044) and is removed. */
static void dummy_tick(Objects *O, Object *o)
{
    obj_run_script(O, o);
    if (!o->used) return;
    if (getenv("DUMMY_LOG")) { int bb[6]; printf("box %d ", obj_box(O, o, 1, bb) ? bb[4] : -1); } if (getenv("DUMMY_LOG")) printf("dummy x %d y %d v2a %d anim %d frame %d inv %d\n", I(o->x), I(o->y), o->v2a, o->anim, o->frame, o->inv);
    if (o->v2a < 9) {
        int h = took_hit(O, o);
        { static int dt; if (getenv("DUMMY_BEAT") && ++dt > atoi(getenv("DUMMY_BEAT")) && iabs(I(o->x) - I(O->P->o.x)) < 160) h = 2; }   /* DOLLS test hook */
        if (h) {
            if (h == 1 && blow_damage(O)) hit_sound(O, 0x10);
            drop_pickup(O, o, 2);
            o->flags50 &= ~0x200u;
            obj_set_anim(O, o, 0xb);
            o->v2a = 9; o->inv = 0x1e;
            O->kills++;
        }
    }
    switch (o->v2a) {
    case 0: {
        int dx, dy, dz;
        dist(O, o, &dx, &dy, &dz);
        if (dx < 0x70 && dz < 0x90) obj_set_anim(O, o, 0x17);   /* widened from the original 0x20/0x20: drops when roughly under it */
        break;
    }
    case 1: {                                                      /* released: falls, bursts on the floor (FUN_1003e770 case 1) */
        int b[6], off = 0;
        if (obj_box(O, o, 0, b)) off = (b[4] << 16) - o->y;
        o->y += off; o->py += off;
        int r = enemies3_phys(O, o);
        if (r & 1) {
            obj_sound(O, 0x4040, o->x);
            obj_set_anim(O, o, 0xb);
            o->v2a = 9; o->inv = 0x1e;
        }
        o->y -= off; o->py -= off;
        if (hits_player(O, o, 0)) { hurt_player(O, o, 1); O->t_hits++; }
        break;
    }
    case 9:
        if (o->inv == 0) { o->v2a = 0x13; obj_sound(O, 0x4044, o->x); }
        /* fall through */
    case 0x13:
        o->y -= 0x20000;
        if (offscreen(O, o, 0x80) || I(o->y) < I(O->P->o.y) - 0x140) obj_kill(O, o, 1);
        break;
    }
    if (o->used && offscreen(O, o, 0x200)) obj_kill(O, o, 0);
}

/* ---------------------------------------------------------------- player projectiles / debris */

/* Projectiles: life = 1 while it can still hit, cool = ticks left to fly. */
static void proj_tick(Objects *O, Object *o)
{
    if (fxw_proj_tick(O, o)) return;    /* FXW: bolt (FUN_1001a0f0) and sonic orbs (FUN_1001a390) */
    o->x += o->vx; o->y += o->vy;
    if (o->script) { obj_run_script(O, o); if (!o->used) return; if (!o->script) o->script = 0; }
    if (o->variant == 1 && o->life > 0) {           /* enemy projectile (thrown by a thug) */
        int b[6] = {I(o->x) - 8, I(o->y) - 8, I(o->z) - 24, I(o->x) + 8, I(o->y) + 8, I(o->z) + 24}, pb[6];
        player_box6(O->P, pb);
        if (obj_boxes_overlap(b, pb)) { hurt_player(O, o, 3); obj_kill(O, o, 1); return; }
    }
    if (--o->cool <= 0 || o->life <= 0 || offscreen(O, o, 0x40)) obj_kill(O, o, 1);
}

void debris_tick(Objects *O, Object *o)
{
    o->x += o->vx; o->y += o->vy; o->vy += 0x8000;
    if (--o->life <= 0) obj_kill(O, o, 1);
}

/* Weapon effects requested by the player's scripts (P->spawns: 0x4005 lightning bolt, 0x4006 sonic orb)
 * and the fireball stream (P->flame). They are 0x4000-pool objects with their own scripts; here they fly
 * along the aim and destroy what they touch (-100 hp, as a projectile hit in FUN_10026de0). */
/* SOLIDS: the weapon effect types (0x4001 / 0x4005 / 0x4006) have no ported init handler, so obj_spawn
 * refuses them; create them directly from their type info so they exist and can hit enemies. */
void enemies_player_spawns(Objects *O) { fxw_spawns(O); }   /* FXW: moved to fxw.c (bolt / orbs / stream) */

/* ---------------------------------------------------------------- dispatch */

void enemy_tick(Objects *O, Object *o)
{
    if (fx_birds_tick(O, o)) return;            /* FX */
    if (nessus_tick(O, o)) return;              /* NESSUS */
    if (leftovers_tick(O, o)) return;           /* LEFTOVERS */
    if (enemies3_tick(O, o) || enemies4_tick(O, o)) return;   /* ENEMIES3 */
    if (enemies2_tick(O, o)) return;            /* ENEMIES2 */
    switch (o->ai) {
    case AI_THUG: thug_tick(O, o); break;
    case AI_SKELETON: skeleton_tick(O, o); break;
    case AI_GENERIC: generic_tick(O, o); break;
    case AI_DUMMY: dummy_tick(O, o); break;
    default: obj_run_script(O, o); break;
    }
}

void enemy_callback(Objects *O, Object *o, int id)
{
    if (nessus_callback(O, o, id)) return;      /* NESSUS */
    if (leftovers_callback(O, o, id)) return;   /* LEFTOVERS */
    if (enemies3_callback(O, o, id)) return;    /* FIX31: Phil */
    if (enemies4_callback(O, o, id)) return;    /* ENEMIES3 */
    if (enemies2_callback(O, o, id)) return;    /* ENEMIES2 */
    (void)id;
    switch (o->ai) {
    case AI_THUG: thug_ai(O, o); break;
    case AI_SKELETON: skeleton_ai(O, o); break;
    default: break;
    }
}

/* Projectiles are effects with ai == AI_PROJ; objects.c ticks K_EFFECT through its own path, so route them. */
void enemies_tick_proj(Objects *O, Object *o) { proj_tick(O, o); }

/* ---------------------------------------------------------------- ENEMIES2: toolkit shared with enemies2.c */

int en_offscreen(Objects *O, const Object *o, int range) { return offscreen(O, o, range); }    /* ENEMIES2 */
void en_move4(Objects *O, Object *o) { move4(O, o); }
int en_took_hit(Objects *O, Object *o) { return took_hit(O, o); }
extern long fxb_player_ticks;
int en_hits_player(Objects *O, const Object *o, int k)
{
    int r = hits_player(O, o, k);
    if (r && getenv("FXB_LOG")) printf("FXB pt=%ld en_hits_player type %x box %d OVERLAP\n", fxb_player_ticks, o->type, k);   /* FXB */
    return r;
}
void en_hurt_player(Objects *O, const Object *o, int level) { hurt_player(O, o, level); }
void en_hit_sound(Objects *O, int sword_id) { hit_sound(O, sword_id); }
int en_blow_damage(Objects *O) { return blow_damage(O); }
void en_drop_pickup(Objects *O, const Object *o, int id) { drop_pickup(O, o, id); }
Object *en_drop_pickup_obj(Objects *O, const Object *o, int id) { return drop_pickup(O, o, id); }   /* TITANS3 */
void en_player_box6(const Player *P, int b[6]) { player_box6(P, b); }
