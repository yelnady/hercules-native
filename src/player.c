#include <stdio.h>
#include "player.h"
#include <stdlib.h>
#include <string.h>

/* Animation scripts (ENG1 virtual addresses). */
enum {
    SCR_IDLE = 0x10053820, SCR_IDLE_BREATHE = 0x10053830, SCR_IDLE_A = 0x100538d0, SCR_IDLE_B = 0x10053930,
    SCR_IDLE_C = 0x10053878, SCR_WALK = 0x10053a20, SCR_SKID = 0x10053b90, SCR_TURN = 0x10053c00,
    SCR_TURN_WALK = 0x10053c18, SCR_JUMP = 0x10053cc0, SCR_JUMP_RUN = 0x10053d10, SCR_LAND = 0x10053da0,
    SCR_FALL = 0x10053df8, SCR_HANG = 0x10054680, SCR_CLIMB = 0x100546b0, SCR_CROUCH = 0x100547e8,
    /* combat */
    SCR_SWORD = 0x10054120,         /* state 0xc: swing, repeats while the sword button is held */
    SCR_SWORD_END = 0x100541b0,     /* state 0x2e after releasing the sword (combo window: op 0x10) */
    SCR_SWORD_END2 = 0x100541d8, SCR_SWORD_END3 = 0x10054470,
    SCR_PUNCH = 0x10053ea0,         /* state 0x29: punch combo, loops while held */
    SCR_PUNCH_END = 0x10053f20,
    SCR_POWER = 0x10053f80,         /* state 0x2a: charge while held (op 0x16) */
    SCR_POWER_HIT = 0x10054018,     /* released: uppercut */
    SCR_CROUCH_HIT = 0x10054078,    /* state 0xe */
    SCR_AIR_SWORD = 0x100541f8, SCR_AIR_PUNCH = 0x100540c0,
    SCR_POUND = 0x10053e28,         /* state 0x18: down at the top of a standing jump */
    SCR_POUND_LAND = 0x10053e58, SCR_POUND_LAND_OBJ = 0x10053e80,
    SCR_HURT_FRONT = 0x100546d8, SCR_HURT_STRONG = 0x10054718, SCR_HURT_BACK = 0x10054750,
    SCR_HURT_AIR = 0x10054798,
    SCR_DIE_BOTH = 0x100549f0, SCR_DIE_A = 0x10054aa0, SCR_DIE_B = 0x10054930,
    SCR_SWING_A = 0x10054850, SCR_SWING_B = 0x10054890,
    SCR_SWING = 0x10054810,         /* state 9: hanging from a pole */
    SCR_SWING_JUMP = 0x100548e8,    /* let go: state 5 */
    SCR_FIRE = 0x10054260,          /* state 0x2b: fireball sword */
    SCR_LIGHTNING = 0x100542a0,     /* state 0x2c: lightning sword, aimed */
    SCR_LIGHT_A = 0x10054318, SCR_LIGHT_B = 0x10054380, SCR_LIGHT_C = 0x100542c8,
    SCR_SONIC = 0x100543d0,         /* state 0x2d: sonic sword */
    SCR_CARRY_IDLE = 0x10053a10, SCR_CARRY_WALK = 0x10053b00, SCR_CARRY_STOP = 0x10053bf0,
    SCR_LIFT = 0x100544c0,          /* state 0x14 */
    SCR_THROW = 0x10054558, SCR_THROW_UP = 0x100545e8,  /* state 0x2f */
    SCR_WIN = 0x10054b50,           /* state 0x19 (HC_DUNIT) */
    /* OBJECTS: depth-plane walk (FUN_10017d60) and the turn back to side-on (FUN_10016cf0) */
    SCR_PLANE_IN = 0x10053a90, SCR_PLANE_OUT = 0x10053ac8, SCR_PLANE_END_OUT = 0x10053bc8, SCR_PLANE_END_IN = 0x10053bb0,
};

/* Player states (index into ENG1's handler table). */
enum {
    ST_IDLE = 0, ST_WALK = 1, ST_JUMP_RUN = 2, ST_SKID = 3, ST_JUMP = 4, ST_TURN = 6, ST_FALL = 7,
    ST_LAND = 8, ST_CROUCH = 0xb, ST_SWORD = 0xc, ST_CROUCH_HIT = 0xe, ST_HANG = 0x10, ST_HURT = 0x11,
    ST_POUND_LAND = 0x12, ST_CLIMB = 0x13, ST_DYING = 0x16, ST_FALL_DEATH = 0x17, ST_POUND = 0x18,
    ST_PUNCH = 0x29, ST_POWER = 0x2a, ST_SWORD_END = 0x2e,
    ST_SWING = 9, ST_LIFT = 0x14, ST_WIN = 0x19, ST_FIRE = 0x2b, ST_LIGHTNING = 0x2c, ST_SONIC = 0x2d,
    ST_THROW = 0x2f,
    ST_PLANE = 0x1c,   /* OBJECTS */
    ST_START = 0xa,    /* FUN_10019440: level start hold */
};

/* ENG1 tables read at run time. */
#define TBL_HURT_DAMAGE 0x10055348u     /* s32[3][8]: damage taken per (difficulty, level-1) */
#define TBL_FLASH 0x100552d0u           /* s16: hurt flash levels, 0xfe = switch to mode 0, 0 = end */
#define TBL_SHAKE 0x100551e8u           /* s32: screen shake offsets, 0 = end */
#define TBL_DMG_CROUCH 0x100553e8u      /* s32[4] per difficulty: damage dealt by each attack kind */
#define TBL_DMG_PUNCH 0x100553f8u
#define TBL_DMG_SWORD 0x10055408u
#define TBL_DMG_AIR_SWORD 0x10055418u
#define TBL_DMG_AIR_PUNCH 0x10055428u
#define TBL_DMG_POWER_MAX 0x10055438u
#define TBL_AIM_DIR 0x10054c78u         /* s16[16] by pad direction bits: wanted fire angle (200 = ahead) */
#define TBL_FIRE_AIM 0x10054c98u        /* 20 x {angle, frame, dx, dy, shot angle} s16 */
#define TBL_LIGHT_AIM 0x10054d60u       /* 6 x the same */
#define TBL_RELOAD 0x10055320u          /* s16[5]: ticks per spent charge */
#define TBL_INV_FLASH 0x100552a8u       /* s16[16]: invulnerability flicker levels */

#define FX(v) ((int32_t)(v) * 65536)
#define I(v) ((v) >> 16)

static int iabs(int v) { return v < 0 ? -v : v; }

static int rnd(Player *P, int n)
{
    P->rng = P->rng * 214013u + 2531011u;          /* MSVC rand() */
    return n ? (int)((P->rng >> 16) & 0x7fff) % n : 0;
}

static void sound(Player *P, int id) { if (P->nsounds < 8) P->sounds[P->nsounds++] = id; }

/* FUN_10030010 against the static map attributes. */
static int solid(Player *P, int32_t x, int32_t y, int32_t z, int mask)
{
    if (P->obj_attr) {      /* OBJECTS: FUN_1002a170 checks the solid object list before the map */
        int a = P->obj_attr(P->obj_ctx, I(x), I(y), I(z));
        if (a >= 0) { uint16_t f = P->L->flags[a & 127]; return (f & 0x8000) ? 0 : (f & mask) != 0; }
    }
    return level_solid(P->L, I(x), I(y), I(z), mask);
}

static int probe(Player *P, int *up, int *down)
{
    Obj *o = &P->o;
    level_probe(P->L, I(o->x), I(o->y), I(o->z), up, down, NULL);
    if (P->obj_floor && P->obj_ctx) {       /* SOLIDS: platform objects are checked with the map floors */
        int u, d;
        if (P->obj_floor(P->obj_ctx, I(o->x), I(o->y), I(o->z), &u, &d)) {
            if (u < *up) *up = u;
            if (d < *down) *down = d;
        }
    }
    return *up != 0x7fff || *down != 0x7fff;
}

/* FUN_10029c20: follow the ground while walking. */
static int ground_follow(Player *P)
{
    Obj *o = &P->o;
    int dx = iabs(I(o->x - o->px));
    int tol = (dx < 4 ? dx + 4 : dx + 2) + iabs(I(o->z - o->pz));
    int up, down;
    if (!probe(P, &up, &down)) return 0;
    if (up <= tol) { o->y = (o->y & (int32_t)0xffff0000) - FX(up); return 1; }
    if (down <= tol) { o->y += FX(down); return 1; }
    return 0;
}

/* FUN_10029da0: while rising, step out of ground we moved into. */
static int rise_snap(Player *P)
{
    Obj *o = &P->o;
    int dx = iabs(I(o->x - o->px));
    if (dx < 4) dx = 4;
    int up, down;
    if (probe(P, &up, &down) && up <= dx + 8) { o->y = (o->y & (int32_t)0xffff0000) - FX(up); return 1; }
    return 0;
}

/* FUN_10029e20: landing test while falling. */
static int land_test(Player *P)
{
    Obj *o = &P->o;
    int dy = iabs(I(o->y - o->py)), dx = iabs(I(o->x - o->px)), dz = iabs(I(o->z - o->pz));
    int up, down;
    if (!probe(P, &up, &down)) return 0;
    if (down == 0) return 1;
    if (up <= (dy + dx + dz) * 2 + 16) { o->y -= FX(up); return 1; }
    return 0;
}

