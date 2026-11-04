/* NESSUS: the Nessus fight at the end of the Forest level (game level 4 is not a separate map) and the
 * Forest's real ending. Ported from ENG1.DLL:
 *
 *   type 0x09 "follower"   init FUN_10014de0, update2 FUN_10014e10: arms the fight (DAT_1006ef14) when
 *                          Hercules passes it, then follows him and applies the ground-shake damage
 *                          (DAT_10072178) when Nessus pounds the wall.
 *   type 0x53 trigger      init FUN_1000c780, update2 FUN_1000c7e0: once armed and the Nessus resources
 *                          are in (DAT_1006ef38 1 -> 5, FUN_1000e670 / FUN_1000ebd0 stream loaders), spawns
 *                          Nessus 0x140 ahead of Hercules (FUN_1000c730).
 *   type 0x2f Nessus       init FUN_1000bb10, update2 FUN_1000bc50, throw FUN_1000bb90.
 *   player state 0xd       FUN_10019260 (riding Nessus), entered from the fall state FUN_10017400.
 *
 * The fight: Nessus walks up, punches / kicks (and pounds the wall, which shakes Hercules off his feet),
 * then backs off and offers his back (state 0x45, DAT_10072120 = 1). Hercules jumps onto it (player state
 * 0xd); while he rides (0x41) the boss bar drains 1 every other tick until Nessus bucks him off (0x42,
 * FUN_1000bb90). With the bar empty Nessus is dazed (0x43); a charged power punch (meter >= 0x65) then
 * defeats him (0x23) and FUN_10019240 (player_win) ends the level.
 *
 * Not ported: the PSX bank streaming calls (FUN_10003520 / FUN_10003870: the Nessus banks are always
 * resident here), the camera right-edge lock (DAT_10070ce8 = DAT_10070ca0), the 0x2024 child effect of
 * FUN_10027cd0 and the Nessus music (CD audio). */
#include "objects_int.h"
#include "sfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { H_FOLLOW = 0x10014de0, H_TRIGGER = 0x1000c780, H_NESSUS = 0x1000bb10 };
/* AUD: was 300..302, colliding with training.c AI3_PHIL/METER/TGT30: nessus_tick ran first and hijacked Phil + the strength meter */
enum { AIN_FOLLOW = 250, AIN_TRIGGER, AIN_NESSUS };
#define HUD_BOSS_START_FADE 5   /* DAT_10072188 = 5 */
#define P_ (O->P)

static struct {
    const Objects *owner;
    int ef10, ef14, ef38;       /* trigger active, fight armed, resource loader state */
    Object *nes;                /* DAT_10072118 */
    int offer;                  /* DAT_10072120: 1 = back offered, 2 = Hercules jumped on */
    int alt;                    /* DAT_100721b4: punch / kick alternation */
    int wallhit;                /* DAT_10072178 */
    int bar;                    /* DAT_10070832: boss bar value */
    int ef4c, ef_state;         /* follower: y offset growth (DAT_1006ef4c), +0x9a */
    int peak;                   /* charged-punch meter during the current power punch */
    long tick;                  /* gamestruct+0x38 frame counter */
} N;

static int iabs(int v) { return v < 0 ? -v : v; }

void nessus_reset(const Objects *O)
{
    memset(&N, 0, sizeof N);
    N.owner = O;
}

static void mirror(Object *o) { o->flags54 = (o->flags54 & ~0x10u) | (o->facing & 0x10); }

/* FUN_1002a880(o, b, k): frame collision box k in world units (index 0 = the body box, as wbox in enemies2.c) */
static int box(Objects *O, const Object *o, int k, int b[6]) { return obj_box(O, o, k < 1 ? 1 : k, b); }

static int wall_at(Objects *O, int x, int y, int z)      /* FUN_10020d30, flag 2 of the fixed table DAT_1004f1e0 */
{
    int a = level_attr(O->L, x, y, z);
    return a > 0 && a < 256 && (pe_u16(O->pe, 0x1004f1e0 + 2u * (uint32_t)a) & 2);
}

