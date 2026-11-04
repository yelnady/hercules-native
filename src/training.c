/* ENEMIES3: Phil's Training lessons. Ports of ENG1.DLL:
 *
 *   Phil 0x31                 init 1002cc50, update 1002cd30, route mover 1002d440, station jump 1002d3f0,
 *                             speech FUN_1002c4c0 (voice clip n = PC_<1000+n>.ESF)
 *   lesson stations           0x32 update 1003cf90 (stomp stage), 0x33 1003cb90, 0x35 1003d930,
 *                             0x39 1003d440 (carousel), 0x3e 1003dcd0 (pendulum stage); shared count FUN_1003eb30
 *   station table             0x10065678, 12-byte entries {x end, done bits, meter state, meter y, stage y,
 *                             gate state}; the original writes it at run time, so it is a mutable copy here
 *   strength meter 0x2f       10040e20 / 10040e50 (state restore FUN_10041000)
 *   targets                   0x30 / 0x4d 1003e220 / 1003e160 / 1003e280, 0x2a 1003e370 / 1003e3d0,
 *                             0x3a 1003e580 / 1003e5d0, carousel hub 0x43 1003d780 / 1003d7c0
 *   gate 0x3f                 1003ecb0, weight 0x44 / 0x45 1003eec0 (both opened by Phil's route ops 5 / 6)
 *   cannon 0x37               1003f1c0 (raise when on screen, spin, fire 0x4028 1003fa50 / 1003fb00)
 *   spinning tops 0x2b-0x2d   1003fe40 / 1003ff30, 0x4038 10040ba0 / 10040c10, 0x403d 1003fcf0 / 1003fe00
 */
#include "objects_int.h"
#include "solids.h"
#include "sfx.h"
#include "fx2.h"   /* FX2 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

enum {
    AI3_BASE = 300,
    AI3_PHIL, AI3_METER, AI3_TGT30, AI3_TGT4D, AI3_TGT2A, AI3_TGT3A, AI3_HUB, AI3_TOPS, AI3_BALL, AI3_LAUNCH, AI3_SHARD,
};

enum {
    H_PHIL = 0x1002cc50, H_METER = 0x10040e20, H_TGT30 = 0x1003e220, H_TGT2A = 0x1003e370, H_TGT3A = 0x1003e580,
    H_HUB = 0x1003d780, H_TOPS = 0x1003fe40, H_BALL = 0x1003fa50, H_LAUNCH = 0x10040ba0, H_SHARD = 0x1003fcf0,
    /* solids.c init handlers whose updates are ported here */
    S_CANNON = 0x1003f140, S_RACK = 0x1003ebc0, S_LIFT = 0x1003edf0,
    S_ST32 = 0x1003ce10, S_ST33 = 0x1003ca80, S_ST35 = 0x1003d800, S_ST39 = 0x1003d310, S_ST3E = 0x1003db90,
    STATION_TABLE = 0x10065678,
};

#define P_ (O->P)
static int iabs(int v) { return v < 0 ? -v : v; }
static int rnd(Objects *O, int n) { return obj_rnd(O, n); }
static void snd(Objects *O, int id, const Object *o) { obj_sound(O, id, o ? o->x : P_->o.x); }
static int16_t sintab(Objects *O, int a) { return pe_s16(O->pe, 0x1005a848 + 2u * (uint32_t)(a & 0x3ff)); }

/* ---------------------------------------------------------------- shared lesson state (ENG1 globals) */

static struct {
    uint16_t tbl[32][6];        /* 0x10065678 station table (mutable copy) */
    int nst;
    Object *eec;                /* DAT_10070eec: the station of Phil's current lesson */
    Object *ef8;                /* DAT_10070ef8: the gate / weight of Phil's current lesson */
    int eea;                    /* DAT_10070eea: Phil's lesson station */
    int ef2, f0c;               /* DAT_10070ef2 / DAT_10070f0c: station under Phil / under Hercules */
    int efe, efc;               /* DAT_10070efe (meter wants to move: 1 partial, 2 all done) / DAT_10070efc */
    int ee6;                    /* DAT_10070ee6: crank speed (Phil +0x4c) */
    int ef4, f00;               /* DAT_10070ef4 / DAT_10070f00: board frames */
    int f04, f0e, f0a, f08;     /* stomp count / targets beaten / meter prize taken / speech lock */
    int ee4, ed8, eda;          /* decay timers */
    int quiet;                  /* DAT_1007144c ticks without action */
    int edc, ee2, ee0;          /* speech cool-downs */
    int f02, ee8;               /* speech table indices */
    int rope_snd;               /* DAT_10070004: the crank sound is playing (value = its sound id) */
    int vase_seq, health_drops; /* DAT_100721a8 / DAT_10072190 (pickup drops, FUN_100131c0) */
    int on;                     /* reset done for this level */
} T;

void enemies3_reset(Objects *O)
{
    memset(&T, 0, sizeof T);
    T.eea = T.ef2 = T.f0c = -1;
    T.ef4 = 0x66; T.f00 = 0x83;
    T.ee4 = T.ed8 = T.eda = 10;
    for (int i = 0; i < 32; i++) {
        uint32_t a = STATION_TABLE + 12u * (uint32_t)i;
        for (int k = 0; k < 6; k++) T.tbl[i][k] = pe_u16(O->pe, a + 2u * (uint32_t)k);
        T.nst = i + 1;
        if (T.tbl[i][0] == 0xffff) break;
    }
    T.on = 1;
}

/* Station table word (field 0..5) for solids.c's inits (they read the run-time copy, not the file). */
uint16_t training_tbl(Objects *O, int st, int field)
{
    if (!T.on) enemies3_reset(O);
    if (st < 0 || st >= T.nst || field < 0 || field > 5) return 0;
    return T.tbl[st][field];
}

void training_tbl_set(Objects *O, int st, int field, uint16_t v)
{
    if (!T.on) enemies3_reset(O);
    if (st >= 0 && st < T.nst && field >= 0 && field <= 5) T.tbl[st][field] = v;
}

static int station_of(int X)                                        /* FUN_10041fd0 */
{
    for (int i = 0; i < T.nst; i++) {
        uint16_t e = T.tbl[i][0];
        if (X > 0 && X < e) return i;
        if (e == 0xffff) break;
    }
    return 0xff;
}

int enemies3_counter(int which) { return which == 0 ? T.health_drops : T.vase_seq; }
void enemies3_counter_set(int which, int v) { if (which == 0) T.health_drops = v; else T.vase_seq = v; }

/* ---------------------------------------------------------------- toolkit */

static int land(Objects *O, Object *o)                              /* FUN_10029e20 */
{
    int up, down;
    level_probe(O->L, I(o->x), I(o->y), I(o->z), &up, &down, NULL);
    int dy = iabs(I(o->y - o->py)), dx = iabs(I(o->x - o->px)), dz = iabs(I(o->z - o->pz));
    if (down == 0) return 1;
    if (up != 0x7fff && up <= (dy + dx + dz) * 2 + 16) { o->y -= FX(up); return 1; }
    return 0;
}

static int phys(Objects *O, Object *o)                              /* FUN_1002bd20 / FUN_1002bc50 */
{
    int32_t *v = o->v;
    int ret = 0;
    v[0] += v[2] - v[3]; if (v[0] < 0) v[0] = 0; if (v[0] > v[1]) v[0] = v[1];
    v[4] += v[6] - v[7]; if (v[4] < 0) v[4] = 0; if (v[4] > v[5]) v[4] = v[5];
    v[8] += v[10] - v[11]; if (v[8] < 0) v[8] = 0; if (v[8] > v[9]) v[8] = v[9];
    o->x += o->facing ? -v[0] : v[0];
    o->y += o->b95 ? -v[4] : v[4];
    o->z += o->zface ? -v[8] : v[8];
    if (v[4] == 0 && o->b95) { v[6] = v[7]; v[7] = 0; o->b95 = 0; v[3] = v[2]; v[2] = 0; }
    if (!o->b95 && land(O, o)) {
        v[4] >>= 1; v[7] = v[6]; v[6] = 0; o->b95 = 0xff; v[2] = v[3]; v[3] = 0;
        ret = v[4] == 0 ? 3 : 1;
    }
    return ret;
}

