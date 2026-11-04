/* FX: Stymphalian birds (STYBIRD bank 46), the swooping bird flocks of Training (type 0x38) and Forest (0x2e).
 *
 *   Training 0x38   init FUN_100433d0, update FUN_10043440, move FUN_10043d80, choose FUN_10043b50,
 *                   killed FUN_10043e20 (+ feathers FUN_10043ff0, table 0x10068970), floor keep FUN_100440d0
 *   Forest   0x2e   init FUN_1000ae00, update FUN_1000b3f0, move FUN_1000b250, choose FUN_1000af20,
 *                   killed FUN_1000b340 (+ FUN_1000cdd0), perch take-off leaves FUN_1000d0c0 (table 0x10051140),
 *                   floor keep FUN_10008480 (0x28 above the floor, objects included)
 *
 * Both run the same state machine (+0x2a): 0x26 / 0x1b perched, 0x21 take off (velocity table by script
 * step +0x32), 2 / 0x27 fly, 0x22 swoop at Hercules, 8 / 9 turn round, 0x2b / 0x2c change depth plane,
 * 0x2d fly away upwards, 0x23 shot down (falls, feathers, then removed).
 * Field map: v[0] = +0xa8 x speed, v[4] = +0xb8 y speed, v[6] = +0xc0 y accel, b21 = +0x21 plane row,
 * home[0] = +0x9c previous x, d[5] = spawn-record x (DAT_1006fee8 lookup), w[0] = +0x108 (Forest turn flag).
 */
#include "objects_int.h"
#include <stdlib.h>
#include <stdio.h>

enum { AI_BIRD_T = 610, AI_BIRD_F };
enum { H_BIRD_T = 0x100433d0, H_BIRD_F = 0x1000ae00 };
#define P_ (O->P)

static int iabs(int v) { return v < 0 ? -v : v; }
static void mirror(Object *o) { o->flags54 = (o->flags54 & ~0x10u) | (o->facing & 0x10); }
static int plane(int32_t z) { return (uint8_t)((I(z) + 0x40) >> 7); }

static int map_down(Objects *O, const Object *o)                    /* FUN_10020b50 */
{
    int up, down;
    level_probe(O->L, I(o->x), I(o->y), I(o->z), &up, &down, NULL);
    return down;
}

static int all_down(Objects *O, const Object *o)                    /* FUN_1002a010: map and platform objects */
{
    int d = map_down(O, o), u2, d2;
    if (objects_floor(O, I(o->x), I(o->y), I(o->z), &u2, &d2) && d2 < d) d = d2;
    return d;
}

static void keep_above(Objects *O, Object *o, int h, int objs)     /* FUN_100440d0 / FUN_10008480 */
{
    int d = objs ? all_down(O, o) : map_down(O, o);
    if (d < h) o->y += FX(d - h);
}

static void shadow_flag(Objects *O, Object *o)                      /* FUN_1000ef10 (flags54 & 8 = floor shadow) */
{
    if (o->z < -0x100000) { o->flags54 &= ~8u; return; }
    int d = map_down(O, o);
    if (d == 0x7fff || d > 0x100) o->flags54 &= ~8u; else o->flags54 |= 8;
}

static int blocked(Objects *O, Object *o, int forest)
{
    if (forest) {                                                  /* FUN_1000b250: box edge in a wall */
        int b[6];
        if (!obj_box(O, o, 1, b)) return 0;
        return (obj_attr_flags(O, o->facing ? b[0] : b[3], I(o->y), I(o->z)) & 2) != 0;
    }
    int ax = I(o->x) + (o->facing ? -0x10 : 0x10);                  /* FUN_10043d80: 16 ahead */
    return (obj_attr_flags(O, ax, I(o->y), I(o->z)) & 7) != 0;
}

static void move(Objects *O, Object *o, int forest)                 /* FUN_10043d80 / FUN_1000b250 */
{
    o->y += o->v[4];
    o->home[0] = o->x;
    int32_t nx = o->facing ? o->x - o->v[0] : o->x + o->v[0];
    if (forest) {
        o->x = nx;
        if (blocked(O, o, 1)) o->x = o->home[0];
        return;
    }
    int32_t sx = o->x; o->x = nx;
    if (!blocked(O, o, 0)) return;
    o->x = sx;
    if (o->v2a != 9 && o->v2a != 0x23) { o->v2a = 9; obj_set_anim(O, o, 3); }
}