static int stop_flags(Objects *O, int x, int32_t y, int32_t z)    /* DAT_100713bc[FUN_1002a170] & 2 */
{
    return obj_attr_flags(O, x, I(y), I(z)) & 2;
}

static void spawn_nessus(Objects *O, Object *trig)       /* FUN_1000c730 */
{
    trig->v2a = 0x52; trig->w[0] = 0x3c;
    obj_spawn(O, 0x2f, P_->o.x + 0x1400000, trig->y, trig->z);
}

/* ---------------------------------------------------------------- type 0x09 follower */

static void follow_init(Objects *O, Object *o)           /* FUN_10014de0 */
{
    (void)O;
    o->ai = AIN_FOLLOW;
    o->d[0] = o->x; o->d[1] = o->y;
    o->v2a = 0; N.ef_state = 0;
}

static void follow_tick(Objects *O, Object *o)           /* FUN_10014e10 */
{
    Obj *p = &P_->o;
    if (o->v2a == 0 && o->x < p->x && o->y < p->y) {
        N.ef14 = 1;
        o->v2a = 1; N.ef_state = 0;
        N.ef4c = 0;
    }
    if (o->v2a != 1) return;
    leftovers_cam_lock(O, o->d[0]);   /* MERGE: DAT_10070ce8 right-edge lock (FUN_10014e10) */
    if (N.wallhit) {
        N.wallhit = 0;
        switch (P_->state) {                             /* airborne / hanging / dying states are spared */
        case 2: case 4: case 7: case 9: case 0x10: case 0x13: case 0x16: case 0x17: case 0x19: case 0x1b: break;
        default:
            if (N.nes) player_damage(P_, 0x22, N.nes->x, N.nes->y, N.nes->z, 0);
        }
    }
    int32_t target = (int32_t)((uint32_t)((0x3000000 - p->y) >> 1) & 0xffff0000u);
    if (N.ef_state == 0) {
        if (target < 0) target = 0;
        if (N.ef4c < target) N.ef4c += 0x20000; else N.ef_state = 1;
    } else N.ef4c = target;
    o->x = p->x; o->y = p->y + N.ef4c; o->z = p->z - 0x180000;
}

/* ---------------------------------------------------------------- type 0x53 trigger */

static void trigger_init(Objects *O, Object *o)          /* FUN_1000c780 */
{
    o->ai = AIN_TRIGGER;
    o->v2a = 0;
    if (N.ef10 == 0 && P_ && P_->o.x <= o->x) {
        N.ef10 = -1;
        if (N.ef14 == 0) { o->v2a = 0x37; return; }
        N.ef14 = 0x21;
        spawn_nessus(O, o);
        return;
    }
    if (P_) obj_kill(O, o, 0);                           /* FUN_10027260 */
}

static void trigger_tick(Objects *O, Object *o)          /* FUN_1000c7e0 */
{
    int32_t ahead = o->x - P_->o.x;
    switch (o->v2a) {
    case 0x37:
        if (ahead > 0x2000000) { obj_kill(O, o, 0); N.ef10 = 0; return; }
        if (N.ef38 == 0) {
            N.ef38 = 1;                                  /* FUN_1000e670 case 1: swap in the NESSUS sfx bank */
            sfx_load_bank(O->fs, 17);
            N.ef38 = 5;                                  /* banks are resident: loaders finish at once */
            o->v2a = 0x51;
        }
        break;
    case 0x51:
        if (N.ef38 == 5) {
            if (ahead > 0x2000000) { obj_kill(O, o, 0); N.ef10 = 0; N.ef38 = 0; return; }
            if (N.ef14 != 0) { N.ef38 = 6; spawn_nessus(O, o); }
        }
        break;
    case 0x52:
        /* FUN_10027260: gone; with the camera held in the arena the record never scrolls back into view */
        if (o->w[0]-- < 1) { obj_sound(O, 0x100, o->x); obj_kill(O, o, 1); N.ef10 = 0; }
        break;
    }
}

/* ---------------------------------------------------------------- Nessus */

