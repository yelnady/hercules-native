/* ENEMIES2: ports of the ENG1.DLL actor behaviours the first enemies pass left generic.
 * See analysis/objects_notes.md "Enemies, part 2".
 *
 *   Thebes minotaur mini-boss   0x37 init 10024270 / update2 10024320 / AI callback 6 = 100244d0
 *     falling boulders 0x4029   10024f50 (spawned by the stomp handler 10024c60)
 *     held rock 0x41            100246c0 / 10024780, shatter 10024980
 *     boss bar start            Thebes per-frame hook 10033720
 */
#include "objects_int.h"
#include "solids.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum {
    AI2_BASE = 100,
    AI2_MINO, AI2_MROCK, AI2_BOULDER,
    AI2_ROCKC, AI2_RCROCK, AI2_ONESHOT, AI2_WRESTLE, AI2_GRIFFON, AI2_SHARK, AI2_PEG, AI2_WATER, AI2_MACHINE,
    AI2_EQROCK,
};

enum {
    H_MINOTAUR = 0x10024270, H_MROCK = 0x100246c0,
    H_ROCKC = 0x10008660, H_RCROCK = 0x100099e0, H_WRESTLE = 0x10009190, H_GRIFFON = 0x1000d760, H_GRIFFON2 = 0x1000ddd0,
    H_SHARK_A = 0x100410b0, H_SHARK_B = 0x10041460, H_PEG = 0x10041f50,
    H_WATER = 0x1000d2c0, H_MACH_A = 0x1002d9b0, H_MACH_B = 0x1002da10,
    H_EQROCK = 0x10034650,      /* AUD: Thebes 0x402a falling rock of the THEQ earthquake girls (update 10034750) */
};

#define P_ (O->P)
#define HUD_BOSS_START_MB 2      /* hud.h HUD_BOSS_START (DAT_10072188 = 2) */
#define HUD_BOSS_END_MB 7        /* hud.h HUD_BOSS_END */
static int iabs(int v) { return v < 0 ? -v : v; }
static int rnd(Objects *O, int n) { return obj_rnd(O, n); }
static int rnd16(Objects *O) { return obj_rnd(O, 0x10000); }          /* FUN_1002c470 */
static void mirror(Object *o) { o->flags54 = (o->flags54 & ~0x10u) | (o->facing & 0x10); }
static Level *mut_level(Objects *O) { return (Level *)O->L; }

/* ---------------------------------------------------------------- toolkit */

/* FUN_10029f50 / FUN_10029e20 for objects: landed on the floor (map or platform object)? */
static int land(Objects *O, Object *o)
{
    int up, down;
    level_probe(O->L, I(o->x), I(o->y), I(o->z), &up, &down, NULL);
    int dy = iabs(I(o->y - o->py)), dx = iabs(I(o->x - o->px)), dz = iabs(I(o->z - o->pz));
    if (down == 0) return 1;
    if (up != 0x7fff && up <= (dy + dx + dz) * 2 + 16) { o->y -= FX(up); return 1; }
    return 0;
}

/* FUN_1002bd20: v[] = {vx, vxmax, xacc, xdec, vy, vymax, yacc, ydec, vz, vzmax, zacc, zdec}; b95 = moving up.
 * Returns bit 0 = landed this tick, bit 1 = landed and stopped bouncing. */
static int phys(Objects *O, Object *o)
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

/* FUN_1002a880 for any object: world box k (model box for bank-0 objects, else sprite frame box). */
static int wbox(Objects *O, const Object *o, int k, int b[6])
{
    if (o->bank == 0 && (o->flags54 & 0x10000000u)) {
        int r[6];
        if (!model_box(O->L, o, k, r)) return 0;
        int X = I(o->x), Y = I(o->y), Z = I(o->z);
        if (!(o->flags54 & 0x10)) { b[0] = X + r[0]; b[3] = b[0] + r[3]; } else { b[3] = X - r[0]; b[0] = b[3] - r[3]; }
        b[1] = Y + r[1]; b[4] = b[1] + r[4];
        b[2] = Z + r[2]; b[5] = b[2] + r[5];
        return 1;
    }
    return obj_box(O, o, k < 1 ? 1 : k, b);
}

static int overlap(Objects *O, const Object *a, const Object *b)    /* FUN_1002a660: box 0 vs box 0 */
{
    int x[6], y[6];
    return wbox(O, a, 0, x) && wbox(O, b, 0, y) && obj_boxes_overlap(x, y);
}

static int touch_player(Objects *O, const Object *o)               /* FUN_1002a710(player +0x88, o) */
{
    int b[6], pb[6];
    if (!wbox(O, o, 0, b)) return 0;
    en_player_box6(P_, pb);
    return obj_boxes_overlap(b, pb);
}

static int blow_on(Objects *O, const Object *o)                    /* FUN_1002a710(DAT_10071440, o), DAT_1006fec4 */
{
    Box ab;
    int b[6];
    if (!player_attack_box(P_, &ab) || !wbox(O, o, 0, b)) return 0;
    int a[6] = {ab.x0, ab.y0, ab.z0, ab.x1, ab.y1, ab.z1};
    return obj_boxes_overlap(a, b);
}

/* FUN_10031000: the frame's attach point (after the collision boxes), mirrored with the sprite. */
static int attach_point(Objects *O, const Object *o, int *ax, int *ay)
{
    *ax = *ay = 0;
    Bank *b = obj_bank(O, o->bank);
    if (!b || o->frame < 1 || o->frame > b->count) return 0;
    const uint8_t *d = b->d;
    uint32_t e = d[4 + 4 * o->frame] | d[5 + 4 * o->frame] << 8 | d[6 + 4 * o->frame] << 16 | (uint32_t)d[7 + 4 * o->frame] << 24;
    if (!e) return 0;
    uint32_t f = (e >> 8) + 4;
    int n = (int8_t)d[f + 1];
    if (f + 8 + 6u * (uint32_t)n > b->size) return 0;
    *ax = (int8_t)d[f + 6 + 6 * n];
    *ay = (int8_t)d[f + 7 + 6 * n];
    if (o->flags54 & 0x10) *ax = -*ax;
    return 1;
}

/* FUN_1002bf30: angle of dy/dx from the tangent table at 0x1005b048 (0x168 entries), 0x4000 per step. */
static int atan_step(Objects *O, int dx, int dy)
{
    if (!dx) return 0x5a0000;
    int t = (int)(((int64_t)dy << 16) / dx);
    int lo = 0, hi = 0x167;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if ((int32_t)pe_u32(O->pe, 0x1005b048 + 4u * (uint32_t)mid) <= t) lo = mid; else hi = mid - 1;
    }
    return lo << 14;
}

static int cos_tab(Objects *O, int a)                               /* FUN_1002bef0 */
{
    return (int)pe_s16(O->pe, 0x1005a848 + 2u * (uint32_t)(((a / 0x2d >> 9) + 0x100) & 0x3ff)) << 1;
}

/* Rubble models 0x4028 (frames 9 / 10, FUN_10024b60 = the solids rubble update). */
static Object *rubble_piece(Objects *O, int32_t x, int32_t y, int32_t z)
{
    Object *p = obj_alloc(O);
    if (!p) return NULL;
    p->kind = K_SOLID; p->init_fn = 1; p->type = 0x4028; p->bank = 0;     /* solids.c I_RUBBLE */
    p->x = p->px = x; p->y = p->py = y; p->z = p->pz = z;
    p->flags54 |= 0x10000000u;
    p->life = 90;
    return p;
}

