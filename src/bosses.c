/* Boss fights: ports of the ENG1.DLL level scripts and boss objects for HYDRA (level 6), MEDUSA (7) and
 * VORTEX / Hades (11). The level hooks are the original's FUN_1001fd20 (start) and FUN_1001fd90 (per frame);
 * the boss objects are the actor / 0x2000-pool handlers those start functions spawn. All animation tables,
 * paths and difficulty tables are read from ENG1.DLL at run time.
 * See analysis/objects_notes.md "Boss fights". */
#include "objects_int.h"
#include "sfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int iabs(int v) { return v < 0 ? -v : v; }
static int32_t s32(const PeImage *pe, uint32_t a) { return (int32_t)pe_u32(pe, a); }

/* Roles (Object.ai for K_BOSS objects). */
enum {
    R_NONE = 100,                   /* clear of the enemies.c AI_* values (proj_hit tests ai == AI_PROJ) */
    R_DROP,                         /* FUN_100131c0 bouncing pickup (0x4012) */
    R_FX,                           /* generic effect: frame range loop/once, optional fade */
    /* Vortex */
    R_HADES, R_HADES_AURA, R_HADES_METEOR, R_HADES_FLAME, R_HADES_FIRE, R_HADES_TRAIL, R_HADES_SPARK, R_VORTEX_DEBRIS,
    /* Medusa */
    R_MEDUSA, R_MEDUSA_SHADOW, R_MEDUSA_ROCK, R_MEDUSA_GAZE, R_MEDUSA_SKEL, R_MEDUSA_PIECE,
    /* Hydra */
    R_HYDRA_HEAD, R_HYDRA_NECK, R_HYDRA_BODY, R_HYDRA_ROCK, R_HYDRA_FX,
};

/* ------------------------------------------------------------------ shared state */

static struct {
    int level, started, pdead;
    int bar;                        /* DAT_10070832: what the HUD bar still shows (0xff full) */
    long tick;
    int taunt_state, taunt_timer, taunt_last;   /* AUDSHELL: DAT_10070240 / DAT_10070298 / DAT_1007023c (Hades' speech) */
    int dead;                       /* DAT_10072014: the boss is beaten, death sequence running */
    int death_timer;                /* DAT_1007028c */
    Object *boss;
    /* drop lists (FUN_10044f80 / FUN_1001b760 / FUN_100218f0 style): {count, 0 / pairs...} */
    uint32_t drop_base, drop_ptr; int drop_n, drop_m;
    /* Vortex */
    struct {
        int v244;                   /* DAT_10070244: path motion 0 on, -1 paused (attack), 1 resumed */
        int t194, t190;             /* meteor / pickup timers */
        int32_t lastx;              /* DAT_10070288 */
        int fire_active;            /* DAT_1007019c */
        int path_done_dead;         /* DAT_100702a0 */
        /* spline follower DAT_100701b8 (FUN_10044740 / FUN_10045010) */
        int32_t pts[24][4]; int npts, seg, step, nsteps, usez;
        int32_t px, py, pz;
    } v;
} B;

/* ------------------------------------------------------------------ toolkit */

static Object *bspawn(Objects *O, uint16_t type, int32_t x, int32_t y, int32_t z, int role)
{
    Object *o = obj_alloc(O);
    if (!o) return NULL;
    uint32_t script = 0, anims = 0, move = 0, init = 0; int bank = 0;
    obj_type_info(O, type, &script, &anims, &move, &bank, &init);
    o->type = type; o->kind = K_BOSS; o->ai = role;
    o->x = o->px = x; o->y = o->py = y; o->z = o->pz = z;
    o->bank = (uint8_t)bank; o->script = script; o->anims = anims; o->movetab = move;
    o->pairs = (type & 0x7000) == 0;
    o->scale = 0x100; o->bright = 0x80; o->child_bright = 0x80;
    o->facing = 0; o->idx = -1; o->timer = 0;
    return o;
}

static void bkill(Objects *O, Object *o) { obj_kill(O, o, 1); }

static void player_box6(const Player *P, int b[6])
{
    Box pb;
    if (player_body_box(P, &pb)) { b[0] = pb.x0; b[1] = pb.y0; b[2] = pb.z0; b[3] = pb.x1; b[4] = pb.y1; b[5] = pb.z1; }
    else {
        b[0] = I(P->o.x) - 14; b[3] = I(P->o.x) + 14; b[1] = I(P->o.y) - 72; b[4] = I(P->o.y);
        b[2] = I(P->o.z) - 16; b[5] = I(P->o.z) + 16;
    }
}

/* Frame box k scaled by +0x3a (FUN_1002a880 uses the scaled sprite box). */
static int sbox(Objects *O, const Object *o, int k, int b[6])
{
    return obj_box(O, o, k, b);     /* COMBATFIX: obj_box already applies +0x3a once (FUN_10030e40); it was scaled twice here */
}

static int hits_player(Objects *O, const Object *o, int k)     /* FUN_1002a880 + FUN_1002a7a0 */
{
    int b[6], pb[6];
    if (!sbox(O, o, k, b)) return 0;
    player_box6(O->P, pb);
    return obj_boxes_overlap(b, pb);
}

/* FUN_10026de0: 1 = the player's blow, 2 = a projectile (the player's weapon effects, enemies.c AI_PROJ = 6). */
static int took_hit(Objects *O, Object *o, int k)
{
    int b[6];
    if (!sbox(O, o, k, b)) return 0;
    for (int i = 0; i < OBJ_MAX; i++) {
        Object *p = &O->obj[i];
        if (!p->used || p->kind != K_EFFECT || p->ai != 6 || p->life <= 0) continue;
        int X = I(p->x), Y = I(p->y), Z = I(p->z);
        int pb[6] = {X - 10, Y - 10, Z - 24, X + 10, Y + 10, Z + 24};
        if (obj_boxes_overlap(pb, b)) { if (p->variant != 2) p->life = 0; return 2; }   /* FUN_10030290 */
    }
    Box ab;
    if (player_attack_box(O->P, &ab)) {
        int a[6] = {ab.x0, ab.y0, ab.z0, ab.x1, ab.y1, ab.z1};
        if (obj_boxes_overlap(a, b)) return 1;
    }
    return 0;
}