static void nes_init(Objects *O, Object *o)               /* FUN_1000bb10 */
{
    o->ai = AIN_NESSUS;
    o->flags50 = 0x202;
    o->v2a = 0x37;
    o->w[0] = 2;
    o->facing = 0xff;
    o->flags54 |= 0x10;
    o->anim = 0; o->script = pe_u32(O->pe, o->anims); o->idx = -1; o->timer = 0;
    if (!o->script && o->anims) { o->anim = (int16_t)pe_u16(O->pe, o->anims + 4); o->script = pe_u32(O->pe, o->anims + 4u * (uint32_t)o->anim); }
    obj_run_script(O, o);
    N.nes = o;
    N.offer = 0; N.alt = 0;
    N.bar = getenv("NESSUS_WEAK") ? 0x20 : 0xff;          /* test hook: one ride empties the bar */
    O->hud_boss_cmd = HUD_BOSS_START_FADE;
}

static void bar_damage(Objects *O, int d)
{
    N.bar -= d; if (N.bar < 0) N.bar = 0;
    O->hud_boss_damage += d;
    O->boss_hp = N.bar; O->boss_hp_max = 0xff;
}

static void set_anim_run(Objects *O, Object *o, int a) { obj_set_anim(O, o, a); obj_run_script(O, o); }

/* Attack choice used by states 1 and 10 (DAT_1005d454 / DAT_1005d468 bank checks are always "loaded"). */
static void attack(Objects *O, Object *o)
{
    if (N.alt == 0) { o->v2a = 0x16; obj_set_anim(O, o, 10); }
    else { o->v2a = 0x46; obj_set_anim(O, o, 5); }
    obj_run_script(O, o);
    o->w[0] = obj_rnd(O, 3) + 1 + P_->difficulty;        /* gamestruct+0xe */
}

static void back_off(Objects *O, Object *o)              /* -> 0x44 */
{
    o->v2a = 0x44; o->v[0] = -0x30000;
    obj_set_anim(O, o, 0xb);
}

static void throw_player(Objects *O, Object *o)          /* FUN_1000bb90 */
{
    int32_t x, vx;
    if (o->facing == 0xff) { x = o->x + 0x440000; vx = -0x80000; }
    else { x = o->x - 0x440000; vx = 0x80000; }
    player_nessus_throw(P_, x, o->y - 0x3c0000, (uint8_t)~o->facing, vx);
}

static int nes_walk(Objects *O, Object *o)               /* states 10 / 0x44: step, undo if a stop is ahead */
{
    int b[6], blocked = 0;
    o->home[0] = o->x;
    if (o->facing == 0) {
        o->x += o->v[0];
        if (box(O, o, 1, b) && getenv("NESSUS_BOX")) printf("walk box1 x %d: %d..%d y %d..%d\n", I(o->x), b[0], b[3], b[1], b[4]);
        if (box(O, o, 1, b) && stop_flags(O, b[3], o->y, o->z)) { o->x -= o->v[0]; blocked = 1; }
    } else {
        o->x -= o->v[0];
        if (box(O, o, 1, b) && stop_flags(O, b[0], o->y, o->z)) { o->x += o->v[0]; blocked = 1; }
    }
    return blocked;
}