static void set_script(Player *P, uint32_t s)
{
    P->o.script = s;
    P->o.timer = -1;
    P->o.idx = -1;
}

static void mirror_sync(Obj *o) { o->flags54 = (o->flags54 & ~0x10u) | (o->facing & 0x10); }

#define OVERLAY 0x800000u       /* obj+0x54 bit 23: draw the sword-trail child object */

static int held(Player *P, uint16_t m) { return (P->pad & m) != 0; }

static void go_idle(Player *P);
static int run_script(Player *P);
static int frame_box(const Player *P, int k, Box *b);

static void spawn(Player *P, int id)
{
    if (P->nspawns >= 4) return;
    Spawn *s = &P->spawns[P->nspawns++];
    s->id = id; s->x = P->o.x; s->y = P->o.y; s->z = P->o.z; s->facing = P->o.facing;
}

static void flame_start(Player *P) { P->flame.active = 1; }            /* FUN_1001a3e0 */
static void flame_stop(Player *P)                                      /* FUN_1001a410 */
{
    P->flame.active = 0;                                               /* (also stops sound 0x26) */
}

static void clamp_x(Player *P)     /* FUN_10019c30 */
{
    Obj *o = &P->o;
    if (P->L->mode) {
        int32_t ring = FX(64 * 64);
        while (o->x < 0) o->x += ring;
        while (o->x >= ring) o->x -= ring;
        return;
    }
    if (o->x < FX(64)) o->x = FX(64);
    if (o->x > FX(P->L->ncols * 64 - 64)) o->x = FX(P->L->ncols * 64) - 0x40;
}

static void clamp_y(Player *P)     /* FUN_10019d30 */
{
    Obj *o = &P->o;
    if (o->y < FX(64)) o->y = FX(64);
    if (o->y > FX(P->L->nrows * 64)) o->y = FX(P->L->nrows * 64);
}

/* FUN_10015d00: advance the animation script; returns 1 when it reaches an end marker. Opcodes that
 * re-enter the interpreter after switching scripts (0x0d, 0x11, 0x12) do so exactly like the original,
 * then keep reading the new script. */
static int run_script(Player *P)
{
    Obj *o = &P->o;
    const PeImage *pe = P->pe;
    if (--o->timer >= 0) return 0;
    for (int guard = 0; guard < 512; guard++) {
        o->idx++;
        uint32_t a = o->script + (uint32_t)o->idx * 4;
        int16_t v = pe_s16(pe, a);
        uint16_t arg = pe_u16(pe, a + 2);
        if (v >= 0) { o->frame = v; o->timer = (int16_t)arg; return 0; }
        switch (v + 0x34) {
        case 0x00:                                          /* SOUND: footsteps in water (levels 1 and 3) */
            if (P->L->index == 1 || P->L->index == 3) {
                if (!P->water_y || (o->y >> 16) <= P->water_y) {
                    if (P->step_state == 3) sound(P, 0x401e);
                    else if (P->step_state == 2) sound(P, 0x401f);
                    else if (P->step_state == 1) sound(P, 0x4020);
                    if (P->step_state > 0) P->step_state--;
                } else { sound(P, arg); P->step_state = 3; }
            }
            break;
        case 0x2a: P->fade_req = 1; P->signal = v + 0x34; break;       /* fade out, then the restart op */
        case 0x01: case 0x2b: P->signal = v + 0x34; break;  /* level flow (level done) */
        case 0x02:                                          /* carrying: swing the held object while turning (DAT_1006ef9c) */
            P->carry_ang = (P->carry_ang + (o->script != 0x10053c40 ? -0x10 : 0x10)) & 0x3ff;
            break;
        case 0x03: P->carry_pose = arg; break;              /* pose index for the carried object */
        case 0x0b: P->sonic_active = 0; break;
        case 0x0c:                                          /* start the two sonic-sword objects */
            if (!P->sonic_active) { spawn(P, 0x4006); spawn(P, 0x4006); }
            P->sonic_active = 1;
            break;
        case 0x18: o->frame = (int16_t)P->dyn_frame; o->timer = (int16_t)arg; return 0;   /* aimed pose */
        case 0x19: if (held(P, P->m_sword)) o->idx = -1; else { o->script = a + 4; o->idx = -1; } break;
        case 0x1a: flame_stop(P); sound(P, -0x26); break;   /* SOUND: FUN_1001a410 also stops the throw loop */
        case 0x1b: flame_start(P); break;
        case 0x22: case 0x23: spawn(P, arg); break;
        case 0x04:                                          /* swing pole: keep swinging unless pushing back */
            if (!P->dying && (!(P->pad & PAD_LEFT) || !o->facing) && (!(P->pad & PAD_RIGHT) || o->facing))
                set_script(P, SCR_SWING_A);
            break;
        case 0x05:
            if ((!P->dying && (P->pad & PAD_LEFT) && !o->facing) || ((P->pad & PAD_RIGHT) && o->facing))
                set_script(P, SCR_SWING_B);
            break;
        case 0x06: o->flags54 &= ~OVERLAY; break;           /* hide the sword-trail overlay */
        case 0x07:                                          /* show overlay frame arg, additive, 5 ticks */
            P->child_frame = arg; P->child_ticks = 5; P->child_trans = 1;
            o->flags54 |= OVERLAY;
            if ((arg == 0x1df || arg == 0x1e0) && (P->L->index == 3 || P->L->index == 1) && P->water_y &&
                P->water_y - 8 < I(o->y))
                o->flags54 &= ~OVERLAY;             /* the sword trail is not drawn under water */
            break;
        case 0x08: P->react_lock = 0; break;                /* hurt animation may be cancelled from here */
        case 0x09: P->hitflags &= (uint16_t)~arg; break;
        case 0x0a: P->hitflags |= arg; break;
        case 0x0d:                                          /* lightning keeps going while the sword is held */
            if (!held(P, P->m_sword) || P->weapon != WPN_LIGHTNING) {
                P->state = ST_SWORD_END;
                set_script(P, SCR_SWORD_END2);
                run_script(P);
            }
            break;
        case 0x0e: P->shake = 1; break;
        case 0x0f: P->air_attack = 0; break;
        case 0x10: P->air_attack = 1; break;
        case 0x11:                                          /* punch released: finish (or stop if walking) */
            if (!held(P, P->m_punch)) {
                if (P->pad & (PAD_LEFT | PAD_RIGHT)) { go_idle(P); break; }
                P->state = ST_PUNCH;
                set_script(P, SCR_PUNCH_END);
                run_script(P);
            }
            break;
        case 0x12:                                          /* after a crouch attack: crouch or stand */
            if (!(P->pad & PAD_DOWN)) go_idle(P);
            else { P->state = ST_CROUCH; set_script(P, SCR_CROUCH); run_script(P); }
            break;
        case 0x13:                                          /* crouching: punch -> crouch attack */
            if (!held(P, P->m_punch)) {
                if (!(P->pad & PAD_DOWN)) o->script = a + 4;
                o->idx = -1;
            } else { P->state = ST_CROUCH_HIT; set_script(P, SCR_CROUCH_HIT); }
            break;
        case 0x14:
            if (!held(P, P->m_sword)) { P->state = ST_SWORD_END; set_script(P, SCR_SWORD_END3); }
            break;
        case 0x15:                                          /* sword released: recover (combo window) */
            if (!held(P, P->m_sword)) {
                o->flags54 &= ~OVERLAY;
                P->air_attack = 0;
                P->state = ST_SWORD_END;
                set_script(P, SCR_SWORD_END);
            }
            break;
        case 0x16:                                          /* charging: fill the meter, release to strike */
            if (!held(P, P->m_power) || P->dying) {
                set_script(P, SCR_POWER_HIT);
                sound(P, -0x15);                    /* FUN_10007f8e(0x15): stop the charge sound */
            } else {
                P->charge += 0x10;
                if (P->charge > 0xff) {
                    if (!P->charge_full) { P->charge_full = 1; sound(P, 0x4029); }
                    P->charge = 0xff;
                }
            }
            break;
        case 0x17: sound(P, arg); break;
        case 0x1c: o->x += o->facing ? 0x100000 : -0x100000; break;
        case 0x1d:                                          /* opaque overlay frame for 2 ticks */
            P->child_frame = arg; P->child_ticks = 2; P->child_trans = 0;
            o->flags54 |= OVERLAY;
            break;
        case 0x1e: o->flags54 &= ~((uint32_t)(int16_t)arg << 16); break;
        case 0x1f: o->flags54 &= ~(uint32_t)(int16_t)arg; break;
        case 0x20: o->flags54 |= (uint32_t)(int16_t)arg << 16; break;
        case 0x21: o->flags54 |= (uint32_t)(int16_t)arg; break;
        case 0x25: o->flags50 &= ~(uint32_t)(int16_t)arg; break;
        case 0x26: o->flags50 |= (uint32_t)(int16_t)arg; break;
        case 0x27: o->bank = (uint8_t)arg; break;
        case 0x28: if (!(P->pad & PAD_UP)) o->script = a + 4; o->idx = -1; break;
        case 0x29: P->dead = 1; P->signal = 0x29; break;    /* death animation done: restart / continue */
        case 0x2c:
            if (o->script != SCR_IDLE_BREATHE) o->idle_count = 2;
            set_script(P, SCR_IDLE);
            break;
        case 0x2d: {
            int s;
            if (!o->idle_count) {
                int32_t ax = o->x + (o->facing ? -FX(56) : FX(56));
                s = (solid(P, ax, o->y - FX(1), o->z, 1) ? 2 : 0) + rnd(P, 20);
            } else { s = 10; o->idle_count--; }
            set_script(P, s == 0 ? SCR_IDLE_A : s == 1 ? SCR_IDLE_B : s == 2 ? SCR_IDLE_C : SCR_IDLE_BREATHE);
            if (P->carrying) set_script(P, SCR_CARRY_IDLE);
            break;
        }
        case 0x2e: o->flags54 ^= 0x10; break;
        case 0x2f: if (!(P->pad & PAD_DOWN)) o->script = a + 4; o->idx = -1; break;
        case 0x30:
            o->facing = (uint8_t)~o->facing;
            mirror_sync(o);
            break;
        case 0x31: return 1;
        case 0x32: o->script = a + 4; o->idx = -1; break;
        case 0x33: o->idx = -1; break;
        default: break;     /* 0x02 (carry turn), 0x24 (sound bank): no effect here */
        }
    }
    return 0;
}