/* ---------------------------------------------------------------- Thebes minotaur */

static void mino_damage(Objects *O, int dmg)
{
    Object *m = O->mino;
    O->hud_boss_damage += dmg;
    if (O->mb_bar - dmg < 1) {
        O->mb_bar = 0;
        obj_set_anim(O, m, 0x1d);
        /* the doorways out of the arena open (attribute flags 0xb / 0xc) */
        mut_level(O)->flags[0xb] = 0x10;
        mut_level(O)->flags[0xc] = 0x20;
        O->hud_boss_cmd = HUD_BOSS_END_MB;
    } else {
        O->mb_bar -= dmg;
        obj_set_anim(O, m, 0x1a);
    }
}

/* FUN_10024c60: a ground pound during the fight brings down rubble and, every few stomps, a boulder. */
static void mino_stomp(Objects *O, Object *m)
{
    int diff = P_->difficulty;
    if (--m->cool < 0) {
        m->cool = diff == 0 ? rnd(O, 2) + 2 : diff == 1 ? rnd(O, 3) + 3 : 999;
        Object *b = obj_spawn(O, 0x4029, P_->o.x, m->y - FX(0x100), m->z);
        if (!b) b = obj_alloc(O);
        if (b) {
            b->type = 0x4029; b->kind = K_ENEMY; b->ai = AI2_BOULDER; b->bank = 0;
            b->x = b->px = P_->o.x; b->y = b->py = m->y - FX(0x100); b->z = b->pz = m->z;
            b->flags54 |= 0x10000040u;
            b->frame = pe_s16(O->pe, 0x100601ac + 2u * (uint32_t)rnd(O, 8));
            b->b95 = 0xff; b->zface = 0xff;
            b->v[4] = 0; b->v[5] = ((rnd16(O) & 0x1ff) + 0xc00) * 0x100; b->v[6] = 0; b->v[7] = rnd(O, 0x2000) + 0x8000;
            b->v[9] = 0xf0000; b->v[8] = 0; b->v[10] = 0; b->v[11] = 0;
            b->v2a = 0; b->scale = 0x100;
        }
    }
    int n = rnd(O, 7) + 10;
    for (int i = n; i > 0; i--) {
        Object *p = rubble_piece(O, FX(rnd(O, 0x80) - 0x40) + P_->o.x, m->y - FX(0x100), m->z);
        if (!p) break;
        int k = rnd(O, 0x1b) + 0xb;
        p->s9a = (int16_t)k;
        p->frame = pe_ptr(O->pe, 0x1005ef60 + (uint32_t)k, 1) ? pe_ptr(O->pe, 0x1005ef60 + (uint32_t)k, 1)[0] : 9;
        p->scale = (uint16_t)(rnd(O, n * -3 + 0xa0) + n * -3 + 0x40);
        p->facing = (uint8_t)-rnd(O, 2); p->zface = 0xff; p->b95 = 0xff;
        p->v[4] = 0; p->v[5] = ((rnd16(O) & 0x1ff) + 0xa00) * 0x100; p->v[6] = 0; p->v[7] = rnd(O, 0x2000) + 0x4000;
        p->v[9] = p->v[8] = (rnd16(O) & 0x1ff) << 8; p->v[10] = rnd(O, 0x1000); p->v[11] = 0;
        p->v[1] = p->v[0] = (rnd16(O) & 0x1ff) << 8; p->v[2] = rnd(O, 0x1000); p->v[3] = 0;
        p->d[0] = rnd(O, 8) << 2; p->d[1] = rnd(O, 8) << 2; p->d[2] = rnd(O, 8) << 2;
    }
}

/* FUN_10024980: the rock breaks into a dust puff and 8..18 pieces. */
static void rock_shatter(Objects *O, Object *r)
{
    Object *d = obj_spawn(O, 0x2027, r->x, r->y - FX(0x20), r->z);
    if (d) d->spawn = -1;
    obj_sound(O, 0xc150, r->x);
    int n = rnd(O, 0xb) + 8;
    for (int i = n; i > 0; i--) {
        Object *p = rubble_piece(O, FX(rnd(O, 0x40) - 0x20) + r->x, r->y - FX(rnd(O, 0x30)), r->z);
        if (!p) break;
        p->frame = rnd(O, 2) ? 9 : 10;
        p->scale = (uint16_t)(rnd(O, n * -3 + 0xa0) + n * -3 + 0x48);
        p->facing = (uint8_t)-rnd(O, 2); p->zface = (uint8_t)-rnd(O, 2); p->b95 = 0xff;
        p->v[6] = 0; p->v[5] = p->v[4] = ((rnd16(O) & 0x1ff) + 0x800) * 0x100; p->v[7] = rnd(O, 0x2000) + 0x4000;
        p->v[9] = p->v[8] = ((rnd16(O) & 0xff) + 0x200) * 0x100; p->v[10] = rnd(O, 0x1000) + 0x2000; p->v[11] = 0;
        p->v[1] = p->v[0] = ((rnd16(O) & 0x1ff) + 0x100) * 0x100; p->v[2] = rnd(O, 0x1000) + 0x1000; p->v[3] = 0;
        p->d[0] = rnd(O, 16) - 8; p->d[2] = rnd(O, 16) - 8;
    }
}

void thebes_rock_shatter(Objects *O, Object *r) { rock_shatter(O, r); }   /* LV_THEBES: FUN_10025190 (the port's rock_shatter has 8..18 pieces, the original 24..31) */

static void mino_init(Objects *O, Object *o)                       /* FUN_10024270 */
{
    O->mino_seq = 0xffff;
    o->v2a = 0;                 /* entry +0xe: 9 once defeated (the port does not persist it) */
    O->mino = o;
    o->anim = 0; o->script = pe_u32(O->pe, o->anims); o->idx = -1; o->timer = 0;
    if (o->v2a == 9) obj_set_anim(O, o, 0x20);
    else o->v2a = 0;
    o->facing = 0xff;
    o->flags50 = 2;
    o->flags54 = 0x80080000u;
    o->cbid = 6;
    o->cool = 0;
    o->hp = (int16_t)(P_ ? P_->difficulty * 2 + 6 : 8);
    o->ai = AI2_MINO; o->moving = -1;
    obj_drop(O, o);
}

/* FUN_10024320 */
static void mino_tick(Objects *O, Object *o)
{
    obj_run_script(O, o);
    if (!o->used) return;
    O->mino_v2a = o->v2a;
    if (o->v2a == 8) o->z -= 0x40000;                              /* defeated: walks off into the back */
    if (!obj_ground_follow(O, o)) { o->x = o->px; o->y = o->py; o->z = o->pz; }
    mirror(o);
    if (O->mb_active) {
        if (P_->hitflags & 0x20 & ~P_->consumed) { P_->consumed |= 0x20; mino_stomp(O, o); }
        if (o->v2a < 7) {
            /* thrown objects (flags50 & 0x800000, DAT_10071be0) hitting it: FUN_10026d50 */
            for (int i = 0; i < OBJ_MAX; i++) {
                Object *t = &O->obj[i];
                if (!t->used || t == o || !(t->flags50 & 0x800000) || !overlap(O, o, t)) continue;
                int dmg = 0x100 / (P_->difficulty * 2 + 6) + 1;
                if (--o->hp < 1) dmg = 300;
                t->flags50 &= ~0x800000u;
                mino_damage(O, dmg);
                return;
            }
        }
    }
    if (touch_player(O, o)) en_hurt_player(O, o, 3);
    { Object *r = thebes_loose_rock(O); if (r && overlap(O, r, o)) { thebes_rock_shatter(O, r); obj_kill(O, r, 0); } }   /* LV_THEBES: FUN_10024320 tail */
}