static void nes_tick(Objects *O, Object *o)               /* FUN_1000bc50 */
{
    Obj *p = &P_->o;
    int32_t d = o->x - p->x;
    int ad = iabs(d);
    int run_after = 1;                                    /* "break": run the script once more after the switch */
    switch (o->v2a) {
    case 1:
        if (P_->health < 1) { back_off(O, o); run_after = 0; break; }
        if (d < 0) {                                      /* Hercules to the right */
            if (o->facing != 0xff) {
                if (ad > 0x4fffff) { o->v2a = 10; o->v[0] = 0x60000; obj_set_anim(O, o, 2); }
                else attack(O, o);
            } else { obj_set_anim(O, o, 1); o->v2a = 9; }
        } else if (o->facing == 0) {
            set_anim_run(O, o, 1); o->v2a = 9;
        } else if (ad < 0x500000) attack(O, o);
        else { o->v2a = 10; o->v[0] = 0x60000; set_anim_run(O, o, 2); }
        break;
    case 9:
        if (!obj_run_script(O, o)) { run_after = 0; break; }
        o->v2a = 1; obj_set_anim(O, o, 0); mirror(o);
        break;
    case 10: {
        int blocked = nes_walk(O, o);
        if (!blocked && ad > 0x4fffff) {
            if (!obj_run_script(O, o)) { run_after = 0; break; }
            o->v2a = 1; obj_set_anim(O, o, 0);
            break;
        }
        attack(O, o); run_after = 0;
        break;
    }
    case 0x16: case 0x46: {
        if (!obj_run_script(O, o)) { run_after = 0; break; }
        int ax = o->facing == 0 ? I(o->x) + 0x40 : I(o->x) - 0x40;
        if (!wall_at(O, ax, I(o->y), I(o->z))) { o->v2a = 1; obj_set_anim(O, o, 0); break; }
        if ((d < 0 && o->facing == 0xff) || (d >= 0 && o->facing == 0)) o->w[0] = 0;   /* Hercules behind him */
        if (o->w[0]-- < 1) back_off(O, o);
        break;
    }
    case 0x23:
        if (o->idx == 10) o->z = p->z - 0x100000;
        if (!obj_run_script(O, o)) { run_after = 0; break; }
        player_win(P_);                                   /* FUN_10019240: the Forest is done */
        o->v2a = 0x25; obj_set_anim(O, o, 0xc);
        break;
    case 0x25:
        break;
    case 0x37:
        o->x -= 0x800000; obj_drop(O, o); o->x += 0x800000;
        obj_run_script(O, o);
        if (o->w[0]-- > 0) { run_after = 0; break; }
        o->v2a = 1; obj_set_anim(O, o, 0);
        break;
    case 0x41:                                            /* Hercules is riding: the bar drains */
        o->x += 0x20000;
        if (N.tick & 1) bar_damage(O, 1);
        run_after = 0;
        if (!obj_run_script(O, o)) break;
        o->v2a = 0x42; set_anim_run(O, o, 7);
        if (N.bar >= 1) N.alt = (N.alt - 1) & 1;
        break;
    case 0x42:
        if (o->idx == 7 && P_->state == 0xd) throw_player(O, o);
        if (!obj_run_script(O, o)) { run_after = 0; break; }
        o->v2a = 0x47; obj_set_anim(O, o, 1);
        break;
    case 0x43:                                            /* dazed: only a full power punch finishes him */
        obj_run_script(O, o);
        run_after = 0;
        if (!(P_->hitflags & HIT_POWER) || N.peak < 0x65) {
            if (o->w[0]-- < 0) { o->w[0] = obj_rnd(O, 0x1e) + 0x1e; obj_sound(O, 0x110, o->x); }
        } else {
            Box ab; int b[6];
            if (player_attack_box(P_, &ab) && box(O, o, 0, b)) {
                int a[6] = {ab.x0, ab.y0, ab.z0, ab.x1, ab.y1, ab.z1};
                if (obj_boxes_overlap(a, b)) {
                    o->v2a = 0x23; set_anim_run(O, o, 4);
                    sfx_stop(0x10f, 0);
                    obj_spawn(O, 0x54, o->x, o->y, o->z);
                    O->boss_hp = 0;
                }
            }
        }
        break;
    case 0x44: {
        nes_walk(O, o);
        run_after = 0;
        if (!obj_run_script(O, o)) break;
        o->v2a = 0x45; set_anim_run(O, o, 9);
        N.offer = 1;
        break;
    }
    case 0x45:
        run_after = 0;
        if (N.offer == 2) { N.offer = 0; o->v2a = 0x41; set_anim_run(O, o, 6); break; }
        if (!obj_run_script(O, o)) break;
        o->v2a = 1; set_anim_run(O, o, 0);
        N.alt = (N.alt - 1) & 1;
        break;
    case 0x47:
        run_after = 0;
        if (!obj_run_script(O, o)) break;
        if (N.bar < 1) {
            o->v2a = 0x43; set_anim_run(O, o, 8);
            obj_sound(O, 0x610f, o->x);
            o->w[0] = 10;
        } else { o->v2a = 1; obj_set_anim(O, o, 0); }
        break;
    default:
        run_after = 0;
        break;
    }
    if (run_after) obj_run_script(O, o);
    mirror(o);

    if (o->v2a != 0x24 && o->v2a != 0x23) {
        int ab[6];
        if (box(O, o, 2, ab)) {
            int pb[6];
            en_player_box6(P_, pb);
            if (obj_boxes_overlap(ab, pb)) player_damage(P_, 5, o->x, o->y, o->z, 0);
            else if ((N.tick & 3) == 0) {
                int ax = o->facing == 0 ? I(o->x) + 0x40 : I(o->x) - 0x40;
                if (wall_at(O, ax, I(o->y), I(o->z))) {    /* fist hits the wall: dust, shake, quake damage */
                    obj_spawn(O, 0x2027, o->x - 0x200000, FX(ab[1]), o->z - 0x100000);
                    P_->shake = 1;
                    N.wallhit = 1;
                    obj_sound(O, 0x24, o->x);
                }
            }
        }
    }
    /* FUN_10026e90(o, 0, 0): blows only spark; Nessus loses no hit points to them */
    if (en_took_hit(O, o) == 1 && en_blow_damage(O)) en_hit_sound(O, 0x10);

    /* player-side view of the offer (DAT_10072120 + box 3, read by the fall state) */
    P_->nes_offer = N.offer;
    P_->nes_box_ok = N.offer == 1 && box(O, o, 3, P_->nes_box);   /* FUN_1002a880(o, b, 3) */
    if (getenv("NESSUS_BOX") && N.offer == 1) {
        for (int k = 1; k <= 5; k++) { int bb[6]; if (obj_box(O, o, k, bb)) printf("  frame %d bank %d box%d x %d..%d y %d..%d\n", o->frame, o->bank, k, bb[0], bb[3], bb[1], bb[4]); }
    }
    if (getenv("NESSUS_BOX") && P_->nes_box_ok) printf("  offer box x %d..%d y %d..%d  player %d,%d state %x\n", P_->nes_box[0], P_->nes_box[3], P_->nes_box[1], P_->nes_box[4], I(p->x), I(p->y), P_->state);
    P_->nes_x = o->x; P_->nes_y = o->y;
}

