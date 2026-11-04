/* FXW: the player's weapon effects (Gifts of the Gods) and the small effect objects they create.
 *
 * ENG1 sources (all addresses are in ENG1.DLL):
 *   - stream (weapon 1, state 0x2b, object type 0x4001): init FUN_1001a440, update FUN_1001a5a0. A chain of 17 sprites
 *     (frames 5..20 of bank 2 = the small blue electric zig-zags, draw mode 4 sprite mesh FUN_100304f0 with
 *     flags54 0x20700000: semi-transparent additive, brightness 0xe0). Each segment is 16 px long, its direction
 *     a0 - sum(bend), bend = sin(phase)*0x24>>16 + deviation/2; the phase advances 0xbc +- 63 per segment and starts at
 *     tick << 7, so the chain crackles every tick. When the aim angle changes the old chain is copied into a short lived
 *     0x2004 afterimage (frames 23 / 25 / 27, FUN_1001a950). A segment that touches solid ground ends the chain and
 *     throws a 0x2010 spark (FUN_10012a10 / FUN_10012b00).
 *   - bolt (weapon 2, state 0x2c, type 0x4005): init FUN_10019f50, update FUN_1001a0f0. A homing orb (frames 29..32, orange swirl,
 *     additive, brightness 0xe0, drawn at the player's depth - 0x10, spinning 0x80 per tick) that leaves a 0x2003 trail puff
 *     (frames 55..60, additive 0xa0, random 45 degree rotation) every second tick, costs 12 charges, steers with
 *     FUN_1002b5e0 towards the nearest target within 0x30 depth / 0x140 distance and turns into a 0x4008 (terrain) or
 *     0x4009 (enemy hit) burst through FUN_10030290.
 *   - sonic (weapon 3, state 0x2d, type 0x4006): two stationary wave sprites (bank 2 frames 61..68 behind, 69..76 in front, additive
 *     0xa0) placed at the player (-8, -48) when script op 0x0c runs; they live for one script pass (16 ticks), their frame box 2
 *     (first orb only) damages (FUN_1001ab00).
 * See NOTES_fxw.md. */
#include "objects_int.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AI_PROJ 6
enum {
    AIM_LIGHT = 0x10054d60,         /* 6 x {angle, frame, dx, dy, shot angle} (the bolt's aim table) */
    VEL_TBL = 0x10055448,           /* {vx, vy} 16.16 per aim angle (angle >> 2 & 0x3e words) */
    LAG_TBL = 0x100553a8,           /* per segment: how far a segment may trail the aim angle */
    SINTAB = 0x1005a848,
};

static int g_tick;                  /* DAT_10070f24 + 0x38 */
static int g_param;                 /* DAT_10072198: parameter of the 0x2010 spark being created */

static int sintab(Objects *O, int a) { return pe_s16(O->pe, SINTAB + 2u * (uint32_t)(a & 0x3ff)); }

void fxw_reset(void) { g_tick = 0; }

/* ------------------------------------------------------------------ small effect objects */

static int px_x(Objects *O, int a) { return (sintab(O, a - 0x200) << 1) >> 12; }     /* FUN_1001a5a0: x += sin(a - 0x200) */
static int px_y(Objects *O, int a) { return (sintab(O, a - 0x100) << 1) >> 12; }     /* FUN_1001a5a0: y -= sin(a - 0x100) */

/* Spark / trail / burst objects: created from the object system's spawn path (script op -0x1f, FUN_100300c0). */
int fxw_init(Objects *O, Object *o, uint32_t init)
{
    switch (init) {
    case 0x1001a1d0:                                    /* 0x2003: bolt trail puff */
        o->kind = K_FXW;
        o->flags54 |= 0x2005; o->flags50 |= 0;
        o->bright = 0xa0;
        o->fxwd = 1; o->fxw_dd = -0x10;
        o->rot[2] = (int16_t)((g_tick & 7) << 7);       /* (DAT_10070f24 + 0x38 & 7) << 7 */
        o->life = 200;
        return 1;
    case 0x10012a10:                                    /* 0x2010: spark (hit / wall impact) */
        o->kind = K_FXW;
        o->flags54 |= 5;
        o->rot[2] = (int16_t)(o->rot[2] + obj_rnd(O, 0x400));
        o->bright = 0x80;
        o->v2a = (uint16_t)g_param;
        if (g_param == 5) {
            o->script = 0x100535b8; o->vy = 0x10000;
            if (obj_rnd(O, 2)) o->vy = -o->vy;
        } else if (g_param >= 1 && g_param <= 4) { o->script = 0x100535b0; o->vy = g_param * 0x10000; }
        else o->script = 0x100535a8;
        o->bank = 2;
        o->life = 200;
        return 1;
    case 0x1000d380:                                    /* 0x4008 / 0x4009 (and 0x4025): plain script objects, FUN_1000ee70 update */
        if (o->type != 0x4008 && o->type != 0x4009) return 0;
        o->kind = K_FXW; o->life = 200;
        return 1;
    default:
        return 0;
    }
}