/* ---- state entries ---- */

static void go_die(Player *P);

/* FUN_10018f60: lives - 1 (never below 0), +1 with the lives cheat; 0 left = game over */
static void lose_life(Player *P)
{
    if (P->dead) return;
    if (P->lives > 0) P->lives--;
    if (P->cheat_lives) P->lives++;
    if (P->lives == 0) P->game_over = 1;
}

static void go_win(Player *P);

static void go_idle(Player *P)          /* FUN_10016790 */
{
    if (P->won) { go_win(P); return; }
    if (P->dying && P->carrying) { P->state = ST_THROW; set_script(P, SCR_THROW); run_script(P); return; }
    if (P->dying) { go_die(P); return; }
    P->state = ST_IDLE;
    set_script(P, SCR_IDLE);
    run_script(P);
    P->o.idle_count = 2;
}

static void go_walk(Player *P)          /* FUN_10016a60 */
{
    Obj *o = &P->o;
    P->state = ST_WALK;
    set_script(P, P->carrying ? SCR_CARRY_WALK : SCR_WALK);
    run_script(P);
    if (P->carrying) { o->vx_target = 0x40000; o->vx_acc = 0x2000; o->vx = 0x20000; }
    else { o->vx_target = 0x80000; o->vx_acc = 0x8000; o->vx = 0x40000; }
    o->vy = 0; o->vy_max = 0;
}

static void go_stop(Player *P)          /* FUN_10016c40 */
{
    Obj *o = &P->o;
    if (P->carrying) { P->state = ST_SKID; set_script(P, SCR_CARRY_STOP); run_script(P); }
    else if (o->vx < 0x40000) go_idle(P);
    else { P->state = ST_SKID; set_script(P, SCR_SKID); run_script(P); }
    o->vx_target = 0; o->vx = 0; o->vx_acc = 0; o->vy = 0; o->vy_max = 0;
}

static void go_turn(Player *P)          /* FUN_10016e70 */
{
    int from_walk = P->state == ST_WALK;
    P->state = ST_TURN;
    set_script(P, from_walk ? SCR_TURN_WALK : SCR_TURN);
    run_script(P);
}

static void jump_common(Player *P)      /* FUN_10017130 */
{
    Obj *o = &P->o;
    o->rising = 0xff;
    o->vy = 0xe0000; o->vy_acc = 0x7000; o->vy_dec = 0xe000; o->vy_max = 0xd0000;
    o->vx_target = 0;
    P->air_attack = 0;
    P->hitflags = 0;
    o->flags54 &= ~OVERLAY;
}

static void go_jump(Player *P)          /* FUN_10017000: standing jump */
{
    Obj *o = &P->o;
    P->state = ST_JUMP;
    set_script(P, SCR_JUMP);
    run_script(P);
    P->jump_kind = 4;
    jump_common(P);
    o->air_speed = 0x40000;
    o->vx_acc = 0x8000;
}

static void go_jump_run(Player *P)      /* FUN_10017060: running jump */
{
    Obj *o = &P->o;
    P->state = ST_JUMP_RUN;
    set_script(P, SCR_JUMP_RUN);
    run_script(P);
    P->jump_kind = 2;
    jump_common(P);
    o->air_speed = o->vx;
}

static void go_fall(Player *P)          /* FUN_10017340 */
{
    Obj *o = &P->o;
    P->state = ST_FALL;
    set_script(P, SCR_FALL);
    run_script(P);
    o->air_speed = o->vx_target;
    o->vx_target = 0x40000; o->vx_dec = 0x8000; o->vx_acc = 0x2000;
    o->vy = 0x40000; o->vy_max = 0xd0000; o->vy_acc = 0x8000;
    o->rising = 0;
    P->jump_kind = 7;
}

static void go_land(Player *P)          /* FUN_100176e0 */
{
    Obj *o = &P->o;
    P->state = ST_LAND;
    set_script(P, SCR_LAND);
    run_script(P);
    P->jump_kind = 0;
    o->vx = 0; o->air_speed = 0; o->vx_acc = 0;
    o->y &= (int32_t)0xffff0000;
    if (P->water_y && P->water_y < I(o->y) && (P->L->index == 1 || P->L->index == 3)) sound(P, 0x4276);   /* FUN_100176e0: splash */
    o->flags54 &= ~OVERLAY;
}

static void go_crouch(Player *P)        /* FUN_10017a50 */
{
    P->state = ST_CROUCH;
    set_script(P, SCR_CROUCH);
    run_script(P);
}

static void go_hang(Player *P)          /* FUN_10018900: grab a ledge at head height */
{
    Obj *o = &P->o;
    o->x = (o->x & (int32_t)0xffc00000) | (o->facing ? 0x100000 : 0x200000);
    o->x += 0x80000;
    o->y = ((o->y - 0x80000) & (int32_t)0xffc00000) + 0x180000;
    o->vx = o->vx_target = o->vx_acc = o->vx_dec = 0;
    o->vy = o->vy_max = o->vy_acc = o->vy_dec = 0;
    P->air_attack = 0;
    P->sword_lock = 0;
    P->state = ST_HANG;
    set_script(P, SCR_HANG);
    run_script(P);
}

static void go_climb(Player *P)         /* end of FUN_10018a10: hop up onto the ledge */
{
    Obj *o = &P->o;
    P->state = ST_CLIMB;
    set_script(P, SCR_CLIMB);
    run_script(P);
    P->jump_kind = ST_CLIMB;
    o->rising = 0xff;
    o->vy = 0xc0000; o->vy_acc = 0x4000; o->vy_dec = 0x10000; o->vy_max = 0xf0000;
    o->vx_target = 0x40000; o->vx = 0x40000; o->air_speed = 0x40000; o->vx_acc = 0x8000;
    P->sword_lock = 0;
}

static void go_fall_death(Player *P)    /* FUN_100170c0 */
{
    Obj *o = &P->o;
    P->state = ST_FALL_DEATH;
    set_script(P, SCR_JUMP);
    run_script(P);
    jump_common(P);
    o->air_speed = 0x40000; o->vx_acc = 0x8000;
    lose_life(P);
    P->dead = 1;
}

/* ---- combat entries ---- */

static int try_jump(Player *P);

static void aim_entry(const Player *P, uint32_t tbl, int i, int16_t e[5])
{
    for (int k = 0; k < 5; k++) e[k] = pe_s16(P->pe, tbl + 10u * (uint32_t)i + 2u * (uint32_t)k);
}

static void flame_place(Player *P)
{
    int16_t e[5];
    aim_entry(P, TBL_FIRE_AIM, P->aim_idx, e);
    P->flame.x = P->o.x + FX(e[2]); P->flame.y = P->o.y + FX(e[3]); P->flame.z = P->o.z;
    P->flame.angle = e[4];
}

static int try_sword(Player *P)         /* FUN_10017ff0: sword, or fire the selected Gift of the Gods */
{
    Obj *o = &P->o;
    if (!held(P, P->m_sword)) return 0;
    P->aim_idx = 0; P->aim_tick = 0; P->aim_anim = 0;
    if (P->weapon == WPN_FIREBALL) {
        P->state = ST_FIRE;
        set_script(P, SCR_FIRE);
        run_script(P);
        P->dyn_frame = 0xd9;
        flame_place(P);
        if (o->facing) P->aim_idx = 0x13;
        P->aim_target = pe_s16(P->pe, TBL_FIRE_AIM + 10u * (uint32_t)P->aim_idx);
    } else if (P->weapon == WPN_LIGHTNING) {
        P->state = ST_LIGHTNING;
        set_script(P, SCR_LIGHTNING);
        run_script(P);
        P->dyn_frame = 0xd9;
        if (o->facing) P->aim_idx = 5;
        P->aim_target = pe_s16(P->pe, TBL_LIGHT_AIM + 10u * (uint32_t)P->aim_idx);
    } else if (P->weapon == WPN_SONIC) {
        P->state = ST_SONIC;
        set_script(P, SCR_SONIC);
        run_script(P);
    } else {
        if (P->sword_lock) return 0;
        P->state = ST_SWORD;
        set_script(P, SCR_SWORD);
        run_script(P);
        P->sword_lock = 1;
    }
    o->vx = 0;
    return 1;
}