int enemies3_phys(Objects *O, Object *o) { return phys(O, o); }    /* FUN_1002bc50 for enemies.c (training dummy 0x36) */

static void vel(Object *o)                                          /* FUN_1002b4c0 */
{
    int32_t *v = o->v;
    v[0] += v[2] - v[3]; if (v[0] < 0) v[0] = 0; if (v[0] > v[1]) v[0] = v[1];
    v[4] += v[6] - v[7]; if (v[4] < 0) v[4] = 0; if (v[4] > v[5]) v[4] = v[5];
    v[8] += v[10] - v[11]; if (v[8] < 0) v[8] = 0; if (v[8] > v[9]) v[8] = v[9];
    o->x += o->facing ? -v[0] : v[0];
    o->y += o->b95 ? -v[4] : v[4];
    o->z += o->zface ? -v[8] : v[8];
}

static void mirror(Object *o) { o->flags54 = (o->flags54 & ~0x10u) | (o->facing & 0x10); }

/* FUN_10026de0 + FUN_10019b40: 0 = no hit, else damage (a projectile counts 100). */
static int hit_damage(Objects *O, Object *o)
{
    if (getenv("DOLL_LOG")) { int b[6]; Box ab; int have = obj_box(O, o, 1, b), at = player_attack_box(P_, &ab);
        if (at) printf("tgt %04x y %d box %s (%d,%d,%d)-(%d,%d,%d) atk (%d,%d,%d)-(%d,%d,%d)\n", o->type, I(o->y), have ? "ok" : "none", b[0], b[1], b[2], b[3], b[4], b[5], ab.x0, ab.y0, ab.z0, ab.x1, ab.y1, ab.z1); }
    int h = en_took_hit(O, o);
    if (getenv("DOLL_LOG") && h) printf("  took_hit %d\n", h);
    if (h == 2) return 100;
    if (h == 1) { int d = en_blow_damage(O); if (d) { en_hit_sound(O, 0x10); return d; } }
    return 0;
}

static int touching_player(Objects *O, const Object *o) { return en_hits_player(O, o, 1); }

static void tick_inv(Object *o) { if (o->inv) o->inv--; }

/* ---------------------------------------------------------------- targets */

static void tgt_beaten(Object *o, Objects *O)
{
    o->flags50 &= ~0x200u;
    obj_set_anim(O, o, 0xb);
    o->v2a = 9;
    T.f0e++;
}

static void tgt30_tick(Objects *O, Object *o)                      /* FUN_1003e160 (0x30) / FUN_1003e280 (0x4d) */
{
    obj_run_script(O, o);
    if (o->v2a == 9) return;
    if (!o->inv) {
        int d = hit_damage(O, o);
        if (d) { o->hp -= (int16_t)d; obj_set_anim(O, o, 0x14); o->inv = 10; if (getenv("DOLL_LOG")) printf("target %04x hit dmg %d hp %d\n", o->type, d, o->hp); }
    } else o->inv--;
    if (o->hp < 1) { tgt_beaten(o, O); return; }
    if (o->ai == AI3_TGT4D && !o->inv && touching_player(O, o)) {   /* only the 0x4d drums hurt */
        en_hurt_player(O, o, 1); O->t_hits++; T.eda = 0xc;
    }
}

static void tgt2a_tick(Objects *O, Object *o)                      /* FUN_1003e3d0: walks in depth */
{
    obj_run_script(O, o);
    obj_move_table(O, o);                 /* FUN_100291d0 (6-byte {dx, dy, dz} entries, z by zface), not FUN_10029160 */
    if (o->moving >= 0 && o->movetab) {   /* FX2: FUN_10002d80 - the conveyor texture (record +0x97) moves with the target */
        int v = pe_s16(O->pe, o->movetab + 4u + 6u * (uint32_t)o->movei);
        if (o->zface) v = -v;
        fx2_tex_scroll((Level *)O->L, o->tag, 0, v);
    }
    int Z = I(o->z);
    if (Z < -0x18) { o->z = -0x180000; obj_set_anim(O, o, 1); o->zface = 0xff; }
    if (Z > 0x130) { o->z = 0x1300000; obj_set_anim(O, o, 2); o->zface = 0; }
    if (o->v2a != 9) {
        if (o->inv) { o->inv--; return; }
        int d = hit_damage(O, o);
        if (d) {
            o->hp -= (int16_t)d;
            obj_set_anim(O, o, o->v2a == 1 ? 5 : 8);
            o->inv = 0x14;
            if (o->hp < 1) { tgt_beaten(o, O); return; }
        }
    }
    if (!o->inv && o->v2a != 9 && touching_player(O, o)) { en_hurt_player(O, o, 1); O->t_hits += 3; }
}

static void tgt3a_tick(Objects *O, Object *o)                      /* FUN_1003e5d0: rides the carousel */
{
    obj_run_script(O, o);
    if (o->v2a == 9) return;
    if (o->cool) o->cool--;
    if (o->inv) { o->inv--; return; }
    int d = hit_damage(O, o);
    if (d) { o->hp -= (int16_t)d; obj_set_anim(O, o, 0x14); o->inv = 10; }
    if (o->hp < 1) { tgt_beaten(o, O); return; }
    if (!o->inv && touching_player(O, o)) { en_hurt_player(O, o, 1); O->t_hits++; }
}

static void hub_tick(Objects *O, Object *o)                        /* FUN_1003d7c0: carousel hub turns */
{
    o->rot[1] = (int16_t)((o->rot[1] + ((o->d[7] & 0x800000) ? 8 : -8)) & 0x3ff);
    (void)O;
}

/* FUN_10041000: the meter's state comes back from the station table (respawn). */
static void meter_restore(Objects *O, Object *m, int st)
{
    if (st < 0 || st >= T.nst) return;
    if (T.tbl[st][3]) m->y = FX(T.tbl[st][3]);
    m->v2a = T.tbl[st][2];
    if (m->v2a == 0) m->v2a = 1;
    if (m->v2a > 7) m->v2a = 0x10;
    if (m->v2a == 0x10) { obj_set_anim(O, m, 0xe); return; }
    switch (m->v2a & 3) { case 0: case 1: obj_set_anim(O, m, 0); break; case 2: obj_set_anim(O, m, 1); break; case 3: obj_set_anim(O, m, 2); break; }
}

static void meter_tick(Objects *O, Object *o)                      /* FUN_10040e50: the prize on the strength meter */
{
    if (o->v2a < 8) {
        if ((o->v2a & 4) && touching_player(O, o)) {
            if (getenv("PRIZE_LOG")) printf("PRIZE touched: meter x %d y %d z %d player x %d y %d z %d\n", I(o->x), I(o->y), I(o->z), I(P_->o.x), I(P_->o.y), I(P_->o.z));   /* FIX32: verification */
            obj_set_anim(O, o, 0xd);
            snd(O, 0x406f, o);
            int st = station_of(I(o->x));
            uint8_t id = st != 0xff ? *pe_ptr(O->pe, 0x10066e88 + (uint32_t)st, 1) : 0;
            en_drop_pickup(O, o, id);
            Object *p = obj_spawn(O, 0x2027, o->x, o->y + 0x200000, o->z);
            if (p) p->spawn = -1;
            T.f0a = 1;
        }
        if (o->v2a < 8 && !o->inv) {
            int d = hit_damage(O, o);
            if (d) {
                o->v2a &= ~3;
                if (o->anim == 0) { obj_set_anim(O, o, 4); o->v2a |= 2; }
                else { obj_set_anim(O, o, 7); o->v2a |= 3; }
                o->inv = 0x28;
            }
        }
    }
    tick_inv(o);
    obj_run_script(O, o);
    mirror(o);
}

/* ---------------------------------------------------------------- stations */

static int count_done(Objects *O, Object *st, uint16_t type)       /* FUN_1003eb30 */
{
    int total = 0, done = 0, bits = 0, b = 1;
    for (int i = 0; i < st->nkids; i++) {
        Object *k = st->kid[i];
        if (!k || !k->used || k->type != type) continue;
        if (k->v2a == 9) { done++; bits |= b; }
        b <<= 1; total++;
    }
    (void)O;
    st->count126 = (int16_t)done; st->cbid = (int16_t)total;
    if (st->b21 < T.nst) T.tbl[st->b21][1] = (uint16_t)bits;
    return total != 0;
}