static const int SPARK_JIT[16] = {0, 0, 2, 0, 1, 0, 0, 0, 0, 0, -2, 0, -1, 0, 0, 0};   /* FUN_10012b00: aiStack_40 */

void fxw_tick(Objects *O, Object *o)
{
    if (o->type == 0x2010) {                            /* FUN_10012b00 */
        if (o->v2a != 0 && o->v2a < 5) o->y += o->vy;
        else if (o->v2a == 5) {
            o->y += o->vy;
            o->x += o->x < O->P->o.x ? 0x20000 : -0x20000;
        }
        o->b21++;
        o->rot[2] = (int16_t)(o->rot[2] + 4);
        o->x += SPARK_JIT[o->b21 & 15] * 0x10000;
        if (o->bright > 0xc) o->bright = (int16_t)(o->bright - 0xc); else o->bright = 0;
        obj_run_script(O, o);
        return;
    }
    obj_run_script(O, o);                               /* 0x2003 / 0x4008 / 0x4009: FUN_10028820; -6 ends them */
    if (!o->used) return;
    if ((o->type == 0x4008 || o->type == 0x4009) && en_offscreen(O, o, 0x84)) obj_kill(O, o, 1);
}

/* FUN_10030290: a spent projectile becomes its burst effect (row type & 0xf of table 0x10051ac0: bolt -> 0x4009 on an enemy
 * (param 0), 0x4008 on terrain (param 1)). The object keeps its flags (additive, brightness, depth, spin). */
static void explode(Objects *O, Object *o, int on_terrain)
{
    uint16_t nt = on_terrain ? 0x4008 : 0x4009;
    uint32_t script, anims, list, init; int bank;
    if (!obj_type_info(O, nt, &script, &anims, &list, &bank, &init)) { obj_kill(O, o, 1); return; }
    if (O->nsounds < 64) { O->sound_x[O->nsounds] = (int)(o - O->obj) + 2; O->sound_owner[O->nsounds] = -4; O->sounds[O->nsounds++] = 0; }   /* the crackle ends */
    o->type = nt; o->bank = (uint8_t)bank; o->script = script; o->idx = 0; o->timer = 0;
    o->kind = K_FXW; o->ai = 0; o->life = 200; o->frame = 0;
    obj_run_script(O, o);                               /* FUN_10028820 */
}

/* ------------------------------------------------------------------ bolt */

/* FUN_10030010 / FUN_1002a170: attribute flags at a point, solid objects first, 0x8000 = passable. */
static int attr_f(Objects *O, int x, int y, int z)
{
    int a = objects_attr(O, x, y, z);
    if (a < 0) a = level_attr(O->L, x, y, z);
    if (a < 0 || a > 127) return 0;
    uint16_t f = O->L->flags[a];
    return (f & 0x8000) ? 0 : f;
}

static Object *find_target(Objects *O, const Object *o)    /* FUN_1002aea0(o, 0x30, 0x140) */
{
    Object *best = NULL;
    int bd = 0x140;
    for (int i = 0; i < OBJ_MAX; i++) {
        Object *e = &O->obj[i];
        if (!e->used || (e->kind != K_ENEMY && e->kind != K_BOSS && e->kind != K_TITAN) || e->inv || e->hidden) continue;
        int b[6];
        if (!obj_box(O, e, 1, b)) continue;
        int dz = abs(I(e->z) - I(o->z));
        if (dz >= 0x30) continue;
        int dx = abs(I(o->x) - I(e->x)), dy = abs(I(o->y) - I(e->y));
        int lo = dx < dy ? dx : dy, hi = dx < dy ? dy : dx;
        int d = (lo >> 2) + (lo >> 3) + hi;
        if (d < bd) { bd = d; best = e; }
    }
    return best;
}