static void hit_sound(Objects *O, int sword)                    /* FUN_1001ab50 / FUN_1001aba0 */
{
    if (O->P->hitflags & 0x18) obj_sound(O, sword, O->P->o.x);
    else obj_sound(O, obj_rnd(O, 0x8000) < 0x4000 ? 0x24 : 0x25, O->P->o.x);
}

static int down_dist(Objects *O, int32_t x, int32_t y, int32_t z)     /* FUN_10020b50 & 0xffff */
{
    int up, down;
    level_probe(O->L, I(x), I(y), I(z), &up, &down, NULL);
    return down;
}

static void drop_floor(Objects *O, Object *o) { obj_drop(O, o); }

/* FUN_10044f80 / FUN_1001b760: walk a drop table of ints {>0 pickup id, 0 m n = "skip unless rnd(n) >= m",
 * <0 restart}; returns 0 for "nothing this time". */
static void drop_list_set(uint32_t base) { B.drop_base = B.drop_ptr = base; B.drop_n = B.drop_m = 0; }
static int drop_list_next(Objects *O)
{
    const PeImage *pe = O->pe;
    if (!B.drop_base) return 0;
    int v = s32(pe, B.drop_ptr);
    for (int guard = 0; guard < 64; guard++) {
        B.drop_ptr += 4;
        if (v >= 1) break;
        if (v == 0) { B.drop_m = s32(pe, B.drop_ptr); B.drop_n = s32(pe, B.drop_ptr + 4); B.drop_ptr += 8; }
        if (v < 0) B.drop_ptr = B.drop_base;
        v = s32(pe, B.drop_ptr);
    }
    int r = obj_rnd(O, B.drop_n);
    if ((unsigned)(r + 1) <= (unsigned)B.drop_m && B.drop_m != B.drop_n) { B.drop_ptr -= 4; return 0; }
    return v;
}

/* FUN_100131c0: a bouncing pickup (0x4012). id & 0xf picks the pickup from the table at 0x10052b90
 * {u16 type, u32 script}; bits 0xe0 give the throw direction, 0x200/0x100/0x400/0x800 the speeds. */
static Object *boss_drop(Objects *O, int32_t x, int32_t y, int32_t z, uint32_t id)
{
    const PeImage *pe = O->pe;
    int k = id & 0xf;
    if (k == 9) k = 2;                      /* letters: handed out in order by the original; coin here */
    uint16_t type = pe_u16(pe, 0x10052b90 + 8u * (uint32_t)k);
    if (!type) return NULL;
    Object *o = bspawn(O, 0x4012, x, y, z, R_DROP);
    if (!o) return NULL;
    o->tag = (uint8_t)type; o->variant = type;                   /* pickup type given on touch */
    o->script = pe_u32(pe, 0x10052b94 + 8u * (uint32_t)k);
    o->pairs = 0; o->idx = 0; o->timer = 0;
    o->life = (id >> 16) & 0xff ? (int)((id >> 16) & 0xff) : 0xfa;   /* +0x97 lifetime */
    o->count126 = 3;                                               /* +0x9a bounces left before it can be taken */
    if (id & 0x800) { o->vy = -0x60000; o->w[0] = 0x5000; }
    else if (id & 0x400) { o->vy = -0x40000; o->w[0] = 0x3000; }
    else { o->vy = -(((int)(obj_rnd(O, 256)) + 0x500) * 0x100); o->w[0] = obj_rnd(O, 0x1000) + 0x4000; }
    o->vx = 0;
    if (id & 0xe0) {
        int dir = (int)((id & 0xe0) >> 6);
        if (dir > 1) dir = obj_rnd(O, 2);
        int sp = (id & 0x200) ? 0x30000 : (id & 0x100) ? 0x10000 : (obj_rnd(O, 0x200) + 0x80) * 0x100;
        o->vx = dir ? -sp : sp;
    }
    return o;
}

static void drop_tick(Objects *O, Object *o)                    /* FUN_10013570 (simplified physics) */
{
    obj_run_script(O, o);
    if (!o->used) return;
    o->x += o->vx;
    o->vy += o->w[0];
    if (o->vy > 0x80000) o->vy = 0x80000;
    int down = down_dist(O, o->x, o->y, o->z);
    if (o->vy > 0 && I(o->vy) >= down && down < 0x7fff) {
        o->y += FX(down);
        if (o->count126) o->count126--;
        if (o->count126 >= 1) obj_sound(O, o->count126 == 1 ? 0x335 : 0x334, o->x);
        o->vy = -o->vy * 2 / 3;
        o->vx /= 2;
        if (iabs(o->vy) < 0x10000) o->vy = 0;
    } else o->y += o->vy;
    if (level_solid(O->L, I(o->x) + (o->vx < 0 ? -32 : 32), I(o->y) - 1, I(o->z), 1)) o->vx = 0;
    int b[6] = {I(o->x) - 10, I(o->y) - 20, I(o->z) - 32, I(o->x) + 10, I(o->y), I(o->z) + 32}, pb[6];
    player_box6(O->P, pb);
    if (o->count126 <= 2 && obj_boxes_overlap(b, pb)) {
        obj_collect(O, (uint16_t)o->variant);
        Object *s = obj_spawn(O, 0x2011, o->x, o->y - FX(8), o->z);
        if (s) s->life = 60;
        bkill(O, o);
        return;
    }
    if (--o->life <= 0) { obj_spawn(O, 0x2011, o->x, o->y - FX(16), o->z); bkill(O, o); }
}

/* Generic frame-range effect: frames w[0]..w[1] every w[2] ticks; w[3] = loops (0 = forever); dies at the end. */
static void fx_tick(Objects *O, Object *o)
{
    if (--o->timer < 1) {
        o->frame++;
        o->timer = (int16_t)o->w[2];
        if (o->frame > o->w[1]) {
            if (o->w[3] && --o->w[3] == 0) { bkill(O, o); return; }
            o->frame = (int16_t)o->w[0];
        }
    }
    if (o->w[4]) { o->scale = (uint16_t)(o->scale - o->w[4]); if (o->scale < 9 || o->scale > 0x8000) { bkill(O, o); return; } }
    if (o->w[5]) o->y += o->w[5];
    if (B.dead && o->w[6]) bkill(O, o);
}