/* The common meter block of the station updates: the strength meter (0x2f) sinks with the share of beaten
 * targets while Phil cranks, and all the way (state |= 4: prize ready) once every target is beaten.
 * FIX31: the winch rope texture (FUN_10002d80(rec, 0, n)) scrolls with every step the meter sinks: rec 5 for the
 * stomp stage 0x32 / pendulum 0x3e, rec 0 for the others; n1 is the step of the first (efc 1) pass, the second is always 2. */
static void station_meter(Objects *O, Object *st, Object *m, int rec, int n1)
{
    int s = st->b21;
    Level *Lv = (Level *)O->L;
    if (st->cbid == st->count126) {
        if (s < T.nst) T.tbl[s][3] = (uint16_t)(I(m->d[0]) + m->d[2]);
        if (I(m->y - FX(m->d[2]) - m->d[0]) < 0) {
            T.efe = 2;
            if (T.efc == 1) { m->y += 0x20000; fx2_tex_scroll(Lv, rec, 0, n1); }
            if (T.efc == 1 || T.efc == 2) { m->y += 0x20000; fx2_tex_scroll(Lv, rec, 0, 2); }
            if (m->v2a < 8) m->v2a |= 4;
        }
    } else {
        int share = st->cbid ? m->d[1] * st->count126 / st->cbid : 0;
        if (s < T.nst) T.tbl[s][3] = (uint16_t)(I(m->d[0]) + share);
        if (I(m->y - FX(share) - m->d[0]) < 0) { T.efe = 1; if (T.efc) { m->y += 0x20000; fx2_tex_scroll(Lv, rec, 0, rec == 5 && n1 == 2 ? 2 : 1); } }
    }
    if (s < T.nst) T.tbl[s][2] = m->v2a;
}

static int riders_stomp(Objects *O, Object *st)                    /* the +0xe8 rider chain: Hercules ground-pounds */
{
    int b[6];
    if (!model_box(O->L, st, 1, b) && !model_box(O->L, st, 0, b)) return 0;
    int X = I(P_->o.x) - I(st->x), Z = I(P_->o.z) - I(st->z), Y = I(P_->o.y) - I(st->y);
    if (X < b[0] - 8 || X > b[0] + b[3] + 8 || Z < b[2] || Z > b[2] + b[5] || iabs(Y - b[1]) > 4) return 0;
    if ((P_->hitflags & 0x20) && !(P_->consumed & 0x20)) { P_->consumed |= 0x20; return rnd(O, 3) + 2; }
    return 0;
}

static void station_tick(Objects *O, Object *o)
{
    uint32_t f = o->init_fn;
    uint16_t ttype = f == S_ST35 ? 0x2a : f == S_ST39 ? 0x3a : f == S_ST3E ? 0x4d : 0x30;
    if (!o->d[6]) {                                                 /* first update: restore the meters (FUN_10041000) */
        o->d[6] = 1;
        for (int i = 0; i < o->nkids; i++) if (o->kid[i] && o->kid[i]->type == 0x2f) meter_restore(O, o->kid[i], o->b21);
        if (f == S_ST3E) { o->home[0] = o->x; o->home[1] = o->y; }
    }
    int32_t swing = 0; int swing_d = 0;
    if (f == S_ST3E) {                                              /* FUN_1003dcd0: pendulum stage */
        if (!o->tag) { if (!en_offscreen(O, o, 0)) { o->tag = 1; snd(O, 0x4864, o); } }
        else o->timer = (int16_t)((o->timer + 8) & 0x3ff);
        int a = (int8_t)((uint16_t)sintab(O, o->timer) >> 8);
        int prev = o->s9a; o->s9a = (int16_t)a;
        o->px = o->x;
        swing = sintab(O, a) * 0x140;
        o->x = o->home[0] + swing;
        o->y = o->home[1] + sintab(O, a + 0x100) * 0x140;
        if (a == 0x7f && o->tag == 1) { o->tag = 2; snd(O, 0x4863, o); }
        if (a == -0x80 && o->tag == 2) { o->tag = 1; snd(O, 0x4864, o); }
        swing_d = prev - a;
        fx2_tex_scroll((Level *)O->L, 1, 0, -swing_d);              /* FIX31: FUN_10002d80(1, 0, -(prev - a)) */
    }
    if (count_done(O, o, ttype)) {
        if (T.eea == o->b21) T.eec = o;
        int stomp = (f == S_ST32 || f == S_ST3E) ? riders_stomp(O, o) : 0;
        if (f == S_ST32) {                                          /* FUN_1003cf90: the stage sinks as targets go */
            int sink = o->cbid ? (o->d[1] * o->count126) / o->cbid : 0;
            if (o->b21 < T.nst) T.tbl[o->b21][4] = (uint16_t)(I(o->home[1]) - sink);
            fx2_tex_scroll((Level *)O->L, 5, 0, -(int)((uint16_t)T.ee6 >> 4));   /* FIX31: FUN_10002d80(5, 0, -(ee6 >> 4)) */
            if (I(o->y + FX(sink) - o->home[1]) > 0) { T.efe = 1; if (T.efc) o->y -= 0x10000; }
        }
        int32_t hx = 0, hz = 0; int ha = 0;
        for (int i = 0; i < o->nkids; i++) {
            Object *k = o->kid[i];
            if (!k || !k->used) continue;
            if (k->type == 0x43) { hx = k->x; hz = k->z; ha = k->rot[1]; }
            if (k->type == 0x30 && f == S_ST32) {
                k->y = o->y;
                if (stomp && k->v2a != 9) { k->hp = -1; T.f04++; stomp--; }
            }
            if (k->type == 0x4d) {
                k->y = k->d[1] + o->y; k->x = k->home[0] + swing;
                if (stomp && k->v2a != 9) { k->hp = -1; T.f04++; stomp--; }
            }
            if (k->type == 0x4032) {
                if (f == S_ST32) { k->y = o->y - 0xa0000; }
                if (f == S_ST3E) { k->y = o->y - 0x80000; k->x = k->home[0] + swing; k->rot[0] = (int16_t)-o->s9a; }
            }
            if (k->type == 0x4033 && f == S_ST3E) { k->x = o->x - 0xb40000; k->y = o->y; }
            if (k->type == 0x3a && hx) {                             /* FUN_1003d440: riders of the carousel */
                int a = ha & 0x3ff;
                k->x = sintab(O, a) * 0xf0 + hx;
                k->z = hz + sintab(O, a + 0x100) * 0xf0;   /* FIX32: FUN_1003d440 uses *0xf0 for z too (was 0x3c) */
                k->rot[0] = (int16_t)(int8_t)((uint16_t)sintab(O, a - 0x200) >> 8);
                if (!k->cool && hz < 0x800000 && a > 0x1f8 && a < 0x208) { snd(O, 0x5867, k); k->cool = 0x14; }
                ha += 0x100;
            }
            if (k->type == 0x2f) station_meter(O, o, k, f == S_ST32 || f == S_ST3E ? 5 : 0, f == S_ST32 ? 2 : 1);
            if (k->type == 0x4031) {
                if (f == S_ST3E && k->z > 0x1200000) k->s98 = (int16_t)swing_d;   /* FIX31: the far pulleys turn with the swing (FUN_1003dcd0) */
                else if (T.eea == o->b21) k->s98 = (int16_t)T.ee6;
            }
        }
        /* FIX31: FUN_10007f8e(id, 0) / FUN_10006da0(id, 0): the crank sound belongs to no object and must be stopped when
         * the crank does (it is a looping sample: it used to play for ever). */
        if (T.ee6 == 0) { if (T.rope_snd) { obj_sound_stop(O, T.rope_snd); T.rope_snd = 0; } }
        else if (!T.rope_snd) { T.rope_snd = f == S_ST32 ? 0x60 : 0x5f; obj_sound_none(O, T.rope_snd, P_->o.x); }
    }
    if (en_offscreen(O, o, 0x200)) {
        if (f == S_ST33 && T.rope_snd) { obj_sound_stop(O, T.rope_snd); T.rope_snd = 0; }   /* FUN_1003cb90 */
        obj_kill(O, o, 0);
    }
}