/* FUN_1002b5e0, the parts the bolt uses: flags 3 = home in x and y on (tx, ty); 0x40 = accelerate along x. v[] = +0xa8..:
 * 0 vx, 1 vxmax, 2 xacc, 3 xdec, 4 vy, 5 vymax, 6 yacc, 7 ydec. facing = +0x94 (0 right), b95 = +0x95 (nonzero up). */
static void steer(Object *o, int flags, int32_t tx, int32_t ty)
{
    if (flags & 1) {
        int slow;
        if (!o->facing) slow = tx < o->x; else slow = !(tx <= o->x);
        if (slow) o->v[0] -= o->v[3]; else o->v[0] += o->v[2];
        if (o->v[0] < 0) { o->v[0] = 0; o->facing = (uint8_t)~o->facing; }
        if (o->v[1] < o->v[0]) o->v[0] = o->v[1];
    }
    if (flags & 2) {
        int slow;
        if (!o->b95) slow = ty < o->y; else slow = !(ty <= o->y);
        if (slow) o->v[4] -= o->v[7]; else o->v[4] += o->v[6];
        if (o->v[4] < 0) { o->v[4] = 0; o->b95 = (uint8_t)~o->b95; }
        if (o->v[1] < o->v[4]) o->v[4] = o->v[5];       /* the original compares with the x limit and clamps to the y one */
    }
    if (flags & 0x40) {
        o->v[0] += o->v[2];
        if (o->v[0] < 0) o->v[0] = 0;
        if (o->v[1] < o->v[0]) o->v[0] = o->v[1];
    }
    o->x += o->facing ? -o->v[0] : o->v[0];
    o->y += o->b95 ? -o->v[4] : o->v[4];
}

static void bolt_tick(Objects *O, Object *o)           /* FUN_1001a0f0 */
{
    if (o->life <= 0) { explode(O, o, 0); return; }     /* an enemy took it (FUN_10030290(.., 0)) */
    obj_run_script(O, o);                               /* frames 29..32 and the 0x2003 trail */
    o->rot[2] = (int16_t)(o->rot[2] - 0x80);
    Object *t = o->w[0] > 0 && o->w[0] <= OBJ_MAX ? &O->obj[o->w[0] - 1] : NULL;
    if (t && (!t->used || t->inv)) t = NULL;
    if (!t) t = find_target(O, o);
    o->w[0] = t ? (int)(t - O->obj) + 1 : 0;
    if (t) {
        int b[6]; int32_t tx = t->x, ty = t->y;
        if (obj_box(O, t, 0, b) || obj_box(O, t, 1, b)) { tx = (int32_t)(b[0] + (b[3] - b[0]) / 2) << 16; ty = (int32_t)(b[1] + (b[4] - b[1]) / 2) << 16; }
        steer(o, 3, tx, ty);
    } else steer(o, 0x40, 0, 0);
    int up, down, inside = 0;
    level_probe(O->L, I(o->x), I(o->y), I(o->z), &up, &down, &inside);
    if (inside || (attr_f(O, I(o->x), I(o->y), I(o->z)) & 1)) { explode(O, o, 1); return; }   /* FUN_10029f50 / FUN_10030010 */
    if (en_offscreen(O, o, t ? 0x100 : 0x40)) obj_kill(O, o, 1);                                    /* FUN_1002aa30 */
}

/* ------------------------------------------------------------------ sonic orbs */

static Object *orb[2];

static int orb_alive(Object *p) { return p && p->used && p->type == 0x4006; }

static void orb_place(Objects *O, Object *p, int slot)     /* FUN_1001a2b0 */
{
    Player *P = O->P;
    p->x = p->px = P->o.x - 0x80000; p->y = p->py = P->o.y - 0x300000; p->z = p->pz = P->o.z;
    p->script = slot ? 0x100554c0 : 0x100554a8;
    p->scale = 0x100; p->rot[1] = 0; p->rot[2] = 0;
    p->idx = 0; p->timer = 0; p->frame = 0;
    p->flags54 = (p->flags54 & ~0x10u) | 0x2005; p->bright = 0xa0;
    p->facing = P->o.facing;
    p->fxwd = 1; p->fxw_dd = (int16_t)(slot ? -0x10 - 0x20 : -0x10 + 0x20);   /* +0x3e = player depth + 0x20 (first) / - 0x20 (second), minus 0x10 */
    p->bank = 2; p->tag = (uint8_t)slot;
    obj_run_script(O, p);                               /* FUN_10028820 */
}