/* Fire direction from the pad (shared by FUN_100182c0 / FUN_10018550). */
static int aim_wanted(Player *P)
{
    int u = (P->pad & PAD_UP ? 1 : 0) | (P->pad & PAD_RIGHT ? 2 : 0) | (P->pad & PAD_DOWN ? 4 : 0) |
            (P->pad & PAD_LEFT ? 8 : 0);
    int t = u ? pe_s16(P->pe, TBL_AIM_DIR + 2u * (uint32_t)u) : 200;
    if (t == 200) return P->o.facing ? 0x28 : 0;
    if (t == 100) return (P->o.facing ? 8 : 0) + 0x10;
    return t;
}

static void aim_animate(Player *P, uint32_t tbl)
{
    Obj *o = &P->o;
    if (--P->aim_tick < 0) { P->aim_tick = 1; if (++P->aim_anim > 4) P->aim_anim = 0; }
    int16_t e[5];
    aim_entry(P, tbl, P->aim_idx, e);
    P->dyn_frame = e[1] + P->aim_anim;
    o->facing = e[0] < 0x15 ? 0 : 0xff;
    mirror_sync(o);
}

static int spend_ammo(Player *P)        /* returns 0 when the weapon ran out */
{
    if (P->weapon_timer[P->weapon] != 0) return 1;
    if (--P->ammo[P->weapon] < 1) { P->ammo[P->weapon] = 0; P->weapon = WPN_NONE; return 0; }
    return 1;
}

static int try_swing(Player *P)         /* FUN_10017810: catch a pole with the hand box (frame box 3) */
{
    Obj *o = &P->o;
    Box b;
    if (!P->npoles || !frame_box(P, 3, &b)) return 0;
    for (int i = 0; i < P->npoles; i++) {
        int px = P->poles[i][0] >> 16, py = P->poles[i][1] >> 16, pz = P->poles[i][2] >> 16;
        if (px < b.x0 || px > b.x1 || py < b.y0 || py > b.y1 || pz < b.z0 || pz > b.z1) continue;
        o->x = P->poles[i][0];
        o->y = P->poles[i][1] + 0x300000;
        P->state = ST_SWING;
        o->script = SCR_SWING;
        o->frame = 0x126; o->timer = 1; o->idx = -1;
        return 1;
    }
    return 0;
}

static int try_pickup(Player *P)        /* FUN_10018b10: lift the object Hercules is pushing against */
{
    if (!P->touch_obj || !(P->touch_type == 2 || P->touch_type == 4 || P->touch_type == 5)) return 0;
    P->carried = P->touch_obj;
    P->carrying = P->touch_type;
    P->carry_pose = 0; P->carry_ang = 0;
    P->state = ST_LIFT;
    set_script(P, SCR_LIFT);
    run_script(P);
    return 1;
}

static int try_throw(Player *P)         /* FUN_10018bb0 */
{
    if (!held(P, P->m_sword)) return 0;
    if (!(P->pad & PAD_UP)) { set_script(P, SCR_THROW); P->throw_event = 1; }
    else if (P->carrying == 4) return 0;
    else { set_script(P, SCR_THROW_UP); P->throw_event = 2; }
    P->state = ST_THROW;
    run_script(P);
    return 1;
}

static void go_win(Player *P)           /* FUN_10019160 (HC_DUNIT is always loaded here) */
{
    P->state = ST_WIN;
    set_script(P, SCR_WIN);
    run_script(P);
}

static int try_punch(Player *P)         /* FUN_10017ba0: punch, or start charging the power punch */
{
    if (held(P, P->m_punch)) { P->state = ST_PUNCH; set_script(P, SCR_PUNCH); }
    else if (held(P, P->m_power)) { P->state = ST_POWER; set_script(P, SCR_POWER); }
    else return 0;
    run_script(P);
    P->o.vx = 0;
    return 1;
}

static int try_air_attack(Player *P)    /* FUN_10017290: needs the jump script's window (op 0x10) */
{
    if (P->air_attack && held(P, P->m_sword) && !P->sword_lock) {
        set_script(P, SCR_AIR_SWORD);
        P->sword_lock = 1;
        P->air_attack = 0;
        return 1;
    }
    if (P->air_attack && held(P, P->m_punch)) {
        set_script(P, SCR_AIR_PUNCH);
        P->air_attack = 0;
        return 1;
    }
    return 0;
}

static void turn_check(Player *P)       /* FUN_10017ae0: turn on the spot mid-attack */
{
    Obj *o = &P->o;
    if (o->facing == 0xff && (P->pad & PAD_RIGHT)) { o->facing = 0; mirror_sync(o); }
    else if (o->facing == 0 && (P->pad & PAD_LEFT)) { o->facing = 0xff; mirror_sync(o); }
}

static void go_die(Player *P)           /* FUN_10018f60: pick a death animation that fits the space */
{
    Obj *o = &P->o;
    lose_life(P);
    ground_follow(P);
    int r = 0, l = 0;
    for (int d = 0x100000; d < 0x410000; d += 0x100000) {
        if (solid(P, o->x + d, o->y - 0x20000, o->z, 1)) r++;
        if (solid(P, o->x - d, o->y - 0x20000, o->z, 1)) l++;
    }
    P->state = ST_DYING;
    uint32_t s;
    if (l && r) s = SCR_DIE_BOTH;
    else {
        if (r + l == 0 && rnd(P, 100) < 50) l = 1;
        s = (l == 0) == (o->facing != 0) ? SCR_DIE_A : SCR_DIE_B;
    }
    set_script(P, s);
    run_script(P);
}

static int back_free(Player *P)         /* FUN_100196f0: room behind to be knocked back */
{
    Obj *o = &P->o;
    int32_t bx = o->x + (o->facing ? 0x1c0000 : -0x1c0000);
    return !solid(P, bx, o->y - 0x200000, o->z, 1) && !solid(P, bx, o->y - 0x100000, o->z, 1) &&
           !solid(P, bx, o->y, o->z, 1);
}

/* FUN_10018c50: apply a pending hit (requested by player_damage) once per tick. */
static void take_damage(Player *P)
{
    Obj *o = &P->o;
    int nomove = (P->hit_req & 0x10) != 0;
    int strong = (P->hit_req & 0x20) != 0 || (P->hit_kind > 0x65 && P->hit_kind < 0x68);
    P->hit_req &= ~0x10;
    int lvl = P->hit_req;
    if (lvl < 2) nomove = 1;
    if (lvl == 0 || P->invuln || P->weapon == WPN_INVULNERABLE) return;
    switch (P->state) {
    case ST_IDLE: case ST_WALK: case ST_SKID: case ST_TURN: case ST_LAND: case ST_CROUCH: case ST_SWORD:
    case ST_CROUCH_HIT: case ST_PUNCH: case ST_POWER:
        if (nomove || P->carrying || (P->cheats & 2)) break;
        P->react_lock = 1;
        {
            int d = P->hit_dir & 7;
            if (o->facing & 0x10) d ^= 2;
            uint32_t s = (d & 2) ? SCR_HURT_BACK : (strong && back_free(P)) ? SCR_HURT_STRONG : SCR_HURT_FRONT;
            P->state = ST_HURT;
            set_script(P, s);
            run_script(P);
        }
        P->charge = 0;
        o->flags54 &= ~OVERLAY;
        break;
    case ST_JUMP_RUN: case ST_JUMP: case 5: case ST_FALL: case ST_CLIMB:
        if (P->cheats & 2) break;
        set_script(P, SCR_HURT_AIR);
        P->air_attack = 0;
        o->flags54 &= ~OVERLAY;
        break;
    case ST_HURT: case ST_DYING: case 0x19: case 0x1b:
        return;                                 /* request stays pending until the reaction ends */
    default: break;
    }
    if (!(P->cheats & 6)) {
        int dmg = (int)pe_u32(P->pe, TBL_HURT_DAMAGE + 4u * (uint32_t)(((lvl - 1) & 7) + (P->difficulty & 3) * 8));
        P->health -= dmg;
        if (P->health < 1) P->health = 0;
    }
    P->hit_req = 0;
    P->flash_idx = 0;
    P->invuln = 0x3c;
    if (nomove) o->vx = 0;
}

/* ---- movement helpers ---- */

static void move_x(Player *P)           /* FUN_10019a10 */
{
    Obj *o = &P->o;
    if (o->vx != o->vx_target) {
        if (o->vx > o->vx_target) { o->vx -= o->vx_dec; if (o->vx < o->vx_target) o->vx = o->vx_target; }
        else { o->vx += o->vx_acc; if (o->vx > o->vx_target) o->vx = o->vx_target; }
    }
    o->x += o->facing ? -o->vx : o->vx;
    clamp_x(P);
}