static void set_state(Objects *O, Object *o, int st, int anim)
{
    o->v2a = (uint16_t)st; obj_set_anim(O, o, anim);
    if (getenv("FX_BIRD")) {
        printf("bird %04x st %x anim %d script %08x:", o->type, st, anim, o->script);
        for (int i = 0; i < 24; i++) printf(" %d", pe_s16(O->pe, o->script + 2u * (uint32_t)i));
        printf("\n");
    }
}

static void choose(Objects *O, Object *o, int32_t dx, int forest)  /* FUN_10043b50 / FUN_1000af20 */
{
    unsigned r = (unsigned)obj_rnd(O, 0x8000);
    int my = o->b21, his = plane(P_->o.z);
    int32_t from = o->x - o->d[5];
    int near = forest ? !((from > 0x2800000 && o->x > P_->o.x) || (from < -0x2800000 && o->x < P_->o.x)) : iabs(from) <= 0x2800000;
    if (!near) { o->v2a = 0x2d; o->v[4] = 0; o->v[6] = -0x8000; obj_set_anim(O, o, 2); obj_run_script(O, o); return; }
    if (forest) {
        int k = obj_rnd(O, 0x400);
        int32_t dy = o->y - P_->o.y;
        o->v[4] = -k * 0x100 + 0x20000;
        if (dy > -0x100001) o->v[4] = -k * 0x100 - 0x20000;
        if (dy < -0x7fffff) o->v[4] += 0x40000;
        if (o->w[0]) {
            if (o->w[0] == 2) { o->w[0] = 0; set_state(O, o, 2, 2); return; }
            o->w[0] = 0;
        }
        int ax = I(o->x) + (o->facing ? -0x40 : 0x40);
        if (obj_attr_flags(O, ax, I(o->y), I(o->z)) & 2) { o->w[0] = 2; set_state(O, o, 9, 3); return; }
    }
    if (my != his && r > 0x4000) {
        int x = I(o->x), step = forest ? 0x40 : 1;
        int zz = my - his < 1 ? I(o->z) + 1 : I(o->z) - 1;
        int yy = forest ? I(o->y) : 0;
        if (!(obj_attr_flags(O, x, yy, zz) & 2) && !(obj_attr_flags(O, x + (o->facing ? -step : step), yy, zz) & 2)) {
            if (my - his < 1) set_state(O, o, 0x2c, 10); else set_state(O, o, 0x2b, 9);
            if (!forest) obj_run_script(O, o);
            return;
        }
    }
    int fwd = !o->facing;                                          /* facing right */
    int32_t ahead = fwd ? dx : -dx;                                /* > 0: Hercules is behind the bird */
    if (ahead > 0x3aaaaa) { set_state(O, o, 9, 3); if (!forest) obj_run_script(O, o); return; }
    if (!(r & 6)) { set_state(O, o, 0x27, 7); if (!forest) obj_run_script(O, o); return; }
    int attack = ahead < -0x3aaaaa && my == his && ahead > -0xb00000;
    if (attack && (forest || !P_->dying)) { set_state(O, o, 0x22, 5); if (!forest) obj_run_script(O, o); return; }
    if (attack) { set_state(O, o, 0x27, 7); obj_run_script(O, o); return; }
    if (!(r & 8)) set_state(O, o, 0x27, 7); else set_state(O, o, 2, 2);
    if (!forest) obj_run_script(O, o);
}

static void scatter(Objects *O, Object *o, uint32_t tbl, uint32_t end, int vx, int vy0, int vyr, int vz)
{
    for (uint32_t a = tbl; a + 8 <= end; a += 8) {
        uint16_t type = pe_u16(O->pe, a + 6);
        Object *p = obj_spawn(O, type, o->x + FX(pe_s16(O->pe, a)), o->y + FX(pe_s16(O->pe, a + 2)), o->z + FX(pe_s16(O->pe, a + 4)));
        if (!p) continue;
        p->spawn = -1;
        obj_run_script(O, p);
        p->kind = K_DEBRIS; p->facing = 0; p->script = 0;
        p->vx = (obj_rnd(O, vx) - vx / 2) * 0x100;
        p->vy = (vy0 - obj_rnd(O, vyr)) * 0x100;
        p->life = 40;
        (void)vz;
    }
}