static Object *fx_spawn(Objects *O, uint16_t type, int32_t x, int32_t y, int32_t z, int f0, int f1, int rate, int loops)
{
    Object *o = bspawn(O, type, x, y, z, R_FX);
    if (!o) return NULL;
    o->frame = (int16_t)f0; o->timer = (int16_t)rate;
    o->w[0] = f0; o->w[1] = f1; o->w[2] = rate; o->w[3] = loops;
    o->semi = 1;
    return o;
}

static void boss_bar(Objects *O, int hp, int max) { O->boss_hp = hp < 0 ? 0 : hp; O->boss_hp_max = max; }
static void bar_hit(Objects *O, int d)                          /* DAT_10072184 = d (one tick) */
{
    if (d >= B.bar) d = B.bar;
    if (d < 0) d = 0;
    O->hud_boss_damage = d; B.bar -= d;
}

static void boss_defeated(Objects *O)
{
    O->level_end = 1;
    if (O->P) player_win(O->P);                                   /* FUN_10019240 */
}

/* ================================================================== VORTEX: Hades (level 11) */

enum {
    V_PATH = 0x100696e8,            /* 20-point flight path, 16-byte {x, y, z (16.16), steps} */
    V_RANGE_LO = 0x10069358, V_RANGE_HI = 0x10069370,   /* meteor x ranges */
    V_DEBRIS = 0x10069388,          /* 0x202b state table: 16-byte {first, last, repeat, next-state mask} */
    V_IDLE_R = 0x10069be8, V_IDLE_L = 0x10069ca0, V_TURN_R = 0x10069d58, V_TURN_L = 0x10069df0,
    V_LAUGH_R = 0x10069e88, V_LAUGH_L = 0x1006a010, V_FIRE_R = 0x1006a198, V_FIRE_L = 0x1006a408,
    V_HURT_R = 0x10069988, V_HURT_L = 0x10069ab8,
    SIN_TABLE = 0x1005a848,         /* 0x400 entries, +-0x7fff */
    ATAN_TABLE = 0x10068d78,
};

static int sin_t(Objects *O, int a) { return pe_s16(O->pe, SIN_TABLE + 2u * (uint32_t)(a & 0x3ff)); }

static int angle_to(Objects *O, int32_t x0, int32_t y0, int32_t x1, int32_t y1)    /* FUN_10046110 */
{
    int dx = I(x1 - x0), dy = I(y1 - y0), ax = iabs(dx), ay = iabs(dy), a;
    if (ay == ax) a = ax ? 0x80 : 0;
    else if (ay < ax) a = pe_s16(O->pe, ATAN_TABLE + 2u * (uint32_t)(ay * 0x200 / ax));
    else a = 0x100 - pe_s16(O->pe, ATAN_TABLE + 2u * (uint32_t)(ax * 0x200 / ay));
    if (dx < 0) a = 0x200 - a;
    if (dy < 0) a = 0x400 - a;
    return a;
}

static int angle_diff(int target, int cur)                      /* FUN_100461a0 */
{
    int t = target & 0x3ff, c = cur & 0x3ff, d = c < t ? c - t + 0x400 : c - t;
    return d > 0x1ff ? d - 0x400 : d;
}

/* FUN_10044740 + FUN_10045010: a Catmull-Rom path through points 2..n+1 (point 1 = point 2, point n+2 = n+1),
 * each segment in steps+1 positions. */
static void path_start(int32_t pts[][4], int n, int usez)
{
    memcpy(B.v.pts, pts, sizeof(int32_t) * 4 * (size_t)(n + 3));
    memcpy(B.v.pts[1], B.v.pts[2], sizeof B.v.pts[1]);
    memcpy(B.v.pts[n + 2], B.v.pts[n + 1], sizeof B.v.pts[1]);
    B.v.npts = n; B.v.seg = 2; B.v.step = -1; B.v.nsteps = 0; B.v.usez = usez;
}

static int path_step(void)
{
    if (B.v.step >= 0 && B.v.step < B.v.nsteps) B.v.step++;
    else {
        if (B.v.seg > B.v.npts) return 0;
        B.v.nsteps = B.v.pts[B.v.seg][3]; B.v.step = 0; B.v.seg++;
    }
    int i = B.v.seg - 1;
    double t = (double)B.v.step / (B.v.nsteps + 1);
    int32_t *P0 = B.v.pts[i - 1], *P1 = B.v.pts[i], *P2 = B.v.pts[i + 1], *P3 = B.v.pts[i + 2];
    int32_t *out[3] = {&B.v.px, &B.v.py, &B.v.pz};
    for (int k = 0; k < 3; k++) {
        double p0 = P0[k], p1 = P1[k], p2 = P2[k], p3 = P3[k];
        double a = 1.5 * p1 + 0.5 * p3 - 1.5 * p2 - 0.5 * p0;
        double b = -0.5 * p3 - 2.5 * p1 + 2 * p2 + p0;
        double c = 0.5 * p2 - 0.5 * p0;
        *out[k] = (int32_t)(((a * t + b) * t + c) * t + p1);
    }
    return 1;
}

static void hades_path_default(Objects *O)                     /* FUN_10044720 */
{
    int32_t pts[24][4];
    for (int i = 0; i < 23; i++) for (int k = 0; k < 4; k++) pts[i][k] = s32(O->pe, V_PATH + 16u * (uint32_t)i + 4u * (uint32_t)k);
    path_start(pts, 0x14, 0);
}

/* x limits of Hades by height (the arena's rock walls) */
static int hades_clamp_x(int x, int y)
{
    if (y >= 0x2d6) { if (x < 0x121) x = 0x121; if (x > 0x3e1) x = 0x3e1; }
    if (y > 0x25c) { if (x < 0xa6) x = 0xa6; if (x > 0x470) x = 0x470; }
    if (x < 0x5f) x = 0x5f;
    if (x > 0x4a2) x = 0x4a2;
    return x;
}