static void orb_kill_both(Objects *O)
{
    for (int i = 0; i < 2; i++) if (orb_alive(orb[i])) obj_kill(O, orb[i], 1);
    orb[0] = orb[1] = NULL;
    if (O->P) O->P->sonic_active = 0;
}

static void orb_tick(Objects *O, Object *o)            /* FUN_1001a390 */
{
    if (o != orb[0] && o != orb[1]) { obj_kill(O, o, 1); return; }
    if (obj_run_script(O, o)) orb_kill_both(O);         /* FUN_1001a280 destroys both once the script has run through */
}

/* ------------------------------------------------------------------ fireball / lightning stream (type 0x4001) */

#define NSEG 20
typedef struct { int angle, n, bend[NSEG]; int32_t x, y, z; } Chain;
static struct {
    int active, age, target, n, nmax;
    int lag[NSEG], bend[NSEG], frm[NSEG];
    int32_t x, y, z; int angle;
    float cx[NSEG], cy[NSEG], ca[NSEG];                 /* segment positions (px) and directions */
} S;
static struct { int life, step; Chain c; int32_t x, y, z; } AF[4];
static int af_next;

static void chain_pos(Objects *O, int n, int angle, const int *bend, int32_t x0, int32_t y0, float *cx, float *cy, float *ca)
{
    int theta = 0, x = x0 >> 16, y = y0 >> 16;
    for (int k = 0; k < n; k++) {
        theta += bend[k];
        int a = angle - theta;
        if (k > 0) { x += px_x(O, a); y -= px_y(O, a); }
        cx[k] = (float)x; cy[k] = (float)y; ca[k] = (float)a;
    }
}

static void spark(Objects *O, int32_t x, int32_t y, int32_t z, int param)
{
    uint32_t script, anims, list, init; int bank;
    if (!obj_type_info(O, 0x2010, &script, &anims, &list, &bank, &init)) return;
    Object *s = obj_alloc(O);
    if (!s) return;
    s->type = 0x2010; s->x = s->px = x; s->y = s->py = y; s->z = s->pz = z; s->bank = 2; s->script = 0; s->pairs = 0;
    s->init_fn = 0x10012a10; s->spawn = -1;
    g_param = param;
    fxw_init(O, s, 0x10012a10);
    s->idx = 0; s->timer = 0;
}