/* FUN_100244d0 (callback 6) */
static void mino_ai(Objects *O, Object *o)
{
    int32_t px = P_->o.x;
    int dx = iabs(I(px - o->x)), dy = iabs(I(P_->o.y - o->y));
    if (o->v2a == 0) {
        if (px < o->x && o->facing == 0) { obj_set_anim(O, o, 0x14); return; }
        if (o->x < px && o->facing == 0xff) { obj_set_anim(O, o, 0x14); return; }
        /* FUN_10024680: next entry of the attack pattern (0x1005a060; 0 = jump to the index after it) */
        O->mino_seq = (O->mino_seq + 1) & 0xffff;
        int v = pe_s16(O->pe, 0x1005a060 + 2u * (uint32_t)O->mino_seq);
        if (v == 0) { O->mino_seq = pe_u16(O->pe, 0x1005a062 + 2u * (uint32_t)O->mino_seq); v = pe_s16(O->pe, 0x1005a060 + 2u * (uint32_t)O->mino_seq); }
        switch (v) {
        case 2: obj_set_anim(O, o, 1); return;
        case 3: obj_set_anim(O, o, 2); return;
        case 6:
            if (!O->mino_rock || !O->mino_rock->used) {
                obj_set_anim(O, o, 0xf);
                Object *r = obj_spawn(O, 0x41, o->x, o->y, o->z);   /* FUN_10030180: the rock it picks up */
                if (r) { r->facing = o->facing; O->mino_rock = r; }   /* +0x96 is set by the init */
                O->mino_seq = (O->mino_seq + 1) & 0xffff;
                o->s9a = pe_s16(O->pe, 0x1005a060 + 2u * (uint32_t)O->mino_seq);
                return;
            }
            break;
        }
        obj_set_anim(O, o, 0);
    } else if (o->v2a == 2) {
        o->facing = px < o->x ? 0xff : 0;
        if (o->s9a) { o->s9a--; return; }
        obj_set_anim(O, o, dx < 0x50 ? 0x17 : 0x11);
        o->v[1] = cos_tab(O, atan_step(O, dx, dy)) << 4;
    }
}

static void mrock_init(Objects *O, Object *o)                       /* FUN_100246c0 */
{
    o->v2a = 0; o->frame = 0x18; o->flags54 |= 0x10000000u; o->scale = 0x100;
    o->zface = 0xff; o->b95 = 0;
    o->v[5] = o->v[4] = 0x100000; o->v[7] = 0; o->v[6] = rnd(O, 0x2000) + 0x3000;
    o->v[10] = 0x4000; o->v[9] = o->v[8] = ((rnd16(O) & 0x1ff) + 0xa80) * 0x100; o->v[11] = 0;
    o->v[1] = o->v[0] = 0; o->v[2] = rnd(O, 0x2000) + 0x3000; o->v[3] = 0;
    o->ai = AI2_MROCK; o->bank = 0; o->script = 0; o->moving = -1;
}

/* FUN_10024780 */
static void mrock_tick(Objects *O, Object *o)
{
    Object *m = O->mino;
    if (o->v2a != 0) {                                              /* thrown */
        o->rot[0] += 0x40;
        if (phys(O, o) & 1) { rock_shatter(O, o); obj_kill(O, o, 1); return; }
    } else if (!m || !m->used || O->mino_rock != o) {
        rock_shatter(O, o); obj_kill(O, o, 1); return;
    } else {
        int ax, ay;
        switch (m->v2a) {
        case 2: case 4: case 5:
            attach_point(O, m, &ax, &ay);
            o->x = m->x + FX(ax); o->y = m->y + FX(ay); o->facing = m->facing;
            break;
        case 6:                                                     /* let go */
            m->v2a = 0;
            o->v[0] = o->v[1] = m->v[1];
            o->v[9] = o->v[8] = o->v[9] + m->v[1] / 6;
            O->mino_rock = NULL;
            o->v2a = 5;
            break;
        case 7: case 8:
            rock_shatter(O, o); obj_kill(O, o, 1); return;
        }
    }
    if (blow_on(O, o)) {                                            /* Hercules smashes it: a pickup falls out */
        rock_shatter(O, o);
        static const int ids[3] = {2, 3, 4};
        en_drop_pickup(O, o, O->mino_drop < 3 ? ids[O->mino_drop] : 1);
        O->mino_drop++;
        if (O->mino_rock == o) O->mino_rock = NULL;
        obj_kill(O, o, 1);
        return;
    }
    if (touch_player(O, o)) en_hurt_player(O, o, 3);
    if (en_offscreen(O, o, 0x100)) { if (O->mino_rock == o) O->mino_rock = NULL; obj_kill(O, o, 1); }
}

/* FUN_10024f50: boulder from a stomp; rolls out towards the camera after landing. */
static void boulder_tick(Objects *O, Object *o)
{
    Object *m = O->mino;
    o->rot[0] += (int16_t)o->d[0];
    if (m && m->used && m->v2a != 7 && m->v2a != 8 && overlap(O, m, o)) {
        int dmg = (0x100 / (P_->difficulty * 2 + 6)) * 2 + 3;
        m->hp -= 2;
        if (m->hp < 1) dmg = 300;
        mino_damage(O, dmg);
    }
    o->y += 0x180000; o->py += 0x180000;
    if (phys(O, o) & 1) {
        if (o->v[10] == 0) obj_sound(O, 0x4150, o->x);
        if (o->v[10] == 0x4000) obj_sound(O, 0x4151, o->x);
        if (o->v[10] == 0x8000) obj_sound(O, 0x4152, o->x);
        o->v[9] += 0x20000; o->v[10] += 0x4000; o->d[0] += 0x20;
    }
    o->y -= 0x180000; o->py -= 0x180000;
    if (touch_player(O, o)) en_hurt_player(O, o, 3);
    { Object *r = thebes_loose_rock(O); if (r && overlap(O, r, o)) { thebes_rock_shatter(O, r); obj_kill(O, r, 0); } }   /* LV_THEBES: FUN_10024f50 tail */
    if (o->z < -0x800000 || o->z > 0x1f00000 || en_offscreen(O, o, 0x80)) obj_kill(O, o, 1);
}

/* AUD: FUN_10034650 / FUN_10034750: the rocks the stomping THEQ girls shake loose (0x402a). A model (table 0x1005ef60) drops from
 * 300 units up near the girl, tumbles as it moves, hurts Hercules on contact (level 1), bounces, and a big or low-numbered one
 * shatters into rubble (FUN_10024980) on its first landing. */
static void eqrock_init(Objects *O, Object *o)
{
    o->ai = AI2_EQROCK; o->kind = K_ENEMY;
    o->flags54 |= 0x10000040u;
    o->y -= FX(300);
    o->x += FX(rnd(O, 200) - 100);
    o->z += FX(rnd(O, 200) - 100);
    int k = rnd(O, 0x26);
    o->s9a = (int16_t)k;
    o->frame = pe_ptr(O->pe, 0x1005ef60 + (uint32_t)k, 1) ? pe_ptr(O->pe, 0x1005ef60 + (uint32_t)k, 1)[0] : 9;
    if (k > 0xf) o->scale = (uint16_t)(rnd(O, 0x200) + 0x80);
    o->b95 = 0xff; o->zface = 0xff;
    o->v[5] = ((rnd16(O) & 0x1ff) + 0xc00) * 0x100; o->v[4] = 0; o->v[6] = 0;
    o->v[7] = rnd(O, 0x2000) + 0x8000;
    o->v[8] = 0; o->v[10] = 0; o->v[11] = 0; o->v[9] = 0xf0000;
    o->px = o->x; o->py = o->y; o->pz = o->z;
}