static void hades_death_path(Objects *O)                        /* FUN_10045780: swoop over Hercules, then away */
{
    Object *h = B.boss;
    const Obj *p = &O->P->o;
    int32_t pts[24][4];
    memset(pts, 0, sizeof pts);
    int py = I(p->y), px = I(p->x), pz = FX(I(p->z));
    int dist = (int)sqrt((double)(I(h->x) - px) * (I(h->x) - px) + (double)(I(h->y) - py) * (I(h->y) - py)) >> 4;
    if (dist < 2) dist = 2;
    pts[2][0] = FX(I(h->x)); pts[2][1] = FX(I(h->y)); pts[2][2] = FX(I(h->z)); pts[2][3] = dist;
    pts[3][0] = FX(px); pts[3][1] = FX(py); pts[3][2] = pz; pts[3][3] = 0x10;
    int x = px, y = py, spread = 0x80, sign = 1, dy = -0x10;
    for (int i = 4; i <= 20; i++) {
        pts[i][0] = FX(hades_clamp_x(x, y)); pts[i][1] = FX(y); pts[i][2] = pz; pts[i][3] = 0x10;
        x = sign * spread + px; y += dy; spread += 0x20; sign = -sign; dy -= 8;
    }
    pts[21][0] = 0x2000000; pts[21][1] = pz; pts[21][2] = 0; pts[21][3] = 0x10;   /* as in the original table */
    path_start(pts, 0x14, 1);
}

static void hades_set(Object *h, uint32_t script, int next)
{
    h->script = script; h->timer = 0; h->w[0] = next; h->v2a = 1;
}

static void hades_start(Objects *O)                             /* FUN_10044120 + FUN_100445a0 */
{
    Player *P = O->P;
    P->o.x = P->o.px = 0x1350000; P->o.y = P->o.py = 0x2200000; P->o.z = P->o.pz = 0x800000;
    Object *h = bspawn(O, 0x28, 0, 0, 0, R_HADES);
    if (!h) return;
    B.boss = h;
    h->facing = (uint8_t)obj_rnd(O, 2);
    h->x = P->o.x + 0x400000; h->y = P->o.y; h->z = P->o.z;
    hades_set(h, h->facing ? V_IDLE_L : V_IDLE_R, 0);
    h->hp = 200; h->frame = 0x86;
    h->child = 0x91; h->child_semi = 1; h->w[1] = 0;            /* w[1]: child visible (flags54 0x800000) */
    Object *a = bspawn(O, 0x2029, h->x, h->y, h->z, R_HADES_AURA);
    if (a) a->frame = 0x9c;
    hades_path_default(O);
    B.v.v244 = 0;
    B.v.lastx = P->o.x;
    for (int i = 0; i < 30; i++) {                              /* 0x202b: the souls drifting in the vortex */
        Object *d = bspawn(O, 0x202b, 0, 0, 0, R_VORTEX_DEBRIS);
        if (!d) break;
        d->w[3] = 0;                                            /* first spawn: frame 0 -> random fade-in */
        extern void vortex_debris_init(Objects *, Object *);
        vortex_debris_init(O, d);
    }
    obj_sound(O, 0x2c0, h->x);
    static const uint32_t DROPS[3] = {0x10069180, 0x10069220, 0x100692b8};
    int d = P->difficulty; if (d < 0 || d > 2) d = 1;
    drop_list_set(DROPS[d]);
    B.v.t194 = B.v.t190 = 0;
    boss_bar(O, 200, 200);
}