static void gate_tick(Objects *O, Object *o)                       /* FUN_1003ecb0 */
{
    if (T.eea == o->b21) T.ef8 = o;
    if (o->b21 < T.nst) T.tbl[o->b21][5] = o->v2a;
    if (o->airborne) { obj_drop(O, o); return; }
    int step = 0;
    if (o->v2a == 3) {
        if (o->z < o->d[2]) { step = -8; o->z += 0x40000; }
        else o->v2a = 9;
    }
    for (int i = 0; i < o->nkids && o->list; i++) {
        Object *k = o->kid[i];
        if (!k || k->type == 0x202b) continue;
        uint32_t e = o->list + 12u * (uint32_t)k->kidx;
        k->x = o->x + FX(pe_s16(O->pe, e)); k->y = o->y + FX(pe_s16(O->pe, e + 2)); k->z = o->z + FX(pe_s16(O->pe, e + 4));
        if (k->type == 0x4031) k->s98 = (int16_t)(step * 4);
    }
    if (en_offscreen(O, o, 0x200) && o->b21 != T.f0c) obj_kill(O, o, 0);
}

static void weight_tick(Objects *O, Object *o)                     /* FUN_1003eec0 */
{
    if (T.eea == o->b21) T.ef8 = o;
    if (o->b21 < T.nst) T.tbl[o->b21][5] = o->v2a;
    if (o->airborne) { obj_drop(O, o); goto offs; }
    if (o->v2a == 1) {                                              /* hoisted by Phil's lesson */
        if (o->d[1] < o->y) o->y -= 0x20000;
        if (o->x + 0xc00000 < P_->o.x) { o->v2a = 2; snd(O, 0x404b, o); }
    } else if (o->v2a == 2) {                                       /* Hercules is past: it drops */
        if (phys(O, o) & 1) { o->v2a = 4; snd(O, 0x404c, o); }
    } else if (o->v2a == 4) {
        if (phys(O, o) & 2) o->v2a = 0;
    }
    for (int i = 0; i < o->nkids && o->list; i++) {
        Object *k = o->kid[i];
        if (!k) continue;
        if (k->type == 0x403b) {
            uint32_t e = o->list + 12u * (uint32_t)k->kidx;
            k->x = o->x + FX(pe_s16(O->pe, e)); k->y = o->y + FX(pe_s16(O->pe, e + 2)); k->z = o->z + FX(pe_s16(O->pe, e + 4));
        } else k->y = ((o->d[1] - o->y + 0x800000) >> 2) + 0x2f40000;
    }
offs:
    if (en_offscreen(O, o, 0x200)) obj_kill(O, o, 0);
}

/* LEFTOVERS: FUN_1002bfb0 - binary search of the cosine half of the sine table (0x1005aa48, 0x200 s16 entries,
 * decreasing) for (a << 15) / b; returns the index 0..0x1ff. */
static int bfb0(Objects *O, int a, int b)
{
    if (b == 0) return 0;
    if (a == 0) return 0xff;
    int t = (a << 15) / b, lo = 0, hi = 0x1ff, mid = 0xff;
    for (;;) {
        int v = pe_s16(O->pe, 0x1005aa48u + 2u * (uint32_t)mid);
        if (v == t) return mid;
        if (v > t) lo = mid; else hi = mid;
        if (hi - 1 <= lo) return lo;
        mid = (hi - lo) / 2 + lo;
    }
}

/* LEFTOVERS: FUN_1002acf0 + the quadrant fix-up of FUN_1003f1c0: the angle (0x400 per turn) from the cannon
 * to Hercules. */
static int angle_of(Objects *O, int dx, int dz)
{
    int q = 4, ax = dx, az = dz;
    if (dx < 0) { ax = -dx; q = 6; }
    if (dz < 0) { az = -dz; q |= 8; }
    int lo = ax < az ? ax : az, hi = ax < az ? az : ax;
    int d = (lo >> 3) + hi + (lo >> 2);                             /* FUN_1002acf0 distance estimate */
    int u = bfb0(O, (int16_t)ax, d & 0xffff) & 0xffff;
    if (q == 6) u += 0x100;
    if (q == 4) u = 0x300 - u;
    if (q == 0xc) u += 0x300;
    if (q == 0xe) u = 0x100 - u;
    return u & 0x3ff;
}

static void cannon_tick(Objects *O, Object *o)                     /* FUN_1003f1c0 */
{
    if (o->airborne) { obj_drop(O, o); goto offs; }
    if (o->hp) o->hp--;                                             /* +0xee doubles as the reload timer */
    if (o->v2a == 0) {
        if (!en_offscreen(O, o, -0x20)) { o->v2a = 1; snd(O, 0x4056, o); }
    } else if (o->v2a == 1) {
        o->rot[0] += 4;
        if (o->rot[0] > 0) { o->rot[0] = 0; o->v2a = 2; }
    } else if (o->v2a == 2) {
        o->rot[1] = (int16_t)((o->rot[1] + o->d[1]) & 0x3ff);
        int want = angle_of(O, I(P_->o.x - o->x), I(P_->o.z - o->z));   /* LEFTOVERS */
        int diff = iabs(o->rot[1] - want);
        if (diff > 0x200) diff = 0x400 - diff;
        if (diff < 0xc && o->hp == 0 && !P_->dying) {
            Object *b = obj_spawn(O, 0x4028, o->x, o->y - 0x200000, o->z);
            if (b) {
                int sx = sintab(O, want) << 5, cz = sintab(O, want + 0x100) << 5;
                /* LEFTOVERS: as the original: FUN_1003fa50 faces it -x / -z and the signed speeds go in as both the
                 * speed and its cap, so FUN_1002b4c0's clamps leave the speed at the (possibly negative) cap */
                b->facing = 0xff; b->zface = 0xff;
                b->v[0] = b->v[1] = sx; b->v[8] = b->v[9] = cz;
                b->spawn = -1;
                o->hp = 0x14;
            }
        }
    }
    if (o->v2a == 0) goto offs;
    {
        int d = hit_damage(O, o);
        if (d) {
            o->hp -= (int16_t)d; o->flags50 &= ~0x200u;
            if (o->hp < 0) { snd(O, 0xc05a, o); Object *p = obj_spawn(O, 0x2027, o->x, o->y, o->z); if (p) p->spawn = -1; obj_kill(O, o, 1); return; }
        }
    }
offs:
    if (en_offscreen(O, o, 0x200)) obj_kill(O, o, 0);
}

static void ball_tick(Objects *O, Object *o)                       /* FUN_1003fb00: cannon ball */
{
    obj_run_words(O, o);
    vel(o);
    int pb[6];
    en_player_box6(P_, pb);
    int X = I(o->x), Y = I(o->y), Z = I(o->z);
    if (X >= pb[0] && X <= pb[3] && Y >= pb[1] && Y <= pb[4] && Z >= pb[2] && Z <= pb[5]) {
        en_hurt_player(O, o, 1);
        Object *p = obj_spawn(O, 0x4025, o->x, o->y, o->z); if (p) p->spawn = -1;
        obj_kill(O, o, 1); return;
    }
    if (o->tag) { o->tag--; return; }
    uint16_t fl = obj_attr_flags(O, X + (o->facing ? -0x20 : 0x10), Y, (Z + 0x40) & 0xf80);
    if (fl & 7) { Object *p = obj_spawn(O, 0x4025, o->x, o->y, o->z); if (p) p->spawn = -1; obj_kill(O, o, 1); return; }
    if (o->z < -0x600000 || o->z > 0x2000000 || en_offscreen(O, o, 0)) obj_kill(O, o, 0);
}

/* ---------------------------------------------------------------- spinning tops (TOPS bank) */