static void eqrock_tick(Objects *O, Object *o)
{
    if (getenv("EQ_LOG")) printf("EQROCK pos %d,%d,%d v4 %d frame %d scale %d\n", I(o->x), I(o->y), I(o->z), o->v[4], o->frame, o->scale);
    o->rot[0] = (int16_t)(0x400 - ((o->z >> 13) & 0x3ff));
    o->rot[1] = (int16_t)(0x400 - ((o->x >> 13) & 0x3ff));
    if (o->v2a == 1) { if (phys(O, o) & 2) { obj_kill(O, o, 1); return; } }
    else if (phys(O, o) & 1) {
        if (o->scale > 0x180 || o->s9a < 0xb) {
            obj_sound(O, 0xd150, o->x);
            rock_shatter(O, o);
            obj_kill(O, o, 1); return;
        }
        if (o->v[10] == 0 && rnd(O, 2)) obj_sound(O, o->s9a < 0x18 ? 0xd151 : 0xd152, o->x);
        o->v[8] = 0x40000;
        o->v[10] += 0x8000;
    }
    if (touch_player(O, o)) en_hurt_player(O, o, 1);
    if (o->z > -0x400001 && o->z < 0x1f00001 && o->y < 0x5800001 && !en_offscreen(O, o, 0x80)) return;
    obj_kill(O, o, 1);
}

/* Thebes per-frame hook FUN_10033720: the fight starts when Hercules enters the arena. */
static void thebes_frame(Objects *O)
{
    int32_t x = P_->o.x, y = P_->o.y;
    { static int tk, done; if (getenv("THEBES_MINO_KILL") && !done && ++tk >= atoi(getenv("THEBES_MINO_KILL")) && O->mino && O->mino->used) { done = 1; O->mb_active = 1; mino_damage(O, 300); } }   /* LV_THEBES: test hook */
    if (!O->mb_active && O->mino && O->mino->used && O->mino_v2a < 8 && x > 0x15100000 && x < 0x17800000 && y > 0x4400000) {
        O->mb_active = 1; O->mb_bar = 0xff; O->hud_boss_cmd = HUD_BOSS_START_MB;
    }
}


/* ---------------------------------------------------------------- Forest rock centaur (0x28) and its rock (0x4a) */

/* Any type, created from its type info without running an init (for types whose init is a no-op here). */
static Object *spawn_raw(Objects *O, uint16_t type, int32_t x, int32_t y, int32_t z)
{
    uint32_t script = 0, anims = 0, list = 0, init = 0; int bank = 0;
    if (!obj_type_info(O, type, &script, &anims, &list, &bank, &init)) return NULL;
    Object *p = obj_alloc(O);
    if (!p) return NULL;
    p->type = type; p->x = p->px = x; p->y = p->py = y; p->z = p->pz = z;
    p->bank = (uint8_t)bank; p->script = script; p->anims = anims; p->movetab = list;
    p->pairs = 0; p->idx = 0; p->timer = 0; p->moving = -1;
    return p;
}

static int water_y(Objects *O)                                      /* DAT_100718e0: Training 0x360, Forest 0x2e1 */
{
    if (O->L->index == 1) return 0x360;
    return O->P && O->P->water_y ? O->P->water_y : 0x2e1;
}

/* Step x by dx (facing-signed) unless box 1's leading edge runs into a wall (attribute & 2). */
static void step_x(Objects *O, Object *o, int32_t dx)
{
    o->home[0] = o->x;
    o->x += o->facing ? -dx : dx;
    int b[6];
    if (obj_box(O, o, 1, b)) {
        int edge = o->facing ? b[0] : b[3];
        if (obj_attr_flags(O, edge, I(o->y), I(o->z)) & 2) o->x -= o->facing ? -dx : dx;
    }
}

/* FUN_1000cfb0: the rock breaks into eight 0x403x fragments (table 0x100510e0 {dx, dy, dz, type}). */
static void rock_bits(Objects *O, Object *r)
{
    obj_sound(O, 0xcc, r->x);
    for (uint32_t a = 0x100510e0; a < 0x10051140; a += 8) {
        Object *p = spawn_raw(O, pe_u16(O->pe, a + 6), r->x + FX(pe_s16(O->pe, a)), r->y + FX(pe_s16(O->pe, a + 2)), r->z + FX(pe_s16(O->pe, a + 4)));
        if (!p) continue;
        p->kind = K_DEBRIS; p->facing = 0;
        p->vx = (rnd(O, 0xa00) - 0x500) * 0x100;
        p->vy = (-0x200 - rnd(O, 0x600)) * 0x100;
        p->life = 40;
        obj_run_words(O, p);
    }
}

static void oneshot_tick(Objects *O, Object *o)                     /* FUN_1000d440: play once, then remove */
{
    if (obj_run_words(O, o)) obj_kill(O, o, 1);
}

static void splash(Objects *O, int32_t x, int32_t y, int32_t z)    /* actor 0x49 (Water bank), init FUN_1000d410 */
{
    Object *p = spawn_raw(O, 0x49, x, y, z);
    if (p) { p->kind = K_ENEMY; p->ai = AI2_ONESHOT; p->scale = 0x180; }
}

static void rockc_init(Objects *O, Object *o)                       /* FUN_10008660 */
{
    o->ai = AI2_ROCKC;
    o->flags50 = 0x212; o->scale = 0x200; o->hp = 100;
    o->b21 = (uint8_t)((I(o->z) + 0x40) >> 7);
    o->facing = 0xff; o->flags54 |= 0x10;
    o->v2a = 10; o->v[0] = 0x20000;
    obj_set_anim(O, o, 2);
    obj_run_script(O, o);
    obj_drop(O, o);
    mirror(o);
}

static void rockc_patrol(Objects *O, Object *o)
{
    o->v2a = 10; o->v[0] = 0x20000; obj_set_anim(O, o, 2);
}