/* Free-space tests in front of the player (FUN_10019900 / 10019820 / 10019890 / 100197b0). */
#define CARRY_EXTRA(P) ((P)->carrying ? FX(28) : 0)     /* FUN_10019900 & co: the held object is probed too */
static int free_right_idle(Player *P) { return !solid(P, P->o.x + FX(36) + CARRY_EXTRA(P), P->o.y - FX(8), P->o.z, 1); }
static int free_left_idle(Player *P) { return !solid(P, P->o.x - FX(36) - CARRY_EXTRA(P), P->o.y - FX(1), P->o.z, 1); }
static int free_right_walk(Player *P) { return !solid(P, P->o.x + FX(28) + CARRY_EXTRA(P), P->o.y - FX(8), P->o.z, 1); }
static int free_left_walk(Player *P) { return !solid(P, P->o.x - FX(28) - CARRY_EXTRA(P), P->o.y - FX(1), P->o.z, 1); }

static int air_free(Player *P)          /* FUN_10019650 */
{
    Obj *o = &P->o;
    int32_t ax = o->x + (o->facing ? -FX(28) : FX(28));
    return !solid(P, ax, o->y - FX(32), o->z, 1) && !solid(P, ax, o->y - FX(16), o->z, 1) &&
           !solid(P, ax, o->y, o->z, 1);
}

static void air_move(Player *P)         /* FUN_100174e0 */
{
    Obj *o = &P->o;
    o->vx_target = 0;
    if (P->state == ST_CLIMB) { o->vx = o->vx_target = 0x30000; }
    else if (P->pad & (PAD_LEFT | PAD_RIGHT)) o->vx_target = o->air_speed;
    if (!o->facing ? (P->pad & PAD_LEFT) : (P->pad & PAD_RIGHT)) {
        int flip = 1;
        if (P->jump_kind == 5) {            /* swing jump: a hard reversal brakes instead of turning at once */
            if (o->vx > 0xbfff) { o->vx_target = 0x4000; o->vx_dec = 0x5000; flip = 0; }
            else o->vx_acc = 0x6000;
        }
        if (flip) {
            o->facing = (uint8_t)~o->facing;
            o->flags54 = (o->flags54 & ~0x10u) | (o->facing & 0x10);
        }
    }
    if (P->state == ST_CLIMB) { move_x(P); return; }
    move_x(P);
    if (!air_free(P)) o->x = o->px;
    int l = solid(P, o->x - FX(28), o->y - FX(1), o->z, 1), r = solid(P, o->x + FX(28), o->y - FX(1), o->z, 1);
    if (l && r) return;
    if (l) o->x += 0x20000;
    if (r) o->x -= 0x20000;
}

static int try_hang(Player *P)          /* FUN_100188a0 */
{
    Obj *o = &P->o;
    if (P->state == ST_CLIMB) return 0;
    int32_t ax = o->x + (o->facing ? -FX(36) : FX(36));
    if (solid(P, ax, o->y - FX(48), o->z, 0x3000)) { go_hang(P); return 1; }
    return 0;
}

static void vertical(Player *P)         /* FUN_10019d70 */
{
    Obj *o = &P->o;
    if (o->rising) {
        rise_snap(P);
        o->vy -= o->vy_dec;
        if (o->vy < 0) {
            o->vy = 0; o->rising = 0; o->vy_max = 0xd0000; o->vy_acc = 0x6000;
            if (P->state != ST_DYING && P->state != ST_FALL_DEATH) {
                if (P->state == ST_JUMP && (P->pad & PAD_DOWN)) {   /* ground pound */
                    P->state = ST_POUND;
                    set_script(P, SCR_POUND);
                    run_script(P);
                    o->vy_acc = 0x10000; o->vy_max = 0x200000;
                    o->flags54 &= ~OVERLAY;
                } else P->state = ST_FALL;
            }
        } else {
            o->y -= o->vy;
            clamp_y(P);
        }
        return;
    }
    o->vy += o->vy_acc;
    if (o->vy > o->vy_max) o->vy = o->vy_max;
    o->y += o->vy;
    if (o->y > FX(P->L->nrows * 64)) {
        if (P->state != ST_FALL_DEATH && P->state != ST_DYING) go_fall_death(P);
        o->y = FX(P->L->nrows * 64);
        o->vy = 0x80000;
        return;
    }
    clamp_y(P);
}

/* FUN_10016990: start walking or turning from a standstill. */
static int try_walk(Player *P)
{
    Obj *o = &P->o;
    if (!(P->pad & PAD_LEFT)) {
        if (P->pad & PAD_RIGHT) {
            if (!(o->flags54 & 0x10)) {
                if (free_right_idle(P)) { go_walk(P); o->facing = 0; return 1; }
            } else if (free_right_idle(P) || o->facing) { o->facing = 0xff; go_turn(P); return 1; }
        }
    } else if (o->flags54 & 0x10) {
        if (free_left_idle(P)) { go_walk(P); o->facing = 0xff; return 1; }
    } else if (free_left_idle(P) || o->facing != 0xff) { o->facing = 0; go_turn(P); return 1; }
    return 0;
}

static int try_jump(Player *P) { if ((P->pressed & P->m_jump) && !P->carrying) { go_jump(P); return 1; } return 0; }
static int try_jump_run(Player *P)
{
    if ((P->pressed & P->m_jump) && !P->carrying) { go_jump_run(P); return 1; }
    return 0;
}
static int try_crouch(Player *P)        /* FUN_10017a20 */
{
    if ((P->pad & PAD_DOWN) && !P->crouch_lock) { go_crouch(P); return 1; }
    return 0;
}
static int try_turn(Player *P)          /* FUN_10016e30 */
{
    if (P->pad & (P->o.facing ? PAD_RIGHT : PAD_LEFT)) { go_turn(P); return 1; }
    return 0;
}

/* ---- OBJECTS: depth-plane switching ---- */

/* FUN_1002ff50 (called from FUN_10015780 every tick): attribute flags 0x10 / 0x20 under the player open a
 * doorway to the plane behind / in front (planes are 128 units deep). */
static void plane_zone(Player *P)
{
    Obj *o = &P->o;
    int z = I(o->z);
    P->zone = 0; P->zmin = P->zmax = z;
    int a = -1;
    if (P->obj_attr) a = P->obj_attr(P->obj_ctx, I(o->x), I(o->y), z);
    if (a < 0) a = level_attr(P->L, I(o->x), I(o->y), z);
    if (getenv("PLANE_LOG")) { static int n; if ((n++ % 10) == 0) printf("PL x=%d y=%d z=%d a=%d f=%x\n", I(o->x), I(o->y), z, a, a >= 0 && a < 128 ? P->L->flags[a] : -1); }
    if (a < 0 || a > 127) return;
    uint16_t f = P->L->flags[a];
    if (f & 0x8000) {   /* FUN_1002ff50: special attribute, handler table entry (flags >> 8) & 0x7f; Thebes attr 25 = FUN_100117b0 */
        if (P->L->index == 5 && ((f >> 8) & 0x7f) == 2 && o->x > 0x29fe0000) o->x = 0x29fe0000;
        return;
    }
    if (f & 0x10) {
        int b = z & 0x7f80;
        if (b < P->zmin) P->zmin = b;
        if (b + 0x80 > P->zmax) P->zmax = b + 0x80;
        P->zone++;
    }
    if (f & 0x20) {
        int b = (z - 0x40) & 0x7f80;
        if (b < P->zmin) P->zmin = b;
        if (b + 0x80 > P->zmax) P->zmax = b + 0x80;
        P->zone++;
    }
}

static void plane_speed(Player *P)
{
    int idx = pe_s16(P->pe, 0x10055240 + 4 * (uint32_t)P->L->index + (P->zface ? 0 : 2));
    P->zstep = idx;
}

static void go_plane(Player *P)         /* FUN_10017d60 */
{
    Obj *o = &P->o;
    o->vx = o->vx_target = o->vy = o->vy_max = 0;
    P->state = ST_PLANE;
    P->zface = (P->pad & PAD_UP) ? 0xff : 0;
    set_script(P, P->zface ? SCR_PLANE_IN : SCR_PLANE_OUT);
    run_script(P);
    plane_speed(P);
}

static int try_plane(Player *P)         /* FUN_10017d00 */
{
    if (!P->zone) return 0;
    int z = I(P->o.z);
    if (((P->pad & PAD_UP) && z < P->zmax) || ((P->pad & PAD_DOWN) && z > P->zmin)) { go_plane(P); return 1; }
    return 0;
}

static void go_plane_end(Player *P)     /* FUN_10016cf0 */
{
    Obj *o = &P->o;
    P->state = ST_SKID;
    set_script(P, P->zface ? SCR_PLANE_END_IN : SCR_PLANE_END_OUT);
    run_script(P);
    o->vx = o->vx_target = o->vx_acc = o->vy = o->vy_max = 0;
}

static int plane_clamp(Player *P)       /* FUN_10019cf0 */
{
    Obj *o = &P->o;
    int z = I(o->z);
    if (z < P->zmin) { o->z = FX(P->zmin); return 1; }
    if (z > P->zmax) { o->z = FX(P->zmax); return 1; }
    return 0;
}