static void tops_init(Objects *O, Object *o)                       /* FUN_1003fe40 */
{
    o->flags50 = 0x400202; o->cbid = 1;
    o->y = 0x3880000;
    o->v[0] = 0; o->v[1] = 0x80000; o->v[2] = rnd(O, 0x4000) + 0x6000; o->v[3] = rnd(O, 0x4000) + 0x4000;
    o->facing = 0xff; o->zface = 0xff;
    o->v[8] = 0; o->v[9] = 0x80000; o->v[10] = 0x8000; o->v[11] = 0;
    o->d[0] = o->x; o->d[1] = o->y; o->d[2] = o->z;
    o->b21 = (uint8_t)station_of(I(o->x));
    o->hp = 3;
}

static void tops_tick(Objects *O, Object *o)                       /* FUN_1003ff30 */
{
    obj_run_script(O, o);
    int32_t *v = o->v;
    if (o->v2a == 1 || o->v2a == 2) {                               /* hop up / thrown out of the box */
        v[4] += v[6] - v[7]; if (v[4] < 0) v[4] = 0; if (v[4] > v[5]) v[4] = v[5];
        o->y += o->b95 ? -v[4] : v[4];
        if (v[4] == 0) { v[6] = v[7]; v[7] = 0; o->b95 = 0; }
        if (o->v2a == 1 && o->y > 0x3880000) { o->y = 0x3880000; o->v2a = 0; return; }
        if (o->v2a == 2) {
            v[8] += v[10] - v[11]; if (v[8] < 0) v[8] = 0; if (v[8] > v[9]) v[8] = v[9];
            o->z -= v[8];
            if (v[4] == 0) { v[11] = v[10]; v[10] = 0; }
            if (!o->b95 && land(O, o)) {
                v[4] = 0; o->y &= ~0xffff; v[5] = 0; v[7] = 0; v[9] = 0x60000;
                v[10] = rnd(O, 0x4000) + 0x7000; v[11] = rnd(O, 0x4000) + 0x4000;
                o->zface = 0xff; o->v2a = 4;
                snd(O, 0x4854, o);
            }
        }
        return;
    }
    if (o->v2a != 4 && o->v2a != 5) return;
    int speed = 5;
    if (o->inv) { speed = 0x28; o->inv--; }
    for (int i = 0; i < OBJ_MAX; i++) {                             /* tops bounce off each other (DAT_100717c0) */
        Object *t = &O->obj[i];
        if (t == o || !t->used || t->ai != AI3_TOPS || t->inv || (t->v2a != 4 && t->v2a != 5)) continue;
        int a[6], b[6];
        if (obj_box(O, o, 1, a) && obj_box(O, t, 1, b) && obj_boxes_overlap(a, b)) {
            o->facing = (uint8_t)~o->facing; o->zface = (uint8_t)~o->zface;
            t->facing = (uint8_t)~t->facing; t->zface = (uint8_t)~t->zface;
            o->inv = (uint8_t)(rnd(O, 10) + 10); t->inv = (uint8_t)(rnd(O, 10) + 10);
            speed = 0;
        }
    }
    int d = hit_damage(O, o);
    if (d) {
        o->inv = (uint8_t)(rnd(O, 10) + 10);
        o->hp -= (int16_t)d;
        if (o->hp < 1) {
            o->flags50 &= ~0x200u;
            if (o->spawn >= 0 && o->spawn < O->nsp) O->sp[o->spawn].state = 2;
            snd(O, 0x4055, o);
            obj_set_anim(O, o, 4);
            for (int k = 0; k < 4; k++) {                          /* table 0x10066738: 4 drops */
                uint32_t id = pe_u32(O->pe, 0x10066738 + 4u * (uint32_t)(k + rnd(O, 4) * 4));
                Object *tmp = o; int32_t y0 = o->y; o->y -= 0x200000;
                en_drop_pickup(O, tmp, (int)(id & 0xffff));
                o->y = y0;
            }
            if (solids_spawn_children(O, o)) {}                    /* the top's pieces (+0x118 list) */
            o->v2a = 9;
            return;
        }
    }
    if (!o->inv && touching_player(O, o)) {
        en_hurt_player(O, o, 3);
        o->facing = (uint8_t)~o->facing; o->zface = (uint8_t)~o->zface; o->inv = 10;
        O->t_hits++;
        speed = 0;
    }
    uint16_t fl = obj_attr_flags(O, I(o->x) - 0x18, I(o->y) - 0x20, I(o->z) + 0x10)
                | obj_attr_flags(O, I(o->x) + 0x18, I(o->y) - 0x20, I(o->z) + 0x10);
    if (fl & 7) { o->facing = (uint8_t)~o->facing; o->zface = (uint8_t)~o->zface; o->inv = 10; speed = 0; }
    /* FUN_1002b5e0 (chase Hercules when he is near and alive) / FUN_1002b9a0 (wander): step toward the target */
    int32_t tx = P_->o.x, tz = P_->o.z;
    if (P_->dying || iabs(I(P_->o.x - o->x)) >= 0x140) { tx = o->d[0]; tz = o->d[2]; }
    if (speed) {
        int dx = I(tx - o->x), dz = I(tz - o->z);
        o->facing = dx < 0 ? 0xff : 0;
        o->x += (dx < 0 ? -1 : dx > 0 ? 1 : 0) * speed * 0x4000;
        o->z += (dz < 0 ? -1 : dz > 0 ? 1 : 0) * speed * 0x2000;
    }
    mirror(o);
}

static void launch_init(Objects *O, Object *o)                     /* FUN_10040ba0: 0x4038 lid flies off */
{
    o->b95 = 0; o->flags54 |= 0x10000000u; o->facing = (uint8_t)o->v2a;
    uint32_t r = (uint32_t)rnd(O, 0x10000);
    o->v[7] = 0; o->v[5] = (int32_t)(((r & 0x3ff) + 0xb00) * 0x100); o->v[4] = o->v[5] >> 2;
    o->v[6] = rnd(O, 0x2000) + 0x8000;
    o->v2a = 1;
}

static void launch_tick(Objects *O, Object *o)                     /* FUN_10040c10 */
{
    if (o->v2a && (phys(O, o) & 1)) {
        int n = rnd(O, 6) + 4;
        for (int i = 0; i < n; i++) {
            uint32_t r1 = (uint32_t)rnd(O, 0x10000), r2 = (uint32_t)rnd(O, 0x10000);
            Object *p = obj_spawn(O, 0x4026, (int32_t)(((r2 & 0x1f00) + 0x1000) * 0x100) + o->x, o->y - (int32_t)((r1 & 0x1f00) * 0x100), o->z);
            if (p) p->spawn = -1;
        }
        o->frame = 0; o->v2a = 0;
    }
}

static void shard_init(Objects *O, Object *o)                      /* FUN_1003fcf0: top pieces */
{
    o->flags54 |= 0x10000000u; o->flags50 |= 0x800000;
    o->facing = (uint8_t)-rnd(O, 2); o->zface = (uint8_t)-rnd(O, 2); o->b95 = 0xff;
    int32_t r = (int32_t)(((rnd(O, 0x10000) & 0x3ff) + 0x300) * 0x100);
    o->v[5] = r; o->v[4] = r; o->v[6] = 0; o->v[7] = rnd(O, 0x2000) + 0x4000;
    r = (int32_t)(((rnd(O, 0x10000) & 0xff) + 0x200) * 0x100);
    o->v[9] = r; o->v[8] = r >> 1; o->v[10] = rnd(O, 0x1000) + 0x2000; o->v[11] = 0;
    r = (int32_t)(((rnd(O, 0x10000) & 0x3ff) + 0x300) * 0x100);
    o->v[1] = r; o->v[0] = r >> 1; o->v[3] = 0; o->v[2] = rnd(O, 0x1000) + 0x6000;
    o->v2a = 0;
}

static void shard_tick(Objects *O, Object *o)                      /* FUN_1003fe00 */
{
    int r = phys(O, o);
    if ((r & 2) || en_offscreen(O, o, 0x40)) obj_kill(O, o, 1);
}

/* ---------------------------------------------------------------- Phil */

static void speech(Objects *O, int n)                              /* FUN_1002c4c0 */
{
    sfx_speech(O->fs, n);
}