static void hades_tick(Objects *O, Object *h)                   /* FUN_100447c0 */
{
    Player *P = O->P;
    const PeImage *pe = O->pe;
    int dx = I(h->x - P->o.x), dy = I(h->y - P->o.y);
    int right = dx >= 0;                                        /* Hercules is to the left of Hades */
    int child_vis = 0, done = 0;
    for (int guard = 0; guard < 64 && !done; guard++) {
        if (h->v2a == 0) {
            if (h->facing == right) {
                if (dx * dx + dy * dy < 0x24001 && !B.dead) {
                    int r = obj_rnd(O, 2);
                    if (!h->facing) {
                        if (r && !B.v.fire_active) { hades_set(h, V_FIRE_R, 0); B.v.v244 = -1; }
                        else hades_set(h, V_IDLE_R, 0);
                    } else {
                        if (r && !B.v.fire_active) { hades_set(h, V_FIRE_L, 0); B.v.v244 = -1; }
                        else hades_set(h, V_IDLE_L, 0);
                    }
                } else if (obj_rnd(O, 2) == 0) hades_set(h, h->facing ? V_IDLE_L : V_IDLE_R, 0);
                else hades_set(h, h->facing ? V_LAUGH_L : V_LAUGH_R, 0);
            } else hades_set(h, h->facing ? V_TURN_L : V_TURN_R, 2);
        } else if (h->v2a == 1) {
            if (--h->timer >= 1) break;
            done = 1;
            int32_t v = s32(pe, h->script); h->script += 4;
            h->frame = (int16_t)v;
            switch (v) {
            case 0: done = 0; h->v2a = (uint16_t)h->w[0]; break;
            case -4: h->w[2] = s32(pe, h->script); h->script += 4; done = 0; break;     /* palette flash level */
            case -3: {                                                                   /* homing fireball */
                Object *f = bspawn(O, 0x2a, 0, 0, 0, R_HADES_FIRE);
                if (f) {
                    f->bright = 0x80; f->semi = 1; f->facing = h->facing;
                    f->w[0] = h->facing ? 0x200 : 0;                                     /* heading */
                    f->x = h->x + (h->facing ? -0x780000 : 0x780000);
                    f->y = h->y - 0x100000; f->z = h->z;
                    f->frame = 0x1d0; f->w[1] = 1; f->w[2] = 0x10; f->w[3] = 0xf0;      /* turn rate, speed, life */
                    B.v.fire_active = 1;
                    obj_sound(O, 0x4ac9, f->x);
                }
                B.v.v244 = 1; done = 0; break;
            }
            case -2: child_vis = 1; h->child = (int16_t)s32(pe, h->script); h->script += 4; done = 0; break;
            case -1: obj_sound(O, (uint16_t)s32(pe, h->script), h->x); h->script += 4; done = 0; break;
            default:
                h->timer = (int16_t)s32(pe, h->script); h->script += 4;
                h->w[1] = child_vis;
                break;
            }
        } else if (h->v2a == 2) { h->v2a = 0; h->facing ^= 1; }
    }
    /* flight along the path */
    if (B.v.v244 >= 0) {
        int guard = 0;
        while (!path_step() && guard++ < 2) { hades_path_default(O); B.v.path_done_dead = B.dead; }
        if (!B.v.path_done_dead) {
            int y = I(B.v.py), x = hades_clamp_x(I(B.v.px), y);
            h->x = FX(x); h->y = FX(y);
            if (B.v.usez) h->z = FX(I(B.v.pz));
        }
    }
    /* falling meteors near Hercules */
    if (--B.v.t194 < 1) {
        B.v.t194 = obj_rnd(O, 0x5a) + 0xf;
        int x = I(B.v.lastx), best = 999999, bi = 0;
        for (int i = 0; i < 6; i++) {
            int lo = s32(pe, V_RANGE_LO + 4u * (uint32_t)i), hi = s32(pe, V_RANGE_HI + 4u * (uint32_t)i);
            if (iabs(x - lo) < best) { best = iabs(x - lo); bi = i; }
            if (iabs(x - hi) < best) { best = iabs(x - hi); bi = i; }
        }
        int lo = s32(pe, V_RANGE_LO + 4u * (uint32_t)bi), hi = s32(pe, V_RANGE_HI + 4u * (uint32_t)bi);
        if (x < lo) x = lo;
        if (x > hi) x = hi;
        Object *m = bspawn(O, 0x29, FX(x), 0x400000, P->o.z, R_HADES_METEOR);
        if (m) { m->frame = 0x188; m->semi = 1; m->timer = 1; }
        B.v.lastx = P->o.x;
    }
    /* power-ups thrown in from the sides */
    if (--B.v.t190 < 1) {
        B.v.t190 = obj_rnd(O, 0xf0) + 0xf0;
        uint32_t id = 0x20; int32_t x = 0x560000;
        if (obj_rnd(O, 2) == 0) { id = 0x40; x = 0x4a60000; }
        id |= 0xb + (uint32_t)obj_rnd(O, 4);
        boss_drop(O, x, 0x600000, P->o.z, id);
    }
    /* taking hits */
    if (h->inv == 0 && !P->dying && !B.dead) {
        int k = took_hit(O, h, 1);
        if (k) {
            int dmg;
            if (k == 1) { dmg = player_hit_damage(P, (uint16_t)(0x5f & ~P->consumed)); hit_sound(O, 0x10); }
            else dmg = 2;
            if (getenv("BOSS_WEAK")) dmg *= 20;                  /* test hook */
            if (dmg) {
                h->hp = (int16_t)(h->hp - dmg);
                int d = 0x100 / (200 / (dmg > 200 ? 200 : dmg));
                bar_hit(O, h->hp < 1 ? B.bar : (d >= B.bar ? B.bar - 1 : d));
            }
            hades_set(h, h->facing ? V_HURT_L : V_HURT_R, 0);
            B.v.v244 = 0;
            h->inv = 8;
            int id = drop_list_next(O);
            if (id) boss_drop(O, h->x, h->y, P->o.z, (uint32_t)id | (h->x > 0x2800000 ? 0x240u : 0x220u));
        }
        if (h->hp < 1) {
            h->hp = 1;
            B.death_timer = 300;
            B.dead = 1;
            hades_death_path(O);
        }
        boss_bar(O, B.dead ? 0 : h->hp, 200);
    }
    if (B.dead) {
        Object *s = fx_spawn(O, 0x202d, h->x + FX(obj_rnd(O, 0x40) - 0x20), h->y + FX(obj_rnd(O, 0x40) - 0x20), h->z,
                             0x2ad, 0x2b9, 3, 0);
        if (s) { s->w[4] = 6; s->w[5] = 0x20000; }
        if ((B.death_timer & 7) == 0) {
            Object *a = bspawn(O, 0x2029, h->x, h->y, h->z, R_HADES_AURA);
            if (a) { a->frame = 0x9c; a->w[0] = 1; a->semi = 1; }
        }
    }
}

static void hades_aura_tick(Objects *O, Object *o)              /* FUN_10045960 */
{
    if (--o->timer < 1) { o->frame++; o->timer = 3; if (o->frame > 0xb1) o->frame = 0x9c; }
    if (!o->w[0]) { if (B.boss) { o->x = B.boss->x; o->y = B.boss->y; o->z = B.boss->z; } return; }
    o->scale = (uint16_t)(o->scale - 6); o->y -= 0x10000;
    if (o->scale < 9) bkill(O, o);
}

static int hades_hurt(Objects *O, Object *o, int level)         /* FUN_10045bf0: only now and then */
{
    if (obj_rnd(O, 8)) return 0;
    player_damage(O->P, level, o->x, o->y, o->z, 0);
    if (O->P->hit_req == 0) return 0;
    {   /* AUDSHELL: FUN_10045bf0 - Hades gloats (speech 0x70 / 0x71 / 0x73 / 0x75) or a fire roar */
        static int last;
        int k = obj_rnd(O, 5);
        if (k == last && --k < 0) k = 4;
        last = k;
        static const int V[4] = {0x70, 0x71, 0x73, 0x75};
        if (k < 4) sfx_speech(O->fs, V[k]); else obj_sound(O, 0x4acb, 0);
    }
    return 1;
}