static void st_plane(Player *P)         /* FUN_10017eb0 */
{
    Obj *o = &P->o;
    if (!P->zface) { if (P->pad & PAD_UP) { o->script = SCR_PLANE_IN; P->zface = 0xff; plane_speed(P); } }
    else if (P->pad & PAD_DOWN) { o->script = SCR_PLANE_OUT; P->zface = 0; plane_speed(P); }
    /* FUN_100291d0: step along the per-level move table (DAT_10054da0, 6-byte {dx, dy, dz}) */
    uint32_t e = 0x10054da0 + 6 * (uint32_t)P->zstep;
    int dx = pe_s16(P->pe, e), dy = pe_s16(P->pe, e + 2), dz = pe_s16(P->pe, e + 4);
    o->x += o->facing ? -FX(dx) : FX(dx);
    o->y += FX(dy);
    o->z += P->zface ? FX(dz) : -FX(dz);
    if (plane_clamp(P)) {
        if (!ground_follow(P)) go_fall(P); else go_plane_end(P);
        return;
    }
    if (!(P->pad & (PAD_UP | PAD_DOWN))) {
        int cur = I(o->z) & 0x7f, prev = I(o->pz) & 0x7f;
        if (!P->zface) {
            if (prev - cur <= 0) { ground_follow(P); o->z = (o->z & 0x7f800000) + 0x800000; go_plane_end(P); return; }
        } else if (prev - cur >= 0) { ground_follow(P); o->z &= 0x7f800000; go_plane_end(P); return; }
    }
    if (!ground_follow(P)) { go_fall(P); return; }
    run_script(P);
}

/* ---- state handlers ---- */

static void st_idle(Player *P)          /* FUN_10016840 */
{
    Obj *o = &P->o;
    if (P->won) { go_win(P); return; }
    if (P->dying) { go_idle(P); return; }
    if (o->script != SCR_IDLE_B) o->flags54 &= ~OVERLAY;
    P->hitflags = 0;
    if (!ground_follow(P)) { go_fall(P); return; }
    if (!P->carrying) {
        if (try_sword(P) || try_punch(P) || try_plane(P) || try_crouch(P)) return;   /* OBJECTS: try_plane */
    } else if (try_throw(P)) return;
    if (P->zone && (I(P->o.z) & 0x7f)) { run_script(P); return; }   /* OBJECTS: mid-doorway */
    if (try_jump(P) || try_walk(P) || (!P->carrying && try_pickup(P))) return;
    run_script(P);
}

static void st_walk(Player *P)          /* FUN_10016b40 */
{
    Obj *o = &P->o;
    if (P->dying || P->won) { go_stop(P); return; }
    if (!P->carrying && (try_sword(P) || try_punch(P) || try_jump_run(P))) return;
    run_script(P);
    int fwd = o->facing ? PAD_LEFT : PAD_RIGHT, back = o->facing ? PAD_RIGHT : PAD_LEFT;
    if (!(P->pad & fwd)) { if (P->pad & back) go_turn(P); else go_stop(P); return; }
    move_x(P);
    if (!ground_follow(P)) { go_fall(P); return; }
    if (!(o->facing ? free_left_walk(P) : free_right_walk(P))) {
        o->x = o->px;
        go_stop(P);
        ground_follow(P);
    }
}

static void st_skid(Player *P)          /* FUN_10016da0 */
{
    ground_follow(P);
    if (!P->dying && !P->carrying && !P->won) {
        if (try_sword(P) || try_punch(P)) return;
        if (I(P->o.z) == P->zmin || I(P->o.z) == P->zmax) { if (try_jump(P) || try_walk(P)) return; }   /* OBJECTS */
        if (try_plane(P) || try_crouch(P)) return;
    }
    if (run_script(P)) go_idle(P);
}

static void st_turn(Player *P)          /* FUN_10016f40 */
{
    if (!P->dying && !P->carrying && !P->won && (try_plane(P) || try_crouch(P) || try_jump(P))) return;
    ground_follow(P);
    if (run_script(P)) go_idle(P);
}

static void st_land(Player *P)          /* FUN_100177a0 */
{
    if (!P->dying && !P->won) {
        if (try_sword(P) || try_punch(P) || try_jump(P) || try_walk(P) || try_plane(P) || try_crouch(P) || try_turn(P)) return;
    }
    ground_follow(P);
    if (run_script(P)) go_idle(P);
}

static void st_crouch(Player *P)        /* FUN_10017a90 */
{
    if (!ground_follow(P)) { go_fall(P); return; }
    if (try_jump(P)) return;
    turn_check(P);
    if (!(P->pad & PAD_DOWN) && try_walk(P)) return;
    if (run_script(P)) go_idle(P);
}

static void st_sword(Player *P)         /* FUN_10018240: state 0xc */
{
    if (!ground_follow(P)) { go_fall(P); return; }
    if (P->o.script == SCR_SWORD_END && try_sword(P)) return;
    if (try_jump(P)) { P->air_attack = 0; P->o.flags54 &= ~OVERLAY; return; }
    turn_check(P);
    if (run_script(P)) { go_idle(P); P->sword_lock = 0; }
}

static void st_sword_end(Player *P)     /* FUN_10018830: state 0x2e, recovering; chain another swing */
{
    if (P->weapon == WPN_NONE && P->air_attack && try_sword(P)) return;       /* FUN_10018830: DAT_1006efb4 == 0 */
    if (try_jump(P) || try_walk(P) || try_punch(P) || try_crouch(P)) return;
    if (!ground_follow(P)) { go_fall(P); return; }
    if (run_script(P)) go_idle(P);
}

static void st_punch(Player *P)         /* FUN_10017c30: state 0x29 */
{
    if (!ground_follow(P)) { go_fall(P); return; }
    if (try_jump(P)) return;
    turn_check(P);
    if (run_script(P)) go_idle(P);
}

static void st_power(Player *P)         /* FUN_10017c70: state 0x2a; the strike drains the meter */
{
    if ((P->hitflags & HIT_POWER) && P->charge) { P->charge -= 0x3c; if (P->charge < 0) P->charge = 0; }
    if (!ground_follow(P)) { go_fall(P); return; }
    if (try_jump(P)) return;
    if (run_script(P)) go_idle(P);
}

static void st_crouch_hit(Player *P)    /* FUN_10017cd0: state 0xe */
{
    if (!ground_follow(P)) { go_fall(P); return; }
    if (try_jump(P)) return;
    if (run_script(P)) go_idle(P);
}

static void st_hurt(Player *P)          /* FUN_10018ed0: state 0x11 */
{
    P->o.idle_count = 3;
    if (!ground_follow(P)) { go_fall(P); return; }
    if (!P->react_lock) {
        if (try_sword(P) || try_punch(P) || try_jump(P) || try_walk(P) || try_plane(P) || try_crouch(P) || try_turn(P)) return;
    }
    if (run_script(P)) { P->hit_req = 0; go_idle(P); }
}

static void st_dying(Player *P)         /* FUN_10019140: state 0x16 */
{
    P->hit_req = 0;
    ground_follow(P);
    run_script(P);
}

static void st_pound(Player *P)         /* FUN_10019290: state 0x18, diving down */
{
    Obj *o = &P->o;
    vertical(P);
    if (!land_test(P)) {
        run_script(P);
        int l = solid(P, o->x - 0x1c0000, o->y - 0x10000, o->z, 1);
        int r = solid(P, o->x + 0x1c0000, o->y - 0x10000, o->z, 1);
        if (l && r) return;
        if (l) o->x += 0x20000;
        if (r) o->x -= 0x20000;
        return;
    }
    if (P->dying) { go_land(P); return; }
    P->state = ST_POUND_LAND;                       /* (SCR_POUND_LAND_OBJ when landing on an object) */
    set_script(P, SCR_POUND_LAND);
    run_script(P);
    P->jump_kind = 0;
    o->vx = 0; o->air_speed = 0; o->vx_acc = 0;
}

static void st_pound_land(Player *P)    /* FUN_100193f0: state 0x12 */
{
    P->crouch_lock = 0x32;
    ground_follow(P);
    if (run_script(P)) {
        if (P->o.script != SCR_POUND_LAND_OBJ) { go_idle(P); return; }
        go_fall(P);
        P->jump_kind = 0x12;
    }
}

static void st_air_up(Player *P)        /* FUN_100171b0: states 2, 4, 5, 0x13, 0x17 */
{
    Obj *o = &P->o;
    air_move(P);
    if (!P->dying && P->state != ST_FALL_DEATH) {
        if (try_swing(P) || try_hang(P)) return;
        try_air_attack(P);
        if ((P->state == ST_JUMP || P->state == ST_JUMP_RUN) && !(P->m_jump & P->pad)) {
            o->vy -= 0xc000;                        /* short hop when the button is released */
            if (o->vy < 0) o->vy = 0;
        }
    }
    rise_snap(P);
    if (o->frame != 0x1c7 && o->frame != 0x1c8) vertical(P);   /* take-off frames hold the jump */
    run_script(P);
}