void fxw_frame(Objects *O)
{
    Player *P = O->P;
    if (!P || O->L->eng3) return;
    g_tick++;
    if (getenv("FXW_LOG")) {   /* test hook */
        int b[6] = {0}, hb = orb_alive(orb[0]) ? obj_box(O, orb[0], 2, b) : -1;
        printf("FXW bosshp=%d t=%d kills=%d orbbox=%d (%d,%d,%d)-(%d,%d,%d) stream=%d n=%d\n", O->boss_hp, g_tick, O->kills, hb, b[0], b[1], b[2], b[3], b[4], b[5], S.active, S.n);
        for (int i = 0; i < OBJ_MAX; i++) if (O->obj[i].used && (O->obj[i].type == 0x4005 || O->obj[i].type == 0x4008 || O->obj[i].type == 0x4009)) printf("   fx %04x pos %d,%d,%d v=%d,%d tgt=%d\n", O->obj[i].type, I(O->obj[i].x), I(O->obj[i].y), I(O->obj[i].z), O->obj[i].v[0] >> 16, O->obj[i].v[4] >> 16, O->obj[i].w[0]);
        for (int i = 0; i < OBJ_MAX; i++) if (O->obj[i].used && O->obj[i].kind == K_ENEMY && abs(I(O->obj[i].x) - I(P->o.x)) < 200) printf("   enemy %04x x=%d hp=%d inv=%d v2a=%d\n", O->obj[i].type, I(O->obj[i].x), O->obj[i].hp, O->obj[i].inv, O->obj[i].v2a);
    }
    if (getenv("FXW_SPARKTEST") && g_tick % 20 == 5) spark(O, P->o.x + FX(60), P->o.y - FX(50), P->o.z, 5);   /* test hook */
    for (int i = 0; i < 4; i++)
        if (AF[i].life && ++AF[i].step > 2) AF[i].life = 0;
    if (!P->flame.active) { S.active = 0; return; }
    int a = P->flame.angle;
    if (!S.active) {                                    /* FUN_1001a440 */
        S.active = 1; S.age = 0; S.target = a;
        for (int k = 0; k < NSEG; k++) { S.lag[k] = a; S.bend[k] = 0; }
        S.nmax = P->state == 9 ? 20 : 17;               /* obj+0x36 - 3 unless hanging from a pole */
        S.n = S.nmax;
    } else {
        S.age++;
        if (a != S.target) {                            /* FUN_1001a5a0: the old chain fades out as a 0x2004 afterimage */
            AF[af_next].life = 1; AF[af_next].step = 0;
            AF[af_next].c.angle = S.target; AF[af_next].c.n = S.n;
            memcpy(AF[af_next].c.bend, S.bend, sizeof S.bend);
            AF[af_next].x = S.x; AF[af_next].y = S.y; AF[af_next].z = S.z;
            af_next = (af_next + 1) & 3;
            S.target = a;
        }
    }
    S.x = P->flame.x; S.y = P->flame.y; S.z = P->flame.z; S.angle = a;
    S.n = S.nmax;
    int fb = 5 + 2 * ((S.age / 2) % 8);                 /* script 0x100554f0: frames 5, 7 .. 19, two ticks each */
    int cur = fb;
    int phase = g_tick << 7;
    int ph_step = ((int)sintab(O, (g_tick & 0x3f) * 16) * 0x7e >> 16) + 0xbc;
    for (int k = 0; k < S.n; k++) {
        int lag = S.lag[k], d = S.target - lag;
        if (d != 0) {
            int lim = pe_u16(O->pe, LAG_TBL + 2u * (uint32_t)k);
            if (lim < abs(d)) d = d < 0 ? d + lim : d - lim; else d = 0;
        }
        S.lag[k] = lag + d;
        S.bend[k] = ((sintab(O, phase & 0x3ff) * 0x24) >> 16) + d / 2;
        S.frm[k] = cur;
        if (++cur > 0x14) cur = 5;
        phase += ph_step;
    }
    chain_pos(O, S.n, a, S.bend, S.x, S.y, S.cx, S.cy, S.ca);
    for (int k = 0; k < S.n; k++) {                     /* a segment inside solid ground ends the chain */
        int ix = (int)S.cx[k], iy = (int)S.cy[k] + 8, iz = I(S.z);
        if ((attr_f(O, ix, iy, iz) & 0x201) == 1) {
            S.n = k + 1;
            if (!(g_tick & 1)) spark(O, (int32_t)ix << 16, (int32_t)S.cy[k] << 16, S.z, 5);
            break;
        }
    }
}

/* ------------------------------------------------------------------ creation (P->spawns / P->flame) */

static Object *make(Objects *O, uint16_t type, int32_t x, int32_t y, int32_t z)
{
    uint32_t script = 0, anims, list, init; int bank = 0;
    obj_type_info(O, type, &script, &anims, &list, &bank, &init);
    Object *p = obj_alloc(O);
    if (!p) return NULL;
    p->type = type; p->x = p->px = x; p->y = p->py = y; p->z = p->pz = z;
    p->bank = (uint8_t)bank; p->script = script; p->pairs = 0; p->idx = 0; p->timer = 0;
    p->spawn = -1; p->kind = K_EFFECT; p->ai = AI_PROJ; p->life = 1; p->variant = 0;
    return p;
}