/* FUN_100086f0 */
static void rockc_tick(Objects *O, Object *o)
{
    int W = water_y(O), run = 1;
    switch (o->v2a) {
    case 9:                                                         /* walking (second half) */
        step_x(O, o, o->v[0]);
        if (o->idx == 3 && o->timer == 0 && I(o->y) < W) obj_sound(O, 0xe0de, o->x);
        if (!obj_run_script(O, o)) { run = 0; break; }
        step_x(O, o, -0x320000);                                    /* turn round: the turn frames start 50 back */
        o->v[0] = 0;
        obj_drop(O, o);
        o->v2a = 0xb; mirror(o);
        obj_set_anim(O, o, 3);
        break;
    case 10:                                                        /* walking */
        step_x(O, o, o->v[0]);
        if (o->idx == 3 && o->timer == 0 && I(o->y) < W) obj_sound(O, 0xe0dd, o->x);
        obj_drop(O, o);
        if (obj_run_script(O, o)) { obj_set_anim(O, o, 1); obj_run_script(O, o); o->v2a = 9; }
        run = 0;
        break;
    case 0xb: {                                                     /* turned: throw if Hercules is in range */
        if (!obj_run_script(O, o)) { run = 0; break; }
        int prow = (I(P_->o.z) + 0x40) >> 7, rows = o->b21 - prow;
        int dx = o->x - P_->o.x;
        if (rows > 3 || (I(o->y) > W && P_->o.y < FX(W)) || rows < 1 || dx > 0xf00000 || dx < -0xf00000) { rockc_patrol(O, o); break; }
        o->v2a = 0xd; obj_set_anim(O, o, 4);
        break;
    }
    case 0xd: {                                                     /* lifting the rock */
        if (!obj_run_script(O, o)) { run = 0; break; }
        int sx = 160 + I(o->x) - O->camx;
        if (sx < -0x20 || sx > 0x160) { o->v2a = 0x19; obj_set_anim(O, o, 5); break; }
        o->v2a = 0xf; obj_set_anim(O, o, 7);
        break;
    }
    case 0xf:                                                       /* throw */
        if (!obj_run_script(O, o)) { run = 0; break; }
        o->timer = -1;
        {
            Object *r = obj_spawn(O, 0x4a, o->x, o->y - 0x820000, o->z);
            if (r) {
                r->scale = 0x200; r->facing = o->facing; mirror(r);
                r->v2a = o->y > FX(W) ? 0x3e : 2;
                r->v[4] = -0xc0000; r->v[8] = 0x80000;
                int32_t dx;
                if (!r->facing) { r->x += 0x140000; dx = P_->o.x - r->x; }
                else { r->x -= 0x140000; dx = r->x - P_->o.x; }
                r->v[0] = dx / 30;
                if (P_->o.y < FX(W) && o->x - P_->o.x > 0x780000) r->v[0] /= 2;
            }
        }
        o->v2a = 0x19; obj_set_anim(O, o, 5);
        break;
    case 0x19:
        if (!obj_run_script(O, o)) { run = 0; break; }
        rockc_patrol(O, o);
        break;
    case 0x23:                                                      /* hit by its own rock */
        if (obj_run_script(O, o)) {
            o->v2a = 0x25; obj_set_anim(O, o, 0x14); obj_run_script(O, o);
            o->scale = 0x200; o->flags54 |= 0x80;
        }
        run = 0;
        break;
    case 0x25:                                                      /* sinks */
        if (o->y > FX(W)) {
            if (obj_run_script(O, o)) { obj_kill(O, o, 1); O->kills++; return; }
            run = 0;
        }
        break;
    default: run = 0; break;
    }
    if (run) obj_run_script(O, o);
    if (!o->used) return;
    if (o->v2a != 0x25) obj_ground_follow(O, o);
    if (en_offscreen(O, o, 0x26e)) obj_kill(O, o, o->v2a == 0x23 || o->v2a == 0x25);
}

static void rcrock_init(Objects *O, Object *o)                      /* FUN_100099e0 */
{
    o->ai = AI2_RCROCK;
    o->flags50 = 0x202; o->v2a = 0x3d;
    o->pairs = 0; o->idx = 0; o->timer = 0;
    obj_run_words(O, o);
    o->v[8] = 0x60000;
}

/* FUN_10009a20 */
static void rcrock_tick(Objects *O, Object *o)
{
    o->z -= o->v[8];
    int dz = I(P_->o.z - o->z);
    if (dz > 0x100 || dz < -0x200) { obj_kill(O, o, 0); return; }
    o->y += o->v[4];
    step_x(O, o, o->v[0]);
    o->v[4] += 0x10000;
    if (I(o->y) > water_y(O)) { splash(O, o->x, o->y, o->z); obj_sound(O, 0xcb, o->x); obj_kill(O, o, 0); return; }
    {   /* bounce on the ground (FUN_10029e20) */
        int up, down;
        level_probe(O->L, I(o->x), I(o->y), I(o->z), &up, &down, NULL);
        if (o->v[4] > 0 && (down == 0 || (up != 0x7fff && up <= I(o->v[4]) + 8))) {
            if (up != 0x7fff && down != 0) o->y -= FX(up);
            o->v[4] = -(o->v[4] / 2); obj_sound(O, 0xd0d0, o->x);
        }
    }
    uint16_t hf = P_->hitflags;
    if ((hf & 0x1b) && blow_on(O, o)) {
        en_hit_sound(O, 0x10);
        if (!(hf & 0x13)) {                                         /* a sword blow breaks it */
            obj_sound(O, 0x8cf, o->x); rock_bits(O, o); obj_kill(O, o, 1); return;
        }
        obj_sound(O, 0xce, o->x);                                   /* punched / charged: knocked back */
        o->v[8] = -0x58000; o->v[0] = -o->v[0]; o->v[4] = -o->v[4];
        P_->consumed |= hf;
    } else if (touch_player(O, o)) {
        obj_sound(O, 0xcf, o->x);
        player_damage(P_, 3, o->x, o->y, o->z, 0x66);
        P_->shake = 1;
        rock_bits(O, o); obj_kill(O, o, 1); return;
    }
    if (o->v[4] > 0) {                                              /* FUN_100083e0: falling onto a rock centaur */
        for (int i = 0; i < OBJ_MAX; i++) {
            Object *c = &O->obj[i];
            if (!c->used || c->ai != AI2_ROCKC || c->v2a >= 0x23) continue;
            int32_t ddz = o->z - c->z, ddx = c->x - o->x;
            if (ddz > 0x200000 || ddz < -0x200000 || ddx < -0x500000 || ddx > 0x500000) continue;
            obj_sound(O, 0xd0cd, o->x); obj_sound(O, 0xe0e0, o->x);
            rock_bits(O, o);
            c->v2a = 0x23; obj_set_anim(O, c, 0xc);                 /* FUN_10008630 */
            obj_kill(O, o, 1);
            return;
        }
    }
    obj_run_words(O, o);
}


/* ---------------------------------------------------------------- Forest wrestling centaur (0x29) */

static void wrestle_init(Objects *O, Object *o)                    /* FUN_10009190 */
{
    o->ai = AI2_WRESTLE;
    o->flags50 = 0x100202; o->v2a = 1; o->hp = 7;
    o->b21 = (uint8_t)((I(o->z) + 0x40) >> 7);
    o->facing = 0xff; o->flags54 |= 0x10;
    o->anim = 0; o->script = pe_u32(O->pe, o->anims); o->idx = -1; o->timer = 0;
    if (!o->script && o->anims) { o->anim = (int16_t)pe_u16(O->pe, o->anims + 4); o->script = pe_u32(O->pe, o->anims + 4u * (uint32_t)o->anim); }
    obj_drop(O, o);
}

static void wrestle_charge(Objects *O, Object *o) { o->v2a = 10; o->v[0] = 0x30000; obj_set_anim(O, o, 2); }

static void wrestle_attack(Objects *O, Object *o)
{
    if (rnd(O, 0x8000) & 8) { o->v2a = 0x17; obj_set_anim(O, o, 0x10); }
    else { o->v2a = 0x16; obj_set_anim(O, o, 6); }
}