static void st_fall(Player *P)          /* FUN_10017400 */
{
    vertical(P);
    if (P->state == ST_FALL_DEATH) return;
    if (!P->dying) {
        if (P->jump_kind != 0x12 && (try_hang(P) || try_swing(P))) return;
        try_air_attack(P);
        /* NESSUS: FUN_10017400 - in the Forest, falling onto Nessus' offered back (box 3) mounts him */
        if (P->L->index == 3 && P->nes_offer == 1 && P->nes_box_ok) {
            int px = I(P->o.x), py = I(P->o.y);
            if (px >= P->nes_box[0] && px <= P->nes_box[3] && py >= P->nes_box[1] && py <= P->nes_box[4]) {
                P->nes_offer = 2; P->state = 0xd;
                return;
            }
        }
    }
    if (land_test(P)) { go_land(P); return; }
    air_move(P);
    run_script(P);
}

static void st_swing(Player *P)         /* FUN_100178f0: state 9, let go at the end of the swing */
{
    Obj *o = &P->o;
    if (!run_script(P)) return;
    P->state = 5;
    set_script(P, SCR_SWING_JUMP);
    run_script(P);
    P->jump_kind = 5;
    o->rising = 0xff;
    o->air_speed = 0x40000;
    o->vy = 0xf0000; o->vy_acc = 0x7000; o->vy_dec = 0xe000; o->vy_max = 0xd0000;
    o->vx_target = 0x40000; o->vx = 0x40000; o->vx_acc = 0x4000;
    if (P->dying) { o->vx_target = 0x8000; o->vx = 0x8000; o->vx_acc = 0x1000; }
    P->air_attack = 0;
    o->air_speed = o->vx;
}

static void st_fire(Player *P)          /* FUN_100182c0: state 0x2b, fireball stream aimed with the pad */
{
    Obj *o = &P->o;
    if (!ground_follow(P)) { flame_stop(P); go_fall(P); return; }
    if (try_jump(P)) { flame_stop(P); return; }
    if (P->flame.active) {
        if (!spend_ammo(P)) {
            flame_stop(P);
            P->state = ST_SWORD_END; set_script(P, SCR_SWORD_END2); run_script(P);
            return;
        }
        P->aim_target = aim_wanted(P);
        int cur = pe_s16(P->pe, TBL_FIRE_AIM + 10u * (uint32_t)P->aim_idx);
        if (P->aim_target != cur) P->aim_idx += cur < P->aim_target ? 1 : -1;
        if (P->aim_idx < 0) P->aim_idx = 0;
        if (P->aim_idx > 19) P->aim_idx = 19;
        aim_animate(P, TBL_FIRE_AIM);
        flame_place(P);
    }
    (void)o;
    if (run_script(P)) { P->state = ST_SWORD_END; set_script(P, SCR_SWORD_END2); run_script(P); }
}

static void st_lightning(Player *P)     /* FUN_10018550: state 0x2c, aim then strike */
{
    Obj *o = &P->o;
    if (!ground_follow(P)) { go_fall(P); return; }
    if (try_jump(P)) return;
    if (pe_s16(P->pe, o->script + 4u * (uint32_t)o->idx) == -0x1c) {     /* sitting on the op 0x18 pose */
        P->aim_target = aim_wanted(P);
        int16_t e[5];
        aim_entry(P, TBL_LIGHT_AIM, P->aim_idx, e);
        if (P->aim_target == e[0]) {
            uint32_t sc = e[1] == 0xeb ? SCR_LIGHT_A : e[1] == 0xf5 ? SCR_LIGHT_B : e[1] == 0xfa ? SCR_LIGHT_C : 0;
            if (sc) set_script(P, sc);
        } else {
            P->aim_idx += e[0] < P->aim_target ? 1 : -1;
            if (P->aim_idx < 0) P->aim_idx = 0;
            if (P->aim_idx > 5) P->aim_idx = 5;
        }
        aim_animate(P, TBL_LIGHT_AIM);
    }
    if (run_script(P)) { P->state = ST_SWORD_END; set_script(P, SCR_SWORD_END2); run_script(P); }
}

static void st_sonic(Player *P)         /* FUN_10018770: state 0x2d */
{
    if (!ground_follow(P)) { go_fall(P); return; }
    if (try_jump(P)) return;
    if (P->sonic_active && !spend_ammo(P)) {
        P->sonic_active = 0;
        P->state = ST_SWORD_END;
        set_script(P, SCR_SWORD_END3);
        return;
    }
    if (run_script(P)) go_idle(P);
}

static void st_lift(Player *P) { if (run_script(P)) go_idle(P); }      /* FUN_10018ba0: state 0x14 */

static void st_throw(Player *P)         /* FUN_10018c30: state 0x2f */
{
    if (run_script(P)) { P->carrying = 0; P->carried = NULL; go_idle(P); }
}

static void st_win(Player *P) { run_script(P); }                       /* FUN_100191e0: state 0x19 */

static void st_hang(Player *P)          /* FUN_10018a10 */
{
    if (run_script(P)) go_climb(P);
}

void player_init(Player *P, const PeImage *pe, const Level *L, int x, int y, int z)
{
    memset(P, 0, sizeof *P);
    P->pe = pe;
    P->L = L;
    P->rng = 1;
    /* ENG1 default masks (DAT_1004d958: +0x6c..+0x76) */
    P->m_sword = PAD_SQUARE; P->m_jump = PAD_CROSS; P->m_punch = PAD_CIRCLE; P->m_power = PAD_TRIANGLE;
    P->m_prev = PAD_L1; P->m_next = PAD_R1;
    P->health = 0x80; P->max_health = 0x80;
    P->lives = 3;
    P->difficulty = 1;
    P->blend_mode = -1; P->blend_level = 0xa0;
    P->water_y = L->index == 3 ? 0x2e1 : L->index == 1 ? 0x360 : 0;     /* SOUND: FUN_10015090 (Forest) / FUN_1003ca10 (Training) */
    Obj *o = &P->o;
    o->x = o->px = FX(x); o->y = o->py = FX(y); o->z = o->pz = FX(z);
    o->bank = 1;
    o->flags54 = 0;
    go_idle(P);                 /* FUN_10015090: FUN_10016790, then state 10 holds for two ticks before idling */
    P->state = ST_START; P->start_hold = 2;
}