void fxw_spawns(Objects *O)
{
    Player *P = O->P;
    if (!P) return;
    int norb = 0;
    for (int i = 0; i < P->nspawns; i++) {
        const Spawn *s = &P->spawns[i];
        if (s->id == 0x4005) {                          /* FUN_10019f50 */
            int row = s->aim < 0 ? 0 : s->aim > 5 ? 5 : s->aim;
            int16_t e[5];
            for (int k = 0; k < 5; k++) e[k] = pe_s16(O->pe, AIM_LIGHT + 10u * (uint32_t)row + 2u * (uint32_t)k);
            Object *p = make(O, 0x4005, s->x + FX(e[2]), s->y + FX(e[3]), s->z);
            if (!p) continue;
            p->facing = (uint8_t)s->facing; p->b95 = 0xff;
            int idx = (e[0] >> 2) & 0x3e;
            p->v[0] = (int32_t)pe_u32(O->pe, VEL_TBL + 4u * (uint32_t)idx);
            p->v[4] = (int32_t)pe_u32(O->pe, VEL_TBL + 4u * (uint32_t)idx + 4);
            p->v[1] = 0xf0000; p->v[2] = 0xa000; p->v[3] = 0xf000;
            p->v[5] = 0xa0000; p->v[6] = 0x8000; p->v[7] = 0xa000;
            p->flags54 = 0x2005; p->bright = 0xe0; p->fxwd = 1; p->fxw_dd = -0x10;
            p->cool = 100000;
            obj_sound_obj(O, 0x4811, p);                /* the bolt's crackle follows it */
        } else if (s->id == 0x4006) {                   /* FUN_1001a220 */
            int slot = norb++ & 1;
            Object *p = orb[slot];
            if (!orb_alive(p)) {
                p = make(O, 0x4006, P->o.x, P->o.y, P->o.z);
                if (!p) continue;
                orb[slot] = p; p->variant = 2; p->cool = 100000;
            }
            orb_place(O, p, slot);
        }
    }
    if (P->flame.active) {                              /* damage: one box per chain segment, refreshed every second tick */
        if (!(g_tick & 1)) {
            for (int k = 0; k < S.n && S.active; k++) {
                Object *p = make(O, 0x4001, (int32_t)S.cx[k] << 16, (int32_t)S.cy[k] << 16, S.z);
                if (!p) break;
                p->bank = 0; p->script = 0; p->frame = 0;
                p->facing = P->o.facing; p->cool = 3;
            }
        }
    }
}

int fxw_proj_tick(Objects *O, Object *o)
{
    if (o->type == 0x4005) { bolt_tick(O, o); return 1; }
    if (o->type == 0x4006) { orb_tick(O, o); return 1; }
    return 0;
}

int fxw_proj_box(Objects *O, Object *p, int box[6])
{
    if (p->type != 0x4006) return -1;                   /* generic */
    if (p->tag) return 0;                               /* only the first orb hurts (FUN_1001ab00) */
    return obj_box(O, p, 2, box);
}

/* ------------------------------------------------------------------ drawing */

float fxw_player_depth(Objects *O, Renderer *R)
{
    const Player *P = O->P;
    float wx, wz, sx, sy, zc;
    level_path_to_world(O->L, P->o.x / 65536.0f, P->o.z / 65536.0f, &wx, &wz, NULL);
    if (!render_project(R, wx, P->o.y / 65536.0f, wz, &sx, &sy, &zc)) return 0;
    return floorf(zc * 0.5f);
}

static void draw_chain(Objects *O, Renderer *R, int n, const float *cx, const float *cy, const float *ca, const int *frm,
                       int32_t z, int bright)
{
    Bank *b = obj_bank(O, 2);
    if (!b) return;
    for (int k = 0; k < n; k++) {
        const Image *img = bank_frame(b, frm[k]);
        if (!img) continue;
        float wx, wz;
        level_path_to_world(O->L, cx[k], (float)I(z), &wx, &wz, NULL);
        uint32_t tint = (uint32_t)bright * 0x010101u;
        if (render_sprite_fx(R, img, wx, cy[k], wz, 0, -9, 0x100, tint, 1, 1.0f))
            render_last_angle(R, ca[k] * 4.0f);
    }
}

void fxw_draw(Objects *O, Renderer *R)
{
    if (!O->P || O->L->eng3) return;
    if (S.active && S.n > 0) draw_chain(O, R, S.n, S.cx, S.cy, S.ca, S.frm, S.z, 0xe0);
    for (int i = 0; i < 4; i++) {
        if (!AF[i].life) continue;
        float cx[NSEG], cy[NSEG], ca[NSEG]; int frm[NSEG];
        chain_pos(O, AF[i].c.n, AF[i].c.angle, AF[i].c.bend, AF[i].x, AF[i].y, cx, cy, ca);
        for (int k = 0; k < AF[i].c.n; k++) frm[k] = 23 + 2 * AF[i].step + (k & 1);
        draw_chain(O, R, AF[i].c.n, cx, cy, ca, frm, AF[i].z, 0xe0);
    }
}