/* ---------------------------------------------------------------- hooks */

int nessus_init(Objects *O, Object *o, uint32_t init)
{
    if (N.owner != O) nessus_reset(O);
    switch (init) {
    case H_FOLLOW: follow_init(O, o); return 1;
    case H_TRIGGER: trigger_init(O, o); return 1;
    case H_NESSUS: nes_init(O, o); return 1;
    }
    return 0;
}

int nessus_tick(Objects *O, Object *o)
{
    switch (o->ai) {
    case AIN_FOLLOW: follow_tick(O, o); return 1;
    case AIN_TRIGGER: trigger_tick(O, o); return 1;
    case AIN_NESSUS: nes_tick(O, o); return 1;
    }
    return 0;
}

int nessus_callback(Objects *O, Object *o, int id)
{
    (void)O; (void)id;
    return o->ai >= AIN_FOLLOW && o->ai <= AIN_NESSUS;
}

/* Before the object updates: frame counter, power-punch peak, player offer state when Nessus is gone. */
void nessus_frame(Objects *O)
{
    if (N.owner != O) nessus_reset(O);
    if (!O->P || O->L->index != 3) return;
    N.tick++;
    Player *P = O->P;
    N.peak = P->state == 0x2a ? (P->charge > N.peak ? P->charge : N.peak) : 0;
    if (P->nes_offer == 2) N.offer = 2;                    /* Hercules jumped onto the offered back */
    if (N.nes && !N.nes->used) N.nes = NULL;
    if (!N.nes) { P->nes_offer = 0; P->nes_box_ok = 0; }
    if (getenv("NESSUS_LOG") && N.nes && N.tick % atoi(getenv("NESSUS_LOG")) == 0)
        printf("  [nessus t=%ld] state %x anim %d idx %d x %d facing %d bar %d offer %d hp %d pstate %x px %d\n", N.tick,
               N.nes->v2a, N.nes->anim, N.nes->idx, I(N.nes->x), N.nes->facing, N.bar, N.offer, P->health, P->state, I(P->o.x));
}