static int route_jump(Objects *O, Object *o, int st)               /* FUN_1002d3f0 */
{
    for (int i = 0; i < 128; i++) {
        uint32_t e = o->list + 6u * (uint32_t)i;
        int16_t op = pe_s16(O->pe, e);
        if (op == 0x14 && pe_s16(O->pe, e + 4) == st) { T.eea = st; o->route = i; return 1; }
        if (op == 0) return 0;
    }
    return 0;
}

/* LEFTOVERS: DAT_10070e6c - the original keeps the visible column range of each strip (written while the map is
 * drawn); the port derives it from the camera: half the screen width scaled by the strip's depth. */
static void visible_cols(Objects *O, int32_t z, int *lo, int *hi)
{
    int half = 160 * (I(z) + 320) / 320;
    *lo = (O->camx - half) >> 6; *hi = (O->camx + half) >> 6;
}

static void phil_route(Objects *O, Object *o)                      /* FUN_1002d440 */
{
    if (o->route < 0) o->route = 0;
    uint32_t e = o->list + 6u * (uint32_t)o->route;
    int16_t op = pe_s16(O->pe, e), a = pe_s16(O->pe, e + 2), b = pe_s16(O->pe, e + 4);
    int32_t tx = FX(a), ty = FX(b);
    switch (op) {
    case 0: case 0xe: break;
    case 1: o->x = tx; o->y = ty; o->route++; return;
    case 2: {
        int v = o->v2a;
        int32_t dx = o->x - tx;
        if (v == 1) {
            if (iabs(dx) < 0x420000) { obj_set_anim(O, o, 8); return; }
            if (en_offscreen(O, o, 0)) {                            /* LEFTOVERS: DAT_10070e6c visible columns */
                int lo, hi; visible_cols(O, o->z, &lo, &hi);
                int32_t a0 = FX(lo * 64 - 64), a1 = FX((hi + 1) * 64);
                if (a0 < tx && o->x < a0) o->x = a0;
                if (tx < a1 && a1 < o->x) o->x = a1;
            }
            o->facing = o->x >= tx ? 0xff : 0;
            return;
        }
        if (v == 0x16) {
            if (iabs(dx) < 0x100000) { o->x = tx; o->route++; return; }
            o->route++; return;
        }
        if (v == 0x15) { if (iabs(dx) > 0x420000) obj_set_anim(O, o, 7); return; }
        obj_set_anim(O, o, 6);
        o->facing = o->x >= tx ? 0xff : 0;
        return;
    }
    case 3: o->cbid = a; T.eea = b; o->route++; return;
    case 4: if (T.eec && T.eec->used && T.eec->cbid == T.eec->count126) o->route++; return;
    case 5: case 6: {
        int v = o->v2a;
        int st = T.ef2;
        if (v == 0 || v == 0xb) {
            int done = st >= 0 && st < T.nst && T.tbl[st][2] > 7;
            if (done || T.f0a) {
                if (done && T.tbl[st][5] != 0) { o->v2a = 4; o->facing = 0; T.f0a = 0; return; }
                obj_set_anim(O, o, op == 5 ? 0x22 : 0x31);
                o->facing = 0; T.f0a = 0;
            }
            return;
        }
        if (v == 5) {
            if (T.ef8 && T.ef8->used && T.ef8->v2a == 0) {
                T.ef8->v2a = op == 5 ? 3 : 1;
                snd(O, op == 5 ? 0x4049 : 0x404a, T.ef8);
            }
            return;
        }
        if (v == 9) {
            if (op == 5 && T.ef8 && T.ef8->v2a == 9) o->route++;
            if (op == 6 && tx < P_->o.x && T.ef8) o->route++;
        }
        return;
    }
    case 7: obj_set_anim(O, o, 0x2e); o->route++; return;
    case 8: o->z = tx; o->route++; return;
    case 9:
        if (P_->o.x <= tx) { o->v2a = 10; return; }
        o->v2a = 0; o->route++; return;
    case 0xb: {
        if (o->x < tx) o->x = tx;
        if (o->x > ty) o->x = ty;
        int32_t d = o->x - P_->o.x;
        o->facing = o->x < P_->o.x ? 0 : 0xff;
        if (o->v2a == 1) {
            if (!o->facing ? d >= -0x180000 : d <= 0x180000) obj_set_anim(O, o, 8);
            return;
        }
        if (o->v2a != 0) return;
        if (!o->facing ? d >= 0 : d <= 0) return;
        if (iabs(d) > 0x600000) obj_set_anim(O, o, 7);
        return;
    }
    case 0xc:
        if (b >= 0 && b < T.nst && T.tbl[b][2] > 3) { o->route += a + 1; return; }
        o->route++; return;
    case 0xd: obj_set_anim(O, o, 0); o->route++; return;
    default: o->route++; return;
    }
}

static void phil_init(Objects *O, Object *o)                       /* FUN_1002cc50 */
{
    T.eec = T.ef8 = NULL;
    T.f0e = T.f0a = T.quiet = T.f04 = 0; O->t_hits = 0;
    T.ed8 = T.eda = T.ee4 = 10;
    T.ee0 = T.ee2 = T.edc = T.f08 = 0;
    T.ef4 = 0x66; T.f00 = 0x83;
    o->ai = AI3_PHIL;
    o->facing = 0;
    obj_set_anim(O, o, 0);
    o->movetab = 0x1005c3a0; o->movei = 1; o->moving = -1;
    o->cbid = 0; o->route = 0;
    obj_drop(O, o);
    o->flags54 |= 8;
}

static void phil_speech(Objects *O, Object *o)                     /* the talk logic at the top of FUN_1002cd30 */
{
    if (T.f0e && --T.ee4 == 0) { T.f0e--; T.ee4 = 0x32; }
    if (T.f04 && --T.ed8 == 0) { T.f04--; T.ed8 = 0x32; }
    if (O->t_hits && --T.eda == 0) { O->t_hits--; T.eda = 0xc; }
    if (o->v2a == 0xb || o->v2a == 0) { if (!T.f04 && !O->t_hits && !T.f0e) T.quiet++; }
    else T.quiet = 0;
    if (T.edc) T.edc--;
    if (T.ee2) T.ee2--;
    if (T.ee0) T.ee0--;
    if (T.f08) { T.f08--; return; }
    if (P_->dying) return;
    T.f02 &= 3; T.ee8 &= 3;
    int st = T.f0c;
    const PeImage *pe = O->pe;
    int idle = 0;
    switch (st) {
    case 1: case 2: case 3: case 4: case 5:
        if (pe_u16(pe, 0x1005c3b8 + 2u * (uint32_t)st) < T.f0e) {
            speech(O, pe_u16(pe, 0x1005c3d0 + 2u * (uint32_t)st)); T.f0e = 0; T.f08 = 0xfa; return;
        }
        if (T.f04 > 1) { if (!T.edc) { speech(O, 0x1a); T.edc = 800; T.f08 = 100; T.f04 = 0; } return; }
        if (O->t_hits > 0x14) {
            if (T.ee2) return;
            speech(O, pe_u16(pe, 0x1005c3f0 + 2u * (uint32_t)T.f02)); T.f02++; T.ee2 = 0xf0; T.f08 = 0xa0; O->t_hits = 0; return;
        }
        idle = 1; break;
    case 8:
        if (O->t_hits > 10) {
            if (T.ee2) return;
            speech(O, pe_u16(pe, 0x1005c3f0 + 2u * (uint32_t)T.f02)); T.f02++; T.ee2 = 0xf0; T.f08 = 0xa0; O->t_hits = 0; return;
        }
        idle = 1; break;
    case 0xc:
        if (T.f0e > 6) { speech(O, pe_u16(pe, 0x1005c3d0 + 2u * (uint32_t)st)); T.f08 = 0xfa; T.f0e = 0; return; }
        if (O->t_hits > 10) {
            if (T.ee2) return;
            speech(O, pe_u16(pe, 0x1005c3f0 + 2u * (uint32_t)T.f02)); T.f02++; T.ee2 = 0xf0; T.f08 = 0xa0; O->t_hits = 0; return;
        }
        if (T.quiet < 0xfb || T.ee0) return;
        speech(O, pe_u16(pe, 0x1005c400 + 2u * (uint32_t)T.ee8)); T.ee8++; T.ee0 = 0x15e; T.f08 = 0xa0; T.quiet = 0;
        return;
    default: return;
    }
    if (idle && T.quiet >= 0xfb && !T.ee0) {
        speech(O, pe_u16(pe, 0x1005c3f8 + 2u * (uint32_t)T.ee8));
        T.ee8++; T.ee0 = 0x15e; T.f08 = 0xa0; T.quiet = 0;
    }
}