static void hades_meteor_tick(Objects *O, Object *o)            /* FUN_10045a80 */
{
    int die = B.dead;
    if (!o->w[2]) { o->w[2] = 1; obj_sound(O, 0xcac1, o->x); }
    if (--o->timer < 1) { o->frame++; o->timer = 1; if (o->frame > 399) o->frame = 0x188; }
    if (o->v2a == 0) {
        int down = down_dist(O, o->x, o->y, o->z);
        if (o->w[0] < down) { o->y += FX(o->w[0]); if (++o->w[0] > 8) o->w[0] = 8; }
        else {
            drop_floor(O, o);
            Object *f = bspawn(O, 0x202a, o->x, o->y, o->z, R_HADES_FLAME);      /* FUN_10045cc0 */
            if (f) {
                f->frame = 0x183; f->semi = 1; f->w[0] = 3; f->timer = 3;
                int r = obj_rnd(O, 4);
                if (r < 3) obj_sound(O, (uint16_t)(0x4ac2 + r), f->x);
            }
            o->v2a = 1;
        }
    } else if (o->v2a == 1) {
        o->scale = (uint16_t)(o->scale - 0x18);
        if (o->scale < 0x19) die = 1;
    }
    if (hits_player(O, o, 1)) hades_hurt(O, o, 0x12);
    if (die) bkill(O, o);
}

static void hades_flame_tick(Objects *O, Object *o)             /* FUN_10045d50 */
{
    int die = B.dead;
    if (--o->timer < 1) {
        o->frame++; o->timer = 3;
        if (o->v2a == 0) { if (o->frame > 0x187) { o->frame = 0x174; o->v2a = 1; } }
        else if (o->v2a == 1) { if (o->frame > 0x17d) { if (--o->w[0] == 0) o->v2a = 2; else o->frame = 0x174; } }
        else if (o->frame > 0x182) die = 1;
    }
    if (hits_player(O, o, 1)) hades_hurt(O, o, 0x11);
    if (die) bkill(O, o);
}

static void hades_fire_tick(Objects *O, Object *o)              /* FUN_10045ef0: homing fireball */
{
    int die = B.dead;
    if (B.death_timer & 1) {
        Object *t = fx_spawn(O, 0x202c, o->x, o->y, o->z, 0x1c8, 0x1cf, 2, 1);
        if (t) t->w[6] = 1;
    }
    if (--o->timer < 1) { o->frame++; o->timer = 2; if (o->frame > 0x1d3) o->frame = 0x1d0; }
    int target = angle_to(O, O->P->o.x, O->P->o.y, o->x, o->y);
    int d = angle_diff(target, o->w[0]);
    if (d > o->w[1]) d = o->w[1];
    if (d < -o->w[1]) d = -o->w[1];
    o->w[0] += d;
    if (o->w[1] < 0x20) o->w[1]++;
    o->x += sin_t(O, o->w[0] + 0x100) * 2 * o->w[2];
    o->y += sin_t(O, o->w[0]) * 2 * o->w[2];
    if (!(B.death_timer & 1) && o->w[2] > 10) o->w[2]--;
    o->rot[2] = (int16_t)(o->rot[2] + (o->facing ? -0x80 : 0x80));
    int up, down, inside = 0;
    level_probe(O->L, I(o->x), I(o->y) - 0x28, I(o->z), &up, &down, &inside);
    if (down >= 0x7fff || level_solid(O->L, I(o->x), I(o->y), I(o->z), 1)) die = 1;
    if (--o->w[3] == 0) die = 1;
    if (hits_player(O, o, 1) && hades_hurt(O, o, 3)) die = 1;
    if (die) {
        if (o->w[3]) obj_sound(O, 0xc2ca, o->x);
        bkill(O, o);
        B.v.fire_active = 0;
    }
}

void vortex_debris_init(Objects *O, Object *o)                 /* FUN_10046230 */
{
    o->bright = 0;
    if (o->frame == 0) o->bright = (int16_t)(obj_rnd(O, 0x60) & 0xfe);
    o->frame = 0x1c7; o->semi = 1;
    o->timer = 0; o->w[0] = 0; o->w[1] = 1;                     /* +0x36 state, +0x3c repeats */
    o->x = FX(obj_rnd(O, 0x4c0) + 0x40);
    o->y = FX(obj_rnd(O, 0x2b0) + 0xd0);
    int32_t pz = O->P ? O->P->o.z : 0x800000;
    if (obj_rnd(O, 2) == 0) o->z = FX(obj_rnd(O, 0x100) + 0x18) + pz;
    else o->z = pz - FX(obj_rnd(O, 0x80)) - 0x180000;
    o->w[2] = obj_rnd(O, 2);                                    /* +0x4c direction bits */
    o->v2a = 0;
    o->w[3] = obj_rnd(O, 8) + 2;                                /* +0x21 speed */
}

static void vortex_debris_tick(Objects *O, Object *o)           /* FUN_10046320 */
{
    const PeImage *pe = O->pe;
    if (o->v2a == 0) { if (++o->bright == 0x60) o->v2a = 1; }
    else if (o->v2a == 1 && --o->bright == 0) {
        if (B.dead) { bkill(O, o); return; }
        vortex_debris_init(O, o);
    }
    int st = o->w[0];
    if (--o->timer < 1) {
        o->frame++; o->timer = 3;
        if (o->frame >= s32(pe, V_DEBRIS + 4 + 16u * (uint32_t)st)) {
            if (--o->w[1] == 0) {
                int r = obj_rnd(O, 6);
                uint32_t mask = pe_u32(pe, V_DEBRIS + 12 + 16u * (uint32_t)st);
                for (int g = 0; g < 8 && !(mask & (1u << (r & 31))); g++) r = r - 1 < 0 ? r + 5 : r - 1;
                o->w[0] = r;
                o->w[1] = pe_s16(pe, V_DEBRIS + 8 + 16u * (uint32_t)r);
                if (!o->w[1]) o->w[1] = obj_rnd(O, 0x10) + 2;
                if (st == 0) o->w[2] ^= 1;
                if (st == 5) vortex_debris_init(O, o);
            }
            st = o->w[0];
            o->frame = (int16_t)s32(pe, V_DEBRIS + 16u * (uint32_t)st);
        }
    }
    if (st == 1) o->x += (o->w[2] & 1) ? -FX(o->w[3]) : FX(o->w[3]);
    else if (st > 3 && st < 6) o->y += (o->w[2] & 2) ? FX(o->w[3]) : -FX(o->w[3]);
    if (o->w[2] & 2) { o->rot[2] = 0x200; o->flags54 = (o->w[2] & 1) ? o->flags54 & ~0x10u : o->flags54 | 0x10; }
    else { o->rot[2] = 0; o->flags54 = (o->w[2] & 1) ? o->flags54 | 0x10 : o->flags54 & ~0x10u; }
}