static void shot_down(Objects *O, Object *o, int forest)            /* FUN_10043e20 / FUN_1000b340 */
{
    obj_spawn(O, 0x2027, o->x, o->y, forest ? o->z - 0x80000 : o->z);
    set_state(O, o, 0x23, 6);
    if (!forest) obj_run_script(O, o);
    if (o->v[0] > 0x30000) o->v[0] = 0x30000;
    if (o->v[0] < -0x30000) o->v[0] = -0x30000;
    if (forest) {
        scatter(O, o, 0x10051140, 0x10051190, 0x200, -0x100, 0x200, 0x1000);   /* FUN_1000cdd0 */
        if (obj_rnd(O, 0x8000) > 0x4000) en_drop_pickup(O, o, 1);
    } else {
        scatter(O, o, 0x10068970, 0x10068a30, 0xa00, -0x200, 0x600, 0x1800);  /* FUN_10043ff0 */
    }
    o->flags50 &= ~0x200u;
}

static int bird_init(Objects *O, Object *o, uint32_t init)
{
    if (init != H_BIRD_T && init != H_BIRD_F) return 0;
    o->d[5] = o->x;
    o->b21 = (uint8_t)plane(o->z);
    o->flags50 = 0x202;
    o->movei = 0; o->moving = -1;
    if (init == H_BIRD_T) {                                        /* FUN_100433d0 */
        o->ai = AI_BIRD_T;
        set_state(O, o, 0x21, 1);
    } else {                                                       /* FUN_1000ae00 */
        o->ai = AI_BIRD_F; o->w[0] = 0;
        int d = all_down(O, o);
        if (d == 0x7fff || d < 0x81) {
            o->facing = 0; o->y -= 0x80000;
            set_state(O, o, 0x26, 4);
        } else {
            o->facing = (obj_attr_flags(O, I(o->x) - 0x20, I(o->y) + 0x80, I(o->z)) & 2) ? 0 : 0xff;
            set_state(O, o, 0x1b, 0);
        }
    }
    mirror(o);
    obj_run_script(O, o);
    return 1;
}