static void phil_tick(Objects *O, Object *o)                       /* FUN_1002cd30 */
{
    phil_speech(O, o);
    if (getenv("PHIL_DBG")) fprintf(stderr, "phil_tick x %d px %d\n", I(o->x), I(P_->o.x));
    if (o->airborne) obj_drop(O, o);
    T.f0c = station_of(I(P_->o.x));
    T.ef2 = station_of(I(o->x));
    T.efc = 0;
    if (o->x < 0x4f80000) o->x = 0x4f80000;
    if (T.f0c != T.eea && T.f0c != T.ef2) route_jump(O, o, T.f0c);
    phil_route(O, o);
    if (getenv("PHIL_LOG")) printf("PHIL st(h)=%d st(p)=%d eea=%d route=%d x=%d v2a=%d anim=%d frame=%d\n", T.f0c, T.ef2, T.eea, o->route, I(o->x), o->v2a, o->anim, o->frame);
    if (o->v2a == 10) {                                             /* off stage: hidden (flags54 bit 31 clear) */
        if (o->frame > 0) { o->s9a = o->frame; o->frame = 0; }
    } else {
        if (o->frame == 0 && o->s9a) o->frame = o->s9a;
        obj_run_script(O, o);
        en_move4(O, o);
        T.ee6 = o->cool;
        mirror(o);
        if (o->v2a == 7) { T.efc = 1; T.ef4 = o->s36; }              /* FIX31: the crank speed stays Phil's +0x4c (it was forced to 0x10) */
        else if (T.efe == 2) { T.efc = 2; T.ee6 = 0x10; T.ef4 = 0x66; }
        T.f00 = (o->v2a == 2 || o->v2a == 5) ? o->s36 : 0;
    }
    T.efe = 0;
}

/* FIX31: Phil's script callbacks (script op -10 calls callback[+0x124]; level 1 table 0x10068138: 2 = FUN_1002d1f0 for
 * the lessons 1-5, 4 = FUN_1002d320 for the rest). They are what makes Phil crank while the meter / stage still has to
 * move (DAT_10070efe = 1: anim 0x11 -> 0x12 loop, state 7), stop when it does not (anim 0x13), turn to Hercules and
 * play his random idle gestures. Without them Phil never cranked until every target was beaten, so the stage stayed put. */
static void phil_idle(Objects *O, Object *o, int n)
{
    if (o->v2a != 0) return;
    if (o->facing == 0xff) { if (o->x < P_->o.x) { obj_set_anim(O, o, 3); return; } }
    else if (P_->o.x < o->x) { obj_set_anim(O, o, 3); return; }
    static const int pick[6] = {0x16, 0x19, 0x1c, 0x25, 0x28, 0x2b};
    obj_set_anim(O, o, pick[rnd(O, n)]);
}