static void vortex_level_tick(Objects *O)                       /* FUN_10044240 */
{
    /* AUDSHELL: Hades' taunts (FUN_1002c4c0 -> speech clip n = PC_<1000+n>.ESF, in the VORTEX bank) */
    if (!sfx_speech_busy()) {
        if (B.taunt_state == 0) B.taunt_state = 1;
        else if (B.taunt_state == 1) {
            int pdead = O->P->dead || O->P->dying;
            if (!pdead) {
                if (B.dead) {                                       /* beaten: one last line (0x42 / 0x6d) */
                    sfx_speech_stop();
                    int r = obj_rnd(O, 2);
                    if (r < 2) sfx_speech(O->fs, r == 0 ? 0x42 : 0x6d);
                    B.taunt_state++;
                } else if (--B.taunt_timer < 1) {
                    B.taunt_timer = obj_rnd(O, 0xf0) + 0x78;
                    int k = obj_rnd(O, 4);
                    if (k == B.taunt_last && --k < 0) k = 3;
                    B.taunt_last = k;
                    static const int V[4] = {0x41, 0x6c, 0x40, 0x6e};
                    sfx_speech(O->fs, V[k]);
                }
            } else {                                                /* Hercules fell: Hades crows (0x6f / 0x74) */
                sfx_speech_stop();
                int r = obj_rnd(O, 2);
                if (r < 2) sfx_speech(O->fs, r == 0 ? 0x6f : 0x74);
                B.taunt_state++;
            }
        }
    }
    B.death_timer--;
    {   /* FX2: DAT_10071848 camera depth offset - pull the camera back so Hercules and Hades both stay on screen.
         * margin = Hercules' distance to the nearest screen edge, sep = the largest distance from Hercules to
         * three points on Hades (FUN_1002c030 hypot); off = (margin - sep) * 2, limited to -0x100 and to the
         * level's x range 0x40..0x400. */
        Object *h = B.boss;
        int off = 0;
        if (h && h->used && h->ai == R_HADES) {
            int cx = O->camx - 0xa0, cy = O->cam_y - 0x78;
            int hx = I(O->P->o.x), hy = I(O->P->o.y);
            int m = hx - cx, t;
            if ((t = cx + 0x140 - hx) < m) m = t;
            if ((t = hy - cy) < m) m = t;
            if ((t = cy + 0xf0 - hy) < m) m = t;
            int bx = I(h->x), by = I(h->y), sep = 0;
            static const int PX[3] = {-0x16, 0x16, 0x16}, PY[3] = {-0x50, -0x50, 0};
            for (int k = 0; k < 3; k++) {
                int dx = bx - hx + PX[k], dy = by - hy + PY[k];
                int d = (int)sqrt((double)dx * dx + (double)dy * dy);
                if (d > sep) sep = d;
            }
            if (m < sep) off = (m - sep) * 2;
            if (off < -0x100) off = -0x100;
            if (cx + off < 0x40) off = 0x40 - cx;
            if (0x400 < cx - off) off = cx - 0x400;
        }
        O->cam_zoff = off;
        if (getenv("FX2_LOG") && (B.tick % 30) == 0 && h) printf("zoff=%d hades=(%d,%d) herc=(%d,%d) cam=(%d,%d)\n", off, I(h->x), I(h->y), I(O->P->o.x), I(O->P->o.y), O->camx, O->cam_y);
    }
    if (obj_rnd(O, 0x96) == 0) obj_sound(O, (uint16_t)(0x2cc + obj_rnd(O, 4)), O->P->o.x);
    if (B.dead && B.death_timer < 1 && !O->level_end) boss_defeated(O);
}

/* ================================================================== MEDUSA (level 7) */

#include "bosses_medusa.inc"

/* ================================================================== HYDRA (level 6) */

#include "bosses_hydra.inc"

/* ================================================================== dispatch */

void bosses_level_tick(Objects *O)
{
    if (!O->P) return;
    O->hud_boss_cmd = 0; O->hud_boss_damage = 0;
    B.tick++;
    if (B.started && O->P->dead) B.pdead = 1;
    else if (B.started && B.pdead && !O->P->dead && !O->P->dying) {      /* respawned: the original restarts the level */
        for (int i = 0; i < OBJ_MAX; i++) if (O->obj[i].used && O->obj[i].kind == K_BOSS) obj_kill(O, &O->obj[i], 1);
        B.started = 0;
    }
    if (!B.started || B.level != O->L->index) {
        memset(&B, 0, sizeof B);
        B.level = O->L->index; B.started = 1;
        O->boss_hp = O->boss_hp_max = 0; O->level_end = 0;
        switch (B.level) {
        case 6: hydra_start(O); break;
        case 7: medusa_start(O); break;
        case 11: hades_start(O); break;
        default: break;
        }
        if (B.level == 6 || B.level == 7 || B.level == 11) { O->hud_boss_cmd = 5; B.bar = 0xff; }   /* DAT_10072188 = 5: bar fades in */
        int tx, ty, tz;                                         /* test hook: "x,y,z" after the boss start */
        if (getenv("BOSS_TELEPORT") && sscanf(getenv("BOSS_TELEPORT"), "%d,%d,%d", &tx, &ty, &tz) == 3) {
            O->P->o.x = O->P->o.px = FX(tx); O->P->o.y = O->P->o.py = FX(ty); O->P->o.z = O->P->o.pz = FX(tz);
        }
        return;
    }
    if (getenv("BOSS_GOD")) O->P->health = 0x80;          /* test hook: Hercules can't die */
    if (getenv("BOSS_LOG") && B.tick % atoi(getenv("BOSS_LOG")) == 0) {     /* test hook */
        printf("  [boss t=%ld] hp %d/%d dead %d timer %d done %d", B.tick, O->boss_hp, O->boss_hp_max, B.dead, B.death_timer, O->level_end);
        if (B.boss) printf(" boss@%d,%d,%d v2a %d frame %d", I(B.boss->x), I(B.boss->y), I(B.boss->z), B.boss->v2a, B.boss->frame);
        printf(" player %d,%d hp %d won %d", I(O->P->o.x), I(O->P->o.y), O->P->health, O->P->won);
        if (B.level == 6) for (int k = 0; k < 8; k++) if (H.h[k].neck) printf(" h%d:%d/%d/b%d@%d,%d,%d", k, H.h[k].neck->v2a, H.h[k].neck->count126, H.h[k].bite, H.h[k].head ? I(H.h[k].head->x) : 0, H.h[k].head ? I(H.h[k].head->y) : 0, H.h[k].head ? I(H.h[k].head->z) : 0);
        if (B.level == 7) {
            printf(" beams %d/%d cols", M.beam[0][0], M.beam[1][0]);
            for (int c = 0; c < 8; c++) printf(" %d", M.col[c].state);
            printf(" skels %d", M.skels);
        }
        printf("\n");
    }
    switch (B.level) {
    case 6: hydra_level_tick(O); break;
    case 7: medusa_level_tick(O); break;
    case 11: vortex_level_tick(O); break;
    default: break;
    }
}