/* Per-tick bookkeeping from FUN_10015780 / FUN_10012040, then the state handler. */
void player_tick(Player *P, uint16_t pad)
{
    Obj *o = &P->o;
    if (P->dying || P->won) pad &= (uint16_t)~(P->m_sword | P->m_jump | P->m_punch | P->m_power | PAD_DOWN);
    P->pressed = pad & ~P->pad;
    P->pad = pad;
    P->nsounds = 0;
    P->nspawns = 0;
    P->throw_event = 0;

    /* weapon charge timers (FUN_10015780) and weapon select (FUN_10012040; the original polls the held
     * buttons on a 12-tick HUD cadence, here a press switches) */
    if (P->wpn_snd && --P->wpn_snd < 1) { P->wpn_snd = 0; sound(P, 0x14); }     /* AUDSHELL: FUN_10012040 */
    P->weapon_timer[0] = 5;
    if (--P->weapon_timer[P->weapon] < 0)
        P->weapon_timer[P->weapon] = pe_s16(P->pe, TBL_RELOAD + 2u * (uint32_t)P->weapon);
    if (P->state != ST_FIRE && P->state != ST_LIGHTNING && P->state != ST_SONIC && !P->dying &&
        (P->pressed & (P->m_prev | P->m_next))) {
        int old = P->weapon, step = (P->pressed & P->m_prev) ? -1 : 1;
        P->wpn_snd = 0;
        do { P->weapon = (P->weapon + step + 5) % 5; } while (P->weapon && P->ammo[P->weapon] <= 0);
        if (P->weapon != old) { if (P->weapon == WPN_INVULNERABLE) P->wpn_snd = 0x14; sound(P, 0x27); }
    }

    /* sword-trail overlay lifetime */
    P->child_ticks = (P->child_ticks & 7) - 1;
    if (P->child_ticks < 0) { o->flags54 &= ~OVERLAY; P->child_ticks = 0; }

    /* invulnerability countdown and the hurt flash (obj+0xec, DAT_1006ef94, table 0x100552d0) */
    if (P->invuln) P->invuln--;
    if (P->weapon == WPN_INVULNERABLE) {                /* Gift: invulnerable while charges last */
        P->inv_tick--;
        P->blend_mode = 0;
        P->blend_level = pe_s16(P->pe, TBL_INV_FLASH + 2u * (uint32_t)(P->inv_tick & 15));
        if (P->weapon_timer[WPN_INVULNERABLE] == 0 && --P->ammo[WPN_INVULNERABLE] <= 0) {
            P->ammo[WPN_INVULNERABLE] = 0; P->weapon = WPN_NONE;
        }
    } else if (P->invuln) {
        P->invuln--;
        P->blend_mode = 1;
        P->blend_level = pe_s16(P->pe, TBL_FLASH + 2u * (uint32_t)P->flash_idx++);
        if (P->blend_level == 0xfe) {
            P->blend_level = pe_s16(P->pe, TBL_FLASH + 2u * (uint32_t)P->flash_idx++);
            P->blend_mode = 0;
        }
        if (P->blend_level == 0) { P->flash_idx = 0; P->invuln = 0; P->blend_mode = -1; P->blend_level = 0xa0; }
    } else { P->flash_idx = 0; P->blend_mode = -1; P->blend_level = 0xa0; }

    /* screen shake after a ground pound (script op 0x0e) */
    if (P->shake) {
        P->shake++;
        P->shake_y = (int)pe_u32(P->pe, TBL_SHAKE + 4u * (uint32_t)P->shake) >> 16;
        if (!pe_u32(P->pe, TBL_SHAKE + 4u * (uint32_t)P->shake)) { P->shake = 0; P->shake_y = 0; }
    }

    if (P->heal_cd > 0) P->heal_cd--;
    P->hitflags &= (uint16_t)~P->consumed;
    P->consumed = 0;
    if (P->sword_lock && !held(P, P->m_sword)) P->sword_lock = 0;
    if (P->crouch_lock) { if (!(P->pad & PAD_DOWN)) P->crouch_lock = 0; else P->crouch_lock--; }
    if (P->charge_full && P->charge < 0x81) P->charge_full = 0;
    if (P->charge && P->state != ST_POWER) P->charge = 0;

    if (!(P->cheats & 1) && P->health == 0 && !P->dying) P->dying = 1;
    if (!P->dying) {
        take_damage(P);
        if (P->health == 0 && !(P->cheats & 1)) P->dying = 1;
    }

    o->px = o->x; o->py = o->y; o->pz = o->z;
    plane_zone(P);                                          /* OBJECTS */
    switch (P->state) {
    case ST_PLANE: st_plane(P); break;                      /* OBJECTS */
    case ST_START:                                          /* FUN_10019440 */
        if (--P->start_hold == 0) go_idle(P);
        run_script(P);
        break;
    case 0xd: P->o.x = P->nes_x; P->o.y = P->nes_y; P->o.frame = 0; break;   /* NESSUS: FUN_10019260, riding */
    case ST_IDLE: st_idle(P); break;
    case ST_WALK: st_walk(P); break;
    case ST_SKID: st_skid(P); break;
    case ST_TURN: st_turn(P); break;
    case ST_LAND: st_land(P); break;
    case ST_CROUCH: st_crouch(P); break;
    case ST_FALL: st_fall(P); break;
    case ST_HANG: st_hang(P); break;
    case ST_SWORD: st_sword(P); break;
    case ST_SWORD_END: st_sword_end(P); break;
    case ST_PUNCH: st_punch(P); break;
    case ST_POWER: st_power(P); break;
    case ST_CROUCH_HIT: st_crouch_hit(P); break;
    case ST_HURT: st_hurt(P); break;
    case ST_DYING: st_dying(P); break;
    case ST_POUND: st_pound(P); break;
    case ST_POUND_LAND: st_pound_land(P); break;
    case ST_SWING: st_swing(P); break;
    case ST_FIRE: st_fire(P); break;
    case ST_LIGHTNING: st_lightning(P); break;
    case ST_SONIC: st_sonic(P); break;
    case ST_LIFT: st_lift(P); break;
    case ST_THROW: st_throw(P); break;
    case ST_WIN: st_win(P); break;
    case ST_JUMP: case ST_JUMP_RUN: case 5: case ST_CLIMB: case ST_FALL_DEATH: st_air_up(P); break;
    default: go_idle(P); break;
    }
    if (P->state != ST_FIRE) flame_stop(P);
    P->touch_obj = NULL; P->touch_type = 0;     /* the object system sets these again before the next tick */
}

void player_win(Player *P) { P->won = 1; }

void player_nessus_throw(Player *P, int32_t x, int32_t y, uint8_t facing, int32_t vx)   /* NESSUS: FUN_1000bb90 */
{
    Obj *o = &P->o;
    o->x = x; o->y = y;
    o->facing = facing;
    o->flags54 = (o->flags54 & ~0x10u) | (facing & 0x10);
    o->vx = vx; o->vy = -0x30000;
    go_fall(P);
}

/* ---- combat API ---- */

/* FUN_10030e40 / FUN_1002a880: collision box k (1 = body, 2 = attack) of the current frame, in world units. */
static int frame_box(const Player *P, int k, Box *b)
{
    const Obj *o = &P->o;
    if (!P->banks || o->bank >= 8 || o->frame <= 0) return 0;
    const Bank *bk = &P->banks[o->bank];
    if (!bk->d || o->frame > bk->count) return 0;
    const uint8_t *d = bk->d;
    uint32_t ea = 4 + 4 * (uint32_t)o->frame;
    if (ea + 4 > bk->size) return 0;
    uint32_t e = d[ea] | d[ea + 1] << 8 | d[ea + 2] << 16 | (uint32_t)d[ea + 3] << 24;
    if (!e) return 0;
    uint32_t f = (e >> 8) + 4;
    if (f + 2 > bk->size || (int8_t)d[f + 1] < k) return 0;
    uint32_t p = f + 2 + 6 * (uint32_t)(k - 1);
    if (p + 6 > bk->size) return 0;
    const int8_t *c = (const int8_t *)(d + p);
    if (!c[0] && !c[1] && !c[2] && !c[3] && !c[4] && !c[5]) return 0;
    int x = c[0], y = c[1], z = c[2] >> 1, w = c[3] - c[0], h = c[4] - c[1], dz = (c[5] >> 1) - z;
    if (!w && !h) return 0;
    int ox = o->x >> 16, oy = o->y >> 16, oz = o->z >> 16;
    if (o->flags54 & 0x10) { b->x1 = ox - x; b->x0 = b->x1 - w; }
    else { b->x0 = ox + x; b->x1 = b->x0 + w; }
    b->y0 = oy + y; b->y1 = b->y0 + h;
    b->z0 = oz + z; b->z1 = b->z0 + dz;
    return 1;
}

int player_body_box(const Player *P, Box *b) { return frame_box(P, 1, b); }

int player_attack_box(const Player *P, Box *b)
{
    if (P->ext_box_on) { *b = P->ext_box; return P->hitflags & (uint16_t)~P->consumed & 0x7f; }   /* ENG3GAME */
    if (!frame_box(P, 2, b)) return 0;
    return P->hitflags & (uint16_t)~P->consumed & 0x7f;
}

int player_hit_damage(Player *P, uint16_t mask)     /* FUN_10019b40 */
{
    uint16_t m = mask & P->hitflags;
    uint32_t d = 4u * (uint32_t)(P->difficulty & 3);
    if (m & HIT_POWER) {
        P->consumed |= HIT_POWER;
        int lo = (int)pe_u32(P->pe, TBL_DMG_PUNCH + d), hi = (int)pe_u32(P->pe, TBL_DMG_POWER_MAX + d);
        return P->charge < lo ? lo : P->charge > hi ? hi : P->charge;
    }
    static const struct { uint16_t bit; uint32_t tbl; } K[] = {
        {HIT_CROUCH, TBL_DMG_CROUCH}, {HIT_PUNCH, TBL_DMG_PUNCH}, {HIT_SWORD, TBL_DMG_SWORD},
        {HIT_AIR_SWORD, TBL_DMG_AIR_SWORD}, {HIT_AIR_PUNCH, TBL_DMG_AIR_PUNCH},
    };
    for (size_t i = 0; i < sizeof K / sizeof K[0]; i++)
        if (m & K[i].bit) { P->consumed |= K[i].bit; if (getenv("HIT_LOG")) printf("HIT_LOG player hit mask=%x frame=%d\n", K[i].bit, P->o.frame); return (int)pe_u32(P->pe, K[i].tbl + d); }
    return 0;
}

int player_damage(Player *P, int level, int32_t src_x, int32_t src_y, int32_t src_z, int kind)  /* FUN_100266a0 */
{
    const Obj *o = &P->o;
    if (getenv("HIT_LOG")) printf("HIT_LOG player_damage level=%d from (%d,%d,%d) invuln=%d\n", level, src_x >> 16, src_y >> 16, src_z >> 16, P->invuln);
    if (P->weapon == WPN_INVULNERABLE) return 0;
    if (P->hit_req <= level + 1) {
        P->hit_req = (uint16_t)(level + 1);
        /* FUN_1002ab30 direction code (attacker relative to the player) */
        int32_t dx = src_x - o->x, dy = src_y - o->y, dz = src_z - o->z;
        int code = dx < 0 ? 6 : 4;
        if (dy < 0) code -= 4;
        if (dz < 0) code |= 8;
        if (iabs(dx >> 16) < iabs(dy >> 16)) code += 1;
        P->hit_dir = (uint8_t)code;
        P->hit_kind = kind;
    }
    return P->invuln == 0 && !P->dying;
}

int player_attack_rect(const Player *P, int *x0, int *y0, int *x1, int *y1, int *damage)
{
    Box b;
    int k = player_attack_box(P, &b);
    if (!k) return 0;
    *x0 = b.x0; *y0 = b.y0; *x1 = b.x1; *y1 = b.y1;
    if (damage) {
        uint32_t d = 4u * (uint32_t)(P->difficulty & 3);
        *damage = (k & HIT_POWER) ? (P->charge < 5 ? 5 : P->charge > 15 ? 15 : P->charge)
                                  : (int)pe_u32(P->pe, TBL_DMG_SWORD + d);
    }
    return k;
}

int player_frame_box(const Player *P, int k, Box *b) { return frame_box(P, k, b); }   /* COMBAT: debug */