/* FUN_100092c0 */
static void wrestle_tick(Objects *O, Object *o)
{
    int prow = (I(P_->o.z) + 0x40) >> 7;
    switch (o->v2a) {
    case 1: {                                                       /* decide */
        int32_t home = o->spawn >= 0 ? o->x - O->sp[o->spawn].x : 0;
        if (P_->health == 0 || (home < -0x1400000 && o->facing == 0xff) || (home > 0x1400000 && o->facing == 0)) {
            o->v2a = 0x47; obj_set_anim(O, o, 1); break;
        }
        int32_t d = o->x - P_->o.x, ad = d < 0 ? -d : d;
        obj_drop(O, o);
        if (o->b21 == prow && ad < 0xf00000) {
            int wrong = d < 0 ? o->facing == 0xff : o->facing == 0;
            if (wrong) { obj_set_anim(O, o, 1); o->v2a = 9; break; }
            if (ad < 0x690000) { o->v2a = 0x15; obj_set_anim(O, o, 4); }
            else wrestle_charge(O, o);
        } else { o->v2a = 0xb; obj_set_anim(O, o, 3); }
        break;
    }
    case 9:                                                         /* turning */
        if (obj_run_script(O, o)) { o->v2a = 1; obj_set_anim(O, o, 0); mirror(o); }
        break;
    case 10: {                                                      /* charging */
        step_x(O, o, o->v[0]);
        obj_drop(O, o);
        int32_t d = o->x - P_->o.x;
        if (d > -0x690000 && d < 0x690000) wrestle_attack(O, o);
        if (obj_run_script(O, o)) o->v2a = 1;
        else if (o->idx > 0x13) o->v[0] /= 2;
        break;
    }
    case 0xb: {
        if (!obj_run_script(O, o)) break;
        int32_t d = o->x - P_->o.x; if (d < 0) d = -d;
        if (o->b21 == prow && d < 0xf00000) { o->v2a = 1; obj_set_anim(O, o, 0); }
        else { o->v2a = 9; obj_set_anim(O, o, 1); }
        break;
    }
    case 0x15: obj_run_script(O, o); wrestle_attack(O, o); break;
    case 0x16: obj_run_script(O, o); if (o->anim != 6) { o->v2a = 1; obj_set_anim(O, o, 0); } break;
    case 0x17: obj_run_script(O, o); if (o->anim != 0x10) { o->v2a = 1; obj_set_anim(O, o, 0); } break;
    case 0x23: obj_run_script(O, o); if (o->timer > 0x62) o->timer = 199; break;
    case 0x24: if (obj_run_script(O, o)) { o->v2a = 1; obj_set_anim(O, o, 0); } break;
    case 0x47: if (obj_run_script(O, o)) wrestle_charge(O, o); break;
    }
    if (!o->used) return;
    if (o->v2a != 0x24 && o->v2a != 0x23) {
        if (en_hits_player(O, o, 2)) player_damage(P_, 4, o->x, o->y, o->z, o->v2a == 0x16 ? 0x67 : 0x66);
        else if (touch_player(O, o)) { o->x = o->px; player_damage(P_, 0x22, o->x, o->y, o->z, 0); }
        else {
            int h = en_took_hit(O, o), d = 0;                       /* FUN_10026e90(o, 2, 0) */
            if (h == 2) d = 2;
            else if (h == 1) { d = en_blow_damage(O); if (d) en_hit_sound(O, 0x10); }
            if (d) {
                o->hp -= (int16_t)d;
                if (o->hp > 0) { obj_sound(O, 0x60df, o->x); o->v2a = 0x24; obj_set_anim(O, o, 9); obj_run_script(O, o); }
                else {                                              /* FUN_100091f0: beaten, drops three pickups */
                    obj_sound(O, 0x60e0, o->x); o->v2a = 0x23;
                    en_drop_pickup(O, o, 1); en_drop_pickup(O, o, 2); en_drop_pickup(O, o, 1);
                    obj_set_anim(O, o, 0xb); o->flags50 &= ~0x200u; obj_run_script(O, o);
                    O->kills++;
                }
            }
        }
    }
    obj_ground_follow(O, o);
    mirror(o);
    if (en_offscreen(O, o, 0x200)) obj_kill(O, o, o->v2a == 0x23);
}


/* ---------------------------------------------------------------- Forest griffons (0x46 big, 0x47 small) */

/* FUN_10029e20 for an object falling with vy = v[4]: landed on the floor? (snaps onto it) */
static int landed(Objects *O, Object *o)
{
    int up, down;
    level_probe(O->L, I(o->x), I(o->y), I(o->z), &up, &down, NULL);
    if (down == 0) return 1;
    if (up != 0x7fff && up <= I(o->v[4]) + 8) { o->y -= FX(up); return 1; }
    return 0;
}

static void fall_step(Objects *O, Object *o)                        /* vy += g unless rising-or-landed */
{
    if (o->v[4] < 0 || !landed(O, o)) { o->v[4] += o->v[6]; o->y += o->v[4]; }
    else o->v[4] = 0;
}

static void griffon_decide(Objects *O, Object *o)                   /* FUN_1000d560 (0x46) / FUN_1000dd10 (0x47) */
{
    int prow = (I(P_->o.z) + 0x40) >> 7;
    int32_t d = o->x - P_->o.x, ad = d < 0 ? -d : d;
    if (o->variant) {
        if (ad <= 0xb40000 && o->b21 == prow) {
            obj_set_anim(O, o, 3); o->v2a = 0x3b; o->v[0] = 0; o->v[4] = -0x58000; o->v[6] = 0x8800;
            obj_sound(O, rnd(O, 0x8000) < 0x4000 ? 0x68e2 : 0x68e3, o->x);
        } else { o->v2a = 0xb; obj_set_anim(O, o, 5); }
        return;
    }
    obj_drop(O, o);
    int32_t home = o->spawn >= 0 ? o->x - O->sp[o->spawn].x : 0;
    if (P_->health == 0 || (home < -0x1400000 && o->facing == 0xff) || (home > 0x1400000 && o->facing == 0)) {
        o->v2a = 0x47; obj_set_anim(O, o, 1); return;
    }
    if (ad <= 0xb40000 && o->b21 == prow) {
        if ((d < 0 && o->facing == 0xff) || (d > 0 && o->facing == 0)) { obj_set_anim(O, o, 1); o->v2a = 9; return; }
        if (ad > 0x960000) { o->v2a = 10; o->v[0] = 0x50000; obj_set_anim(O, o, 2); return; }
        if (ad <= 0x3c0000) { rnd(O, 0x8000); obj_set_anim(O, o, 5); o->v2a = 0x3a; return; }
        obj_set_anim(O, o, 3); o->v2a = 0x3f; obj_sound(O, 0x68e1, o->x); return;
    }
    if (rnd(O, 0x8000) < 0x4001) { o->v2a = 0x40; obj_set_anim(O, o, 9); }
    else { o->v2a = 0xb; obj_set_anim(O, o, 6); }
}

static void griffon_init(Objects *O, Object *o, int small)         /* FUN_1000d760 / FUN_1000ddd0 */
{
    o->ai = AI2_GRIFFON; o->variant = small;
    o->v2a = 1; o->hp = small ? 1 : 6;
    o->flags50 = 0x100202;
    o->b21 = (uint8_t)((I(o->z) + 0x40) >> 7);
    o->facing = 0xff; o->flags54 |= 0x10;
    griffon_decide(O, o);
    mirror(o);
    obj_drop(O, o);
    if (small) o->y -= 0x10000;
}