static void bird_tick(Objects *O, Object *o, int forest)
{
    int st = o->v2a;
    if (forest && st != 0x26 && st != 0x1b && st != 0x21) keep_above(O, o, 0x28, 1);
    int32_t dx = o->x - P_->o.x, adx = iabs(dx);
    int32_t dy = o->y - P_->o.y;
    if (!forest && level_attr(O->L, I(o->x), I(o->y), I(o->z)) == 0x1a) dy = 0xff0000;   /* attr 0x1a: never sink */
    if (!forest) keep_above(O, o, 0x26, 0);
    switch (st) {
    case 2: case 0x27:
        if (!obj_run_script(O, o)) {
            if (!forest) { if (dy > -0x400000) o->y -= 0x60000; if (dy < -0x800000) o->y += 0x60000; }
            move(O, o, forest);
        } else choose(O, o, dx, forest);
        break;
    case 8:
        if (o->idx > 0xd && (o->v[0] += 0x8000) > 0x80000) o->v[0] = 0x80000;
        move(O, o, forest);
        keep_above(O, o, forest ? 0x28 : 0x26, forest);
        obj_run_script(O, o);
        if (o->timer > 99) o->timer = 199;
        break;
    case 9:
        if (o->idx < 0xe) { if ((o->v[0] -= 0x8000) < 0) o->v[0] = 0; }
        else if ((o->v[0] += 0x8000) > 0x80000) o->v[0] = 0x80000;
        move(O, o, forest);
        if (obj_run_script(O, o)) {
            if (forest) choose(O, o, dx, 1);
            else choose(O, o, dx, 0);
        } else if (!forest) {
            if (dy > -0x400000) o->y -= 0x60000;
            if (dy < -0x800000) o->y += 0x60000;
        }
        break;
    case 0x1b:                                                      /* Forest: perched until Hercules is near */
        if (o->b21 == plane(P_->o.z) && adx < obj_rnd(O, 0x600000) + 0xf00000) {
            scatter(O, o, 0x10051140, 0x10051190, 0x200, 0, 0x200, 0x1000);   /* FUN_1000d0c0 leaves */
            set_state(O, o, 0x22, 5); o->idx = 10;
            obj_run_script(O, o);
            break;
        }
        o->timer = 99; obj_run_script(O, o);
        break;
    case 0x21: {                                                    /* take off */
        int s = o->idx;
        if (s > 5) {
            if (s < 10) {
                if (s == 6) { o->v[4] = -0x18000; o->v[0] = 0x20000; }
                else if (s == 7) { o->v[4] = -0x30000; o->v[0] = 0x2aaaa; }
                else if (s < 8) o->v[0] = 0x60000;
                else { o->v[4] = -0x60000; o->v[0] = 0x40000; }
            } else o->v[0] = 0x80000;
            move(O, o, forest);
        }
        if (obj_run_script(O, o)) { o->v[4] = 0; o->v[0] = 0x80000; set_state(O, o, 2, 2); o->idx = 2; }
        break;
    }
    case 0x22: {                                                    /* swoop */
        int s = o->idx;
        if (s < 10) { if ((o->v[0] -= 0x4000) < 0x40000) o->v[0] = 0x40000; }
        else if (s < 0x10) {
            if ((o->v[0] += 0x10000) > 0x100000) o->v[0] = 0x100000;
            if ((o->v[4] += 0x10000) > 0xc0000) o->v[4] = 0x60000;
        } else if (s < 0x15) {
            if ((o->v[0] -= 0x8000) < 0x40000) o->v[0] = 0x40000;
            if ((o->v[4] -= 0x10000) < -0x60000) o->v[4] = -0x60000;
        } else if ((o->v[0] += 0x8000) > 0x80000) o->v[0] = 0x80000;
        if (o->anim != 5) { set_state(O, o, 2, 2); o->v[0] = 0x80000; o->v[4] = 0; }
        move(O, o, forest);
        obj_run_script(O, o);
        break;
    }
    case 0x23:                                                      /* shot down */
        move(O, o, forest);
        keep_above(O, o, forest ? 0x28 : 0x26, forest);
        if (obj_run_script(O, o)) { obj_kill(O, o, 1); return; }
        break;
    case 0x26:
        if (forest) { set_state(O, o, 0x21, 1); obj_run_script(O, o); break; }
        if ((uint32_t)adx < (uint32_t)obj_rnd(O, 0x300000) + 0x480000u) { set_state(O, o, 0x21, 1); obj_run_script(O, o); }
        else { o->timer = 99; obj_run_script(O, o); }
        break;
    case 0x2b: case 0x2c: {                                         /* change depth plane */
        int s = o->idx;
        if (s < 8) { if ((o->v[0] -= 0x8000) < 0) o->v[0] = 0; }
        else if (s < 0x10) { o->z += st == 0x2b ? -0x80000 : 0x80000; if (s == 8) o->v[0] = 0; }
        else if ((o->v[0] += 0x8000) > 0x80000) o->v[0] = 0x80000;
        move(O, o, forest);
        keep_above(O, o, forest ? 0x28 : 0x26, forest);
        if (obj_run_script(O, o)) {
            o->z = (int32_t)(((uint32_t)o->z + 0x400000u) & 0xff80ffffu);
            o->b21 = (uint8_t)plane(o->z);
            choose(O, o, dx, forest);
        }
        break;
    }
    case 0x2d:                                                      /* leave upwards */
        o->v[4] += o->v[6];
        move(O, o, forest);
        if (en_offscreen(O, o, 100)) { obj_kill(O, o, 0); return; }
        obj_run_script(O, o);
        break;
    default:
        set_state(O, o, 1, 0);
        break;
    }
    if (o->v2a != 0x23) {
        int h = en_took_hit(O, o);
        int dead = 0;
        if (h == 2) dead = 1;
        else if (h == 1) { int dmg = en_blow_damage(O); if (dmg) { en_hit_sound(O, 0x10); dead = 1; } }
        if (dead) shot_down(O, o, forest);
        else if (en_hits_player(O, o, 1)) {
            en_hurt_player(O, o, forest ? (o->v2a == 5 ? 0x14 : 0x12) : 0x11);   /* FUN_100266a0 */
            if (!forest) O->t_hits += 2;                                       /* DAT_10070ef0 */
        } else if (!forest && o->v2a != 0x26 && map_down(O, o) == 0) shot_down(O, o, 0);   /* FUN_10029e20 */
    }
    if (!forest) shadow_flag(O, o);
    if (en_offscreen(O, o, 500)) obj_kill(O, o, 0);                 /* FUN_1002aa30(500, 0x140) -> FUN_10027260 */
}

int fx_birds_init(Objects *O, Object *o, uint32_t init) { return bird_init(O, o, init); }

int fx_birds_tick(Objects *O, Object *o)
{
    if (o->ai == AI_BIRD_T) { bird_tick(O, o, 0); return 1; }
    if (o->ai == AI_BIRD_F) { bird_tick(O, o, 1); return 1; }
    return 0;
}