int enemies3_callback(Objects *O, Object *o, int id)
{
    if (o->ai != AI3_PHIL) return 0;
    if (id == 2) {                                                  /* FUN_1002d1f0 */
        if (o->v2a == 7) {
            if (T.efe == 2) obj_set_anim(O, o, 0x13);
            if (T.efe == 0) { obj_set_anim(O, o, 0x13); return 1; }
        } else if (T.efe == 0) phil_idle(O, o, 6);
        else if (T.efe == 1) { o->facing = 0; obj_set_anim(O, o, 0x11); }
        return 1;
    }
    if (id == 4) {                                                  /* FUN_1002d320 */
        phil_idle(O, o, 4);
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- dispatch */

int enemies3_init(Objects *O, Object *o, uint32_t init)
{
    if (!T.on) enemies3_reset(O);
    switch (init) {
    case H_PHIL: phil_init(O, o); return 1;
    case H_METER: o->ai = AI3_METER; o->anim = 0; return 1;
    case H_TGT30:                                                   /* FUN_1003e220 (0x30 and 0x4d) */
        o->ai = o->type == 0x4d ? AI3_TGT4D : AI3_TGT30;
        obj_set_anim(O, o, 0xd);
        o->flags50 = 0xa02; o->cbid = 0; o->hp = 3;
        o->d[1] = (int32_t)((uint32_t)o->v2a << 16); o->v2a = 0;
        o->home[0] = o->x;
        return 1;
    case H_TGT2A:                                                   /* FUN_1003e370 */
        o->ai = AI3_TGT2A; obj_set_anim(O, o, 0); o->movei = 0; o->moving = 1;
        o->flags50 = 0x202; o->cbid = 1; o->hp = 4;
        return 1;
    case H_TGT3A:                                                   /* FUN_1003e580 */
        o->ai = AI3_TGT3A; obj_set_anim(O, o, 0xd);
        o->flags50 = 0x202; o->cbid = 0; o->hp = 5; o->cool = 0x14;
        return 1;
    case H_HUB:                                                     /* FUN_1003d780 */
        o->ai = AI3_HUB; o->frame = 5; o->flags54 |= 0x10000040u;
        o->rot[1] = (int16_t)(rnd(O, 0x400) & 0x3ff);
        o->d[7] = o->x & 0x800000;
        return 1;
    case H_TOPS: o->ai = AI3_TOPS; tops_init(O, o); return 1;
    case H_BALL:                                                    /* FUN_1003fa50 */
        if (O->L->index != 1) return 0;
        o->ai = AI3_BALL; o->kind = K_ENEMY;
        o->facing = 0xff; o->zface = 0xff;
        o->v[10] = 0x4000; o->v[11] = 0; o->v[9] = o->v[8] = (int32_t)(((rnd(O, 0x10000) & 0x1ff) + 0xa80) * 0x100);
        o->v[1] = o->v[0] = (int32_t)(((rnd(O, 0x10000) & 0x1ff) + 0xa80) * 0x100);
        o->v[2] = rnd(O, 0x2000) + 0x3000; o->v[3] = 0;
        o->scale = 0x80; o->flags54 |= 5; o->tag = 4;
        return 1;
    case H_LAUNCH: o->ai = AI3_LAUNCH; launch_init(O, o); return 1;
    case H_SHARD: o->ai = AI3_SHARD; shard_init(O, o); return 1;
    }
    return 0;
}

int enemies3_tick(Objects *O, Object *o)
{
    switch (o->ai) {
    case AI3_PHIL: phil_tick(O, o); return 1;
    case AI3_METER: meter_tick(O, o); return 1;
    case AI3_TGT30: case AI3_TGT4D: tgt30_tick(O, o); return 1;
    case AI3_TGT2A: tgt2a_tick(O, o); return 1;
    case AI3_TGT3A: tgt3a_tick(O, o); return 1;
    case AI3_HUB: hub_tick(O, o); return 1;
    case AI3_TOPS: tops_tick(O, o); return 1;
    case AI3_BALL: ball_tick(O, o); return 1;
    case AI3_LAUNCH: launch_tick(O, o); return 1;
    case AI3_SHARD: shard_tick(O, o); return 1;
    }
    return 0;
}

/* solids.c objects whose update is ported here (stations, gate, weight, cannon). */
int enemies3_solid_tick(Objects *O, Object *o)
{
    if (O->L->index != 1) return 0;
    switch (o->init_fn) {
    case S_ST32: case S_ST33: case S_ST35: case S_ST39: case S_ST3E: station_tick(O, o); return 1;
    case S_RACK: gate_tick(O, o); return 1;
    case S_LIFT: weight_tick(O, o); return 1;
    case S_CANNON: cannon_tick(O, o); return 1;
    }
    return 0;
}

void enemies3_debug(Objects *O, FILE *f);
/* Per frame: Phil's boards (0x2029 / 0x202b) show his current marks. */
void enemies3_frame(Objects *O)
{
    if (O->L->index != 1 || !O->P) return;
    for (int i = 0; i < OBJ_MAX; i++) {
        Object *o = &O->obj[i];
        if (!o->used) continue;
        if (o->type == 0x2029) o->frame = (int16_t)T.ef4;
        if (o->type == 0x202b) o->frame = (int16_t)T.f00;
    }
    static int bt;
    bt++;
    if (getenv("TGT_KILL") && bt % atoi(getenv("TGT_KILL")) == 0) for (int i = 0; i < OBJ_MAX; i++) {   /* test hook: beat one more target of Hercules' station every N frames */
        Object *o = &O->obj[i];
        if (o->used && (o->ai == AI3_TGT30 || o->ai == AI3_TGT4D || o->ai == AI3_TGT2A || o->ai == AI3_TGT3A) && o->v2a != 9 && station_of(I(o->x)) == T.f0c) { o->hp = -1; if (o->ai == AI3_TGT2A) { o->flags50 &= ~0x200u; tgt_beaten(o, O); } break; }
    }
    if (getenv("TRAINING_PRIZE")) for (int i = 0; i < OBJ_MAX; i++) {      /* test hook: take every ready prize without touching it */
        Object *o = &O->obj[i];
        if (o->used && o->ai == AI3_METER && o->v2a < 8 && (o->v2a & 4)) { obj_set_anim(O, o, 0xd); T.f0a = 1; }
    }
    if (getenv("TP_AT")) {                                          /* test hook: TP_AT="frame,x[,y];frame,x[,y]..." teleports Hercules at frame f */
        const char *q = getenv("TP_AT");
        while (q && *q) {
            int f, x, y = -1, n = 0;
            if (sscanf(q, "%d,%d,%d%n", &f, &x, &y, &n) < 2) { if (sscanf(q, "%d,%d%n", &f, &x, &n) < 2) break; y = -1; }
            if (bt == f) { P_->o.x = x << 16; if (y >= 0) P_->o.y = y << 16; }
            q = strchr(q, ';'); if (q) q++;
        }
    }
    if (getenv("TGT_POKE") && bt % atoi(getenv("TGT_POKE")) == 0) for (int i = 0; i < OBJ_MAX; i++) {   /* test hook: a non-fatal blow on the first walker / drum */
        Object *o = &O->obj[i];
        if (o->used && (o->ai == AI3_TGT2A || o->ai == AI3_TGT4D) && o->v2a != 9 && !o->inv) {
            o->hp -= 1; obj_set_anim(O, o, o->ai == AI3_TGT2A ? (o->v2a == 1 ? 5 : 8) : 0x14); o->inv = o->ai == AI3_TGT2A ? 0x14 : 10;
            if (o->ai == AI3_TGT2A) obj_sound_stop(O, 0); break; }
    }
    if (getenv("SP_LIST") && bt == 1) for (int i = 0; i < T.nst; i++) printf("STN %d xend %d bits %x meter(%d %d) stage %d gate %d\n", i, T.tbl[i][0], T.tbl[i][1], T.tbl[i][2], T.tbl[i][3], T.tbl[i][4], T.tbl[i][5]);
    if (getenv("SP_LIST") && bt == 1) for (int i = 0; i < O->nsp; i++) { uint16_t ty = O->sp[i].type; uint16_t tmpl = ty; (void)tmpl; printf("SP %d type %04x at %d,%d,%d\n", i, ty, I(O->sp[i].x), I(O->sp[i].y), I(O->sp[i].z)); }
    if (getenv("TRAINING_BEAT") && bt >= (getenv("TRAINING_BEAT_AT") ? atoi(getenv("TRAINING_BEAT_AT")) : 0)) {                                  /* test hook: beat every target of the lesson Hercules is in */
        for (int i = 0; i < OBJ_MAX; i++) {
            Object *o = &O->obj[i];
            if (o->used && (o->ai == AI3_TGT30 || o->ai == AI3_TGT4D || o->ai == AI3_TGT2A || o->ai == AI3_TGT3A) && o->v2a != 9
                && (!getenv("TRAINING_BEAT_ST") || T.f0c == atoi(getenv("TRAINING_BEAT_ST"))) && (atoi(getenv("TRAINING_BEAT")) == 2 ? iabs(I(o->x) - I(P_->o.x)) < 200 : station_of(I(o->x)) == T.f0c)) {
                o->hp = -1;
                if (o->ai == AI3_TGT2A) tgt_beaten(o, O);     /* the walkers only test their hp when hit */
            }
        }
    }
    if (getenv("ANIM_DUMP")) {                                      /* DOLLS: dump the scripts of the first object of each type */
        static unsigned seen[64]; static int ns;
        for (int i = 0; i < OBJ_MAX; i++) {
            Object *o = &O->obj[i]; int k;
            if (!o->used || !o->anims) continue;
            for (k = 0; k < ns; k++) if (seen[k] == o->type) break;
            if (k < ns || ns >= 64) continue;
            seen[ns++] = o->type;
            printf("type %04x anims %08x\n", o->type, o->anims);
            for (int a = 0; a < 40; a++) {
                uint32_t sc = pe_u32(O->pe, o->anims + 4u * (uint32_t)a);
                if (!sc || sc < 0x10000000) continue;
                printf("  anim %d:", a);
                for (int w = 0; w < 40; w++) {
                    int16_t v = pe_s16(O->pe, sc + 4u * (uint32_t)w), g = pe_s16(O->pe, sc + 4u * (uint32_t)w + 2);
                    printf(" %d/%d", v, g);
                    if (v == -1 || v == -6 || v == -7 || v == -4 || v == -0xc) break;
                }
                printf("\n");
            }
        }
    }
    static int t;
    if (getenv("TRAINING_LOG") && ++t % atoi(getenv("TRAINING_LOG")) == 0) enemies3_debug(O, stdout);
}

void enemies3_debug(Objects *O, FILE *f)
{
    if (O->L->index != 1) return;
    fprintf(f, "  training: player st %d phil st %d lesson %d efe %d efc %d hits %d beaten %d prize %d\n",
            T.f0c, T.ef2, T.eea, T.efe, T.efc, O->t_hits, T.f0e, T.f0a);
    for (int i = 0; i < OBJ_MAX; i++) {
        Object *o = &O->obj[i];
        if (!o->used) continue;
        if (o->ai == AI3_PHIL) fprintf(f, "    phil x %d z %d v2a %d anim %d route %d frame %d\n", I(o->x), I(o->z), o->v2a, o->anim, o->route, o->frame);
        if (o->ai >= AI3_TGT30 && o->ai <= AI3_TGT3A) fprintf(f, "    tgt %04x x %d y %d z %d v2a %d hp %d anim %d inv %d moving %d movei %d zface %d\n", o->type, I(o->x), I(o->y), I(o->z), o->v2a, o->hp, o->anim, o->inv, o->moving, o->movei, o->zface);
        if (o->ai == AI3_METER) { int mb[6], pb[6]; int hv = obj_box(O, o, 1, mb); en_player_box6(P_, pb); fprintf(f, "    meter x %d y %d z %d v2a %d anim %d box %d [%d %d %d %d %d %d] player x %d y %d z %d pbox [%d %d %d %d %d %d]\n", I(o->x), I(o->y), I(o->z), o->v2a, o->anim, hv, mb[0],mb[1],mb[2],mb[3],mb[4],mb[5], I(P_->o.x), I(P_->o.y), I(P_->o.z), pb[0],pb[1],pb[2],pb[3],pb[4],pb[5]); }
        if (o->init_fn == S_RACK || o->init_fn == S_LIFT) fprintf(f, "    %s st %d v2a %d y %d z %d\n", o->init_fn == S_RACK ? "gate" : "weight", o->b21, o->v2a, I(o->y), I(o->z));
        if (o->kind == K_SOLID && (o->init_fn == S_ST32 || o->init_fn == S_ST33 || o->init_fn == S_ST35 || o->init_fn == S_ST39 || o->init_fn == S_ST3E))
            fprintf(f, "    station %d (%08x) %d/%d y %d\n", o->b21, o->init_fn, o->count126, o->cbid, I(o->y));
    }
}

/* PLAYER: DAT_10070f0c (station under Hercules) for the Training camera bias (FUN_1003ca50) */
int training_cam_station(void) { return T.f0c; }