/* FUN_1000d7d0 (0x46) / FUN_1000de50 (0x47) */
static void griffon_tick(Objects *O, Object *o)
{
    switch (o->v2a) {
    case 10:                                                        /* run (0x46) */
        if (o->variant) goto plain;
        step_x(O, o, o->v[0]); obj_drop(O, o);
        if (obj_run_script(O, o)) griffon_decide(O, o);
        break;
    case 9: case 0xb:
        if (!o->variant) goto plain;
        if (obj_run_script(O, o)) griffon_decide(O, o);
        break;
    case 0x23:                                                      /* beaten: falls, lies there */
        fall_step(O, o);
        obj_run_script(O, o);
        if (o->timer > 0x62) o->timer = o->variant ? 0x6f : 199;
        break;
    case 0x24:                                                      /* hurt (0x46) */
        if (o->variant) goto plain;
        fall_step(O, o);
        if (obj_run_script(O, o)) { griffon_decide(O, o); obj_drop(O, o); }
        break;
    case 0x3b: {                                                    /* leap */
        step_x(O, o, o->v[0]);
        if (o->v[4] < 1 || !landed(O, o)) { if (o->v[0] != 0 || o->variant) { o->v[4] += o->v[6]; o->y += o->v[4]; } }
        else o->v[4] = 0;
        if (obj_run_script(O, o)) {
            if (o->variant) { obj_set_anim(O, o, 1); o->v2a = 9; }
            else griffon_decide(O, o);
        }
        break;
    }
    case 0x3f:                                                      /* crouch, then leap at Hercules (0x46) */
        if (o->variant) goto plain;
        if (obj_run_script(O, o)) {
            obj_sound(O, rnd(O, 0x8000) < 0x4001 ? 0x68e3 : 0x68e2, o->x);
            obj_set_anim(O, o, 4); o->v2a = 0x3b; o->v[4] = -0x58000; o->v[6] = 0x8800;
        }
        break;
    case 0x47:                                                      /* turn back home (0x46) */
        if (o->variant) goto plain;
        if (obj_run_script(O, o)) { o->v2a = 10; o->v[0] = 0x50000; obj_set_anim(O, o, 2); }
        break;
    default:
    plain:
        if (obj_run_script(O, o)) griffon_decide(O, o);
        break;
    }
    if (!o->used) return;
    if (o->v2a != 0x24 && o->v2a != 0x23) {
        if (en_hits_player(O, o, 2)) player_damage(P_, 4, o->x, o->y, o->z, 0);
        else {
            int h = en_took_hit(O, o), d = 0;                       /* FUN_10026e90(o, 2, 0) */
            if (h == 2) d = 2;
            else if (h == 1) { d = en_blow_damage(O); if (d) en_hit_sound(O, 0x10); }
            if (d) {
                o->hp -= (int16_t)d;
                if (o->variant) {                                   /* FUN_1000dcb0 */
                    o->hp -= 100; o->v2a = 0x23; obj_set_anim(O, o, 6); en_drop_pickup(O, o, 1);
                    obj_sound(O, 0xe0e7, o->x); o->flags50 &= ~0x200u; O->kills++;
                } else if (o->hp > 0) { obj_sound(O, 0xe0e6, o->x); o->v2a = 0x24; obj_set_anim(O, o, 8); }
                else {                                              /* FUN_1000d490 */
                    obj_sound(O, 0xe0e7, o->x); o->v2a = 0x23; obj_set_anim(O, o, 7);
                    en_drop_pickup(O, o, 1); en_drop_pickup(O, o, 2); en_drop_pickup(O, o, 1);
                    o->flags50 &= ~0x200u; O->kills++;
                }
            } else if (en_hits_player(O, o, 1)) player_damage(P_, 0x22, o->x, o->y, o->z, 0);
        }
    }
    mirror(o);
    if (en_offscreen(O, o, 0x200)) obj_kill(O, o, o->v2a == 0x23);
}


/* ---------------------------------------------------------------- Training: sharks (0x3c / 0x3d) and Pegasus (0x42) */

static void vel(Object *o)                                          /* FUN_1002b4c0: v[] integration, no floor */
{
    int32_t *v = o->v;
    v[0] += v[2] - v[3]; if (v[0] < 0) v[0] = 0; if (v[0] > v[1]) v[0] = v[1];
    v[4] += v[6] - v[7]; if (v[4] < 0) v[4] = 0; if (v[4] > v[5]) v[4] = v[5];
    v[8] += v[10] - v[11]; if (v[8] < 0) v[8] = 0; if (v[8] > v[9]) v[8] = v[9];
    o->x += o->facing ? -v[0] : v[0];
    o->y += o->b95 ? -v[4] : v[4];
    o->z += o->zface ? -v[8] : v[8];
}

static void shark_init(Objects *O, Object *o, int b)                /* FUN_100410b0 / FUN_10041460 */
{
    o->ai = AI2_SHARK; o->variant = b;
    o->flags50 |= 0x800;
    o->anim = 0; o->script = pe_u32(O->pe, o->anims); o->idx = -1; o->timer = 0;
    if (!o->script && o->anims) { o->anim = (int16_t)pe_u16(O->pe, o->anims + 4); o->script = pe_u32(O->pe, o->anims + 4u * (uint32_t)o->anim); }
    o->hp = b ? 5 : 0x1e;
    o->home[0] = o->x; o->home[1] = o->y; o->home[2] = o->z;        /* +0x9c.. (flags50 0x800: not refreshed) */
}

/* The leap's shared tail: splash going out of the water (0x202e) and back in (0x202d). */
static void shark_splash(Objects *O, Object *o)
{
    int W = water_y(O);
    if (o->v2a == 1 && o->y < FX(W)) { obj_spawn(O, 0x202e, o->x, o->y, o->z); obj_sound(O, 0x405b, o->x); o->v2a = 2; }
    if (o->v2a < 9 && !o->b95 && o->y > 0x3600000) { obj_spawn(O, 0x202d, o->x, o->y, o->z); obj_sound(O, 0x405c, o->x); o->v2a = 9; o->hp = 0x3c; }
}

/* FUN_100410e0: leaps straight out of the water every 30 ticks */
static void shark_a_tick(Objects *O, Object *o)
{
    obj_run_script(O, o);
    if (o->hp-- == 0) {
        o->x = o->home[0]; o->z = o->home[2]; o->y = o->home[1];
        obj_drop(O, o);
        o->v[5] = o->v[4] = 0xf0000; o->b95 = 0xff; o->v[7] = 0x9000; o->v[6] = 0;
        o->v[9] = 0x30000; o->v[8] = 0; o->zface = 0xff; o->v[11] = 0; o->v[10] = 0x1000;
        obj_set_anim(O, o, 3);
        o->facing = P_->o.x < o->x ? 0xff : 0;
        mirror(o);
    }
    if (o->v2a != 0) {
        vel(o);
        if (o->idx == 2 && o->v[4] < 0x4000) o->timer = 0;
        if (o->v[4] == 0) { o->v[6] = o->v[7]; o->v[7] = 0; o->b95 = 0; o->v[11] = o->v[10]; o->v[10] = 0; }
        shark_splash(O, o);
    }
    if (touch_player(O, o)) { player_damage(P_, 0x13, o->x, o->y, o->z, 0); O->t_hits++; }
    if (en_offscreen(O, o, 0x200)) obj_kill(O, o, 0);
}