void bosses_start(Objects *O) { B.started = 0; (void)O; }

void boss_tick(Objects *O, Object *o)
{
    switch (o->ai) {
    case R_DROP: drop_tick(O, o); break;
    case R_FX: fx_tick(O, o); break;
    case R_HADES: hades_tick(O, o); break;
    case R_HADES_AURA: hades_aura_tick(O, o); break;
    case R_HADES_METEOR: hades_meteor_tick(O, o); break;
    case R_HADES_FLAME: hades_flame_tick(O, o); break;
    case R_HADES_FIRE: hades_fire_tick(O, o); break;
    case R_VORTEX_DEBRIS: vortex_debris_tick(O, o); break;
    default: medusa_role_tick(O, o) || hydra_role_tick(O, o); break;
    }
}

static void draw_frame(Objects *O, Renderer *R, const Object *o, int bank, int frame, int semi, int bright, float bias)
{
    Bank *b = obj_bank(O, bank);
    if (!b || frame <= 0) return;
    const Image *img = bank_frame(b, frame);
    if (!img) return;
    float wx, wz;
    level_path_to_world(O->L, o->x / 65536.0f, o->z / 65536.0f, &wx, &wz, NULL);
    float sc = (o->scale ? o->scale : 0x100) / 256.0f;
    int lvl = bright * 2; if (lvl > 255) lvl = 255; if (lvl < 0) lvl = 0;
    render_sprite_xf(R, img, wx, o->y / 65536.0f, wz, (o->flags54 & 0x10) != 0, bias, semi ? 1 : -1, lvl, sc,
                     (float)(o->rot[2] * 4));
}

void boss_draw(Objects *O, Renderer *R, Object *o)
{
    if (hydra_draw(O, R, o)) return;
    if (o->ai == R_MEDUSA_GAZE) return;                         /* FX2: gaze parts are drawn as chains by medusa_draw_beams */
    if (B.level == 7 && o->w[11] == 1) return;                  /* hidden (flags54 bit 31 clear) */
    if (o->flags54 & 0x10000000) {                              /* 3D map model */
        if (o->x || o->y || o->z) boss_model_draw(O, R, o);
        return;
    }
    if (o->ai == R_HADES && o->w[1] && o->child) {
        /* FX: script op -4 n loads 32-colour palette n of table 0x100693e8 (FUN_100446d0 -> DAT_10070248) and
         * FUN_100447c0 uploads it every frame into the hair flame's CLUT slot (FUN_10005820(pal, slot, 0x20):
         * entry 0 transparent, 1..31 from the table, repeated every 32 entries). */
        int n = (int)o->w[2];
        Bank *b = obj_bank(O, o->bank);
        if (b && n >= 0 && n < 16) {
            uint32_t pal[256];
            const uint8_t *t = pe_ptr(O->pe, 0x100693e8u + 0x60u * (uint32_t)n, 0x60);
            if (t) {
                for (int i = 0; i < 256; i++) {
                    int k = i & 31;
                    if (!k) { pal[i] = 0; continue; }
                    uint32_t r = (t[3 * k] >> 3) * 255 / 31, g = (t[3 * k + 1] >> 3) * 255 / 31, bl = (t[3 * k + 2] >> 3) * 255 / 31;
                    pal[i] = 0xff000000u | r << 16 | g << 8 | bl;
                }
                const Image *img = bank_frame_cpal(b, o->child, n, pal);
                if (img) {
                    float wx, wz;
                    level_path_to_world(O->L, o->x / 65536.0f, o->z / 65536.0f, &wx, &wz, NULL);
                    float sc = (o->scale ? o->scale : 0x100) / 256.0f;
                    int lvl = o->child_bright * 2; if (lvl > 255) lvl = 255; if (lvl < 0) lvl = 0;
                    render_sprite_xf(R, img, wx, o->y / 65536.0f, wz, (o->flags54 & 0x10) != 0, -5, 1, lvl, sc,
                                     (float)(o->rot[2] * 4));
                }
            }
        } else draw_frame(O, R, o, o->bank, o->child, 1, o->child_bright, -5);
    }
    if (o->ai == R_MEDUSA_SKEL && o->w[3] && o->child) draw_frame(O, R, o, 78, o->child, 1, 0x80, -5);
    if (o->ai == R_MEDUSA_SHADOW) { draw_frame(O, R, o, o->bank, o->frame, 2, 0x40, 2); return; }
    draw_frame(O, R, o, o->bank, o->frame, o->semi, o->semi ? o->bright : 0x80, -4);
    if (B.level == 7 && o == M.medusa) medusa_draw_beams(O, R);
}