/* FUN_10041490: leaps across from the side Hercules is facing away from; can be knocked out of the air */
static void shark_b_tick(Objects *O, Object *o)
{
    obj_run_script(O, o);
    int fly = o->v2a != 0;
    if (!fly && o->hp-- < 0) {
        if (!P_->o.facing) { o->facing = 0xff; o->x = o->home[0] + 0xbc0000; }
        else { o->facing = 0; o->x = o->home[0] - 0xbc0000; }
        o->y = o->home[1];
        obj_drop(O, o);
        o->v[1] = 0xc0000; o->v[0] = 0; o->v[3] = 0; o->v[2] = 0x6800;
        o->v[5] = o->v[4] = 0xc0000; o->b95 = 0xff; o->v[7] = 0x6800; o->v[6] = 0;
        o->flags50 &= ~0x200u;
        obj_set_anim(O, o, 6);
        fly = o->v2a != 0;
    }
    if (fly) {
        vel(o);
        if (o->idx == 4 && o->v[0] > 0x90000) o->timer = 0;
        if (o->v[4] == 0) { o->v[6] = o->v[7]; o->v[7] = 0; o->b95 = 0; o->v[3] = o->v[2]; o->v[2] = 0; }
        shark_splash(O, o);
        if (o->y > 0x3d00000) { if (o->v2a == 0x15) { obj_kill(O, o, 0); return; } obj_set_anim(O, o, 0); }
    }
    if (o->v2a < 0x14 && o->inv == 0) {
        int h = en_took_hit(O, o), d = 0;
        if (h == 0) { if (touch_player(O, o)) { player_damage(P_, 0x13, o->x, o->y, o->z, 0); O->t_hits += 3; } }
        else {
            d = h == 2 ? 100 : en_blow_damage(O);
            if (d) {
                if (h == 1) en_hit_sound(O, 0x10);
                o->hp -= (int16_t)d; o->v2a = 0x14;
                if (o->b95 == 0xff) { o->v[6] = o->v[7]; o->v[7] = 0; o->b95 = 0; o->v[3] = o->v[2]; o->v[2] = 0; o->facing = (uint8_t)~o->facing; }
                o->flags50 &= ~0x200u;
                obj_set_anim(O, o, 9);
            }
            o->inv = 0x1e;
            if (o->hp < 1) o->v2a = 0x15;
        }
    }
    mirror(o);
    if (en_offscreen(O, o, 800)) obj_kill(O, o, 0);
}

static void peg_init(Objects *O, Object *o)                          /* FUN_10041f50: Pegasus watches */
{
    o->ai = AI2_PEG;
    obj_drop(O, o);
    o->anim = 0; o->script = pe_u32(O->pe, o->anims); o->idx = -1; o->timer = 0;
    if (!o->script && o->anims) { o->anim = (int16_t)pe_u16(O->pe, o->anims + 4); o->script = pe_u32(O->pe, o->anims + 4u * (uint32_t)o->anim); }
}

static void peg_tick(Objects *O, Object *o)                          /* FUN_10041f80 */
{
    if (o->airborne) { obj_drop(O, o); return; }
    obj_run_script(O, o);
}


/* ---------------------------------------------------------------- Forest water (0x31) and Phil's score machines (0x2029 / 0x202b) */

static void water_init(Objects *O, Object *o)                       /* FUN_1000d2c0 */
{
    for (uint32_t a = 0x100511b0, i = 0; i < 0xb; i++, a += 0x18) {    /* placement table keyed by the spawn x */
        if ((int32_t)pe_u32(O->pe, a) != o->x) continue;
        o->x += (int32_t)pe_u32(O->pe, a + 4); o->y += (int32_t)pe_u32(O->pe, a + 8); o->z += (int32_t)pe_u32(O->pe, a + 12);
        o->rot[1] = pe_s16(O->pe, a + 16); o->scale = pe_u16(O->pe, a + 20);
        break;
    }
    obj_sound_obj(O, 0x8ca, o);      /* LV_FOREST: FUN_10006da0(0x8ca, obj) - owned by the water so the loop stops when it is culled (was position-only, owner 1: looped forever, one more voice per respawn) */
    o->b23 = 5;                                                     /* vertex callback 5 (ripple) - drawn rigid */
    o->flags54 |= 5;
    o->ai = AI2_WATER; o->pairs = 0; o->idx = 0; o->timer = 0;
}

static void water_tick(Objects *O, Object *o)                       /* FUN_1000d350 */
{
    obj_run_words(O, o);
    if (en_offscreen(O, o, 0x22b)) obj_kill(O, o, 0);
}

/* FUN_1002d9b0 / FUN_1002da10: the lesson boards stand at y 0x320, z 0x100; frame = Phil's current mark
 * (DAT_10070ef4 / DAT_10070f00, 0x66 / 0x83 until a lesson sets them). */
static void machine_init(Objects *O, Object *o, int b)
{
    o->y = 0x3200000; o->z = 0x1000000;
    o->ai = AI2_MACHINE; o->variant = b; o->script = 0;
    o->frame = b ? 0x83 : 0x66; o->s98 = 0x83;
    (void)O;
}

/* ---------------------------------------------------------------- dispatch */

int enemies2_init(Objects *O, Object *o, uint32_t init)
{
    switch (init) {
    case H_MINOTAUR: mino_init(O, o); return 1;
    case H_MROCK: mrock_init(O, o); return 1;
    case H_ROCKC: rockc_init(O, o); return 1;
    case H_RCROCK: rcrock_init(O, o); return 1;
    case H_WRESTLE: wrestle_init(O, o); return 1;
    case H_GRIFFON: griffon_init(O, o, 0); return 1;
    case H_GRIFFON2: griffon_init(O, o, 1); return 1;
    case H_SHARK_A: shark_init(O, o, 0); return 1;
    case H_SHARK_B: shark_init(O, o, 1); return 1;
    case H_PEG: peg_init(O, o); return 1;
    case H_WATER: water_init(O, o); return 1;
    case H_EQROCK: eqrock_init(O, o); return 1;
    case H_MACH_A: machine_init(O, o, 0); return 1;
    case H_MACH_B: machine_init(O, o, 1); return 1;
    }
    return 0;
}

int enemies2_tick(Objects *O, Object *o)
{
    switch (o->ai) {
    case AI2_MINO: mino_tick(O, o); return 1;
    case AI2_MROCK: mrock_tick(O, o); return 1;
    case AI2_BOULDER: boulder_tick(O, o); return 1;
    case AI2_EQROCK: eqrock_tick(O, o); return 1;
    case AI2_ROCKC: rockc_tick(O, o); return 1;
    case AI2_RCROCK: rcrock_tick(O, o); return 1;
    case AI2_ONESHOT: oneshot_tick(O, o); return 1;
    case AI2_WRESTLE: wrestle_tick(O, o); return 1;
    case AI2_GRIFFON: griffon_tick(O, o); return 1;
    case AI2_SHARK: if (o->variant) shark_b_tick(O, o); else shark_a_tick(O, o); return 1;
    case AI2_PEG: peg_tick(O, o); return 1;
    case AI2_WATER: water_tick(O, o); return 1;
    case AI2_MACHINE: if (en_offscreen(O, o, 0x300)) obj_kill(O, o, 0); return 1;
    }
    return 0;
}

int enemies2_callback(Objects *O, Object *o, int id)
{
    (void)id;
    switch (o->ai) {
    case AI2_MINO: mino_ai(O, o); return 1;
    }
    return o->ai > AI2_BASE;
}

void enemies2_frame(Objects *O)
{
    if (!O->P) return;
    if (getenv("ENEMIES2_GOD")) O->P->cheats |= 4;            /* test hook: Hercules takes no damage */
    if (O->L->index == 5) thebes_frame(O);
    if (getenv("ENEMIES2_BOX"))                                         /* debug: body boxes of the enemies */
        for (int i = 0; i < OBJ_MAX; i++) {
            Object *o = &O->obj[i]; int b[6];
            if (o->used && o->kind == K_ENEMY && o->ai != 6 && obj_box(O, o, 1, b))
                printf("  box %04x frame %d: x %d..%d y %d..%d z %d..%d\n", o->type, o->frame, b[0], b[3], b[1], b[4], b[2], b[5]);
        }
}
