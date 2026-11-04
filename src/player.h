/* Hercules player: a port of ENG1.DLL's player controller (FUN_10015780 and the state handlers in the
 * table at DAT_10072040), its animation-script interpreter (FUN_10015d00) and the movement physics
 * (FUN_10019a10, FUN_10019d70, FUN_100174e0, FUN_10029c20/29da0/29e20), plus combat: sword / punch /
 * charged punch / crouch and air attacks, ground pound, hurt reactions, invulnerability flicker and death
 * (see analysis/combat_notes.md). Positions are 16.16 fixed point in vertex units, as in the original. */
#pragma once
#include <stdint.h>
#include "eng1.h"
#include "level.h"
#include "sprite.h"

/* PSX pad bits as ENG1 sees them (low byte directions, high byte buttons). */
enum {
    PAD_UP = 0x10, PAD_RIGHT = 0x20, PAD_DOWN = 0x40, PAD_LEFT = 0x80,
    PAD_TRIANGLE = 0x1000, PAD_CIRCLE = 0x2000, PAD_CROSS = 0x4000, PAD_SQUARE = 0x8000,
    PAD_L2 = 0x0100, PAD_R2 = 0x0200, PAD_L1 = 0x0400, PAD_R1 = 0x0800,
};
/* ENG1's default button masks (DAT_1004d958 copied to gamestruct +0x6c..+0x76): sword = SQUARE (0x8000),
 * jump = CROSS (0x4000), punch = CIRCLE (0x2000), power punch = TRIANGLE (0x1000), previous weapon = L1
 * (0x0400), next weapon = R1 (0x0800). */

/* Attack kinds (DAT_10072012 "hitflags", set by animation script ops 0x0a / 0x09 while a blow is live). */
enum {
    HIT_CROUCH = 0x01,      /* crouching punch (state 0xe) */
    HIT_POWER = 0x02,       /* released charged punch (state 0x2a); damage = charge clamped */
    HIT_PUNCH = 0x04,       /* standing punch combo (state 0x29) */
    HIT_SWORD = 0x08,       /* sword swing (state 0xc) */
    HIT_AIR_SWORD = 0x10,   /* sword swing in the air */
    HIT_STOMP = 0x20,       /* ground-pound landing (state 0x12) */
    HIT_AIR_PUNCH = 0x40,   /* air punch / crouch-attack follow-up uppercut */
};

/* World-space box in vertex units (x along the path, y down, z depth). */
typedef struct { int x0, y0, z0, x1, y1, z1; } Box;

/* Effect / projectile object the player's scripts ask the object system to create (script ops 0x22 / 0x23,
 * FUN_10030180 / FUN_100300c0). id = ENG1 object type (e.g. 0x4005 lightning bolt). Positions 16.16. */
typedef struct { int id; int32_t x, y, z; int facing; int aim; } Spawn;   /* FXW: aim = DAT_1006ef78, the fire-direction table row at spawn time */

/* Gifts of the Gods (DAT_1006efb4). */
enum { WPN_NONE, WPN_FIREBALL, WPN_LIGHTNING, WPN_SONIC, WPN_INVULNERABLE };

typedef struct {
    int32_t x, y, z, px, py, pz;
    int16_t timer, idx, frame;
    uint8_t bank;
    uint32_t script;            /* ENG1 virtual address of the running animation script */
    uint8_t facing;             /* 0 = right, 0xff = left */
    uint8_t rising;
    uint32_t flags50, flags54;  /* 0x54 bit 4 = drawn mirrored, bit 23 = sword-trail overlay visible */
    int32_t vx, vx_target, vx_acc, vx_dec;
    int32_t vy, vy_max, vy_acc, vy_dec;
    int32_t air_speed;
    int idle_count;
} Obj;

typedef struct {
    Obj o;
    int state;
    int start_hold;             /* PLAYER: state 0xa countdown (obj+0x108) */
    int jump_kind;              /* DAT_1006ef70 */
    int air_attack;             /* DAT_1006ef7c: combo / air-attack window (script ops 0x10 / 0x0f) */
    uint16_t hitflags;          /* DAT_10072012, HIT_* */
    uint16_t consumed;          /* DAT_10072168: hit kinds already used this tick */
    uint16_t pad, pressed;
    uint16_t m_sword, m_jump, m_punch, m_power;     /* button masks (+0x6c, +0x6e, +0x70, +0x72) */
    const PeImage *pe;
    const Level *L;
    Bank *banks;                /* sprite banks indexed by ENG1 bank id (for per-frame collision boxes) */
    uint32_t rng;

    /* combat state */
    int health;                 /* gamestruct+0x24, starts at 128 */
    int lives;                  /* gamestruct+0xc */
    int max_health;             /* gamestruct+0x1e: bar length / pickup cap (0x80, +0x20 per 0x2002 up to 0xff) */
    int heal_cd;                /* DAT_1007215c: health pickup sound cooldown */
    int difficulty;             /* gamestruct+0xe: 0 easy .. 2 hard */
    int invuln;                 /* obj+0xec, hit invulnerability countdown */
    int flash_idx;              /* DAT_1006ef94, index into the hurt fade table */
    int blend_mode, blend_level;/* sprite semi-transparency while flashing (mode -1 = opaque) */
    uint16_t hit_req;           /* DAT_1006feb4: pending damage level (+1), | 0x10 no reaction, | 0x20 strong */
    uint8_t hit_dir;            /* DAT_1006feb8: direction code from FUN_1002ab30 */
    int hit_kind;               /* DAT_1006febc */
    int react_lock;             /* DAT_1006ef84: hurt animation can't be cancelled yet */
    int sword_lock;             /* DAT_1007211c: sword button must be released before the next swing */
    int charge, charge_full;    /* DAT_10072194 / DAT_1007216c: charged-punch meter 0..255 */
    int crouch_lock;            /* DAT_1006ef80 */
    int child_frame, child_ticks, child_trans;  /* sword-trail overlay (child object at obj+0x60) */
    int shake, shake_y;         /* DAT_1006ef90 / DAT_100717b4: screen shake after a ground pound */
    int dying;                  /* DAT_10072110: health ran out, death animation pending/playing */
    int game_over;              /* no lives left when the death started */
    int dead;                   /* death finished (script op 0x29 or fell out of the level): respawn */
    int cheat_lives;            /* PLAYER: gamestruct+0x30 bit 8 (cheat screen, Up): dying costs no life */
    int cheats;                 /* gamestruct+0x30: 1 no death, 2 no reactions, 4 no damage */

    /* weapons (Gifts of the Gods): hold sword to fire the selected one */
    uint16_t m_prev, m_next;    /* weapon select masks (+0x74 / +0x76) */
    int weapon;                 /* WPN_* selected */
    int ammo[5];                /* gamestruct+0x24+2*i: charges per weapon (index 0 unused) */
    int weapon_timer[5];        /* DAT_10072140: ammo is spent when this wraps */
    int inv_tick;               /* DAT_1006ef64: invulnerability flicker counter */
    int aim_idx, aim_target;    /* DAT_1006ef78 / DAT_1006ef74: fire direction (table index / wanted angle) */
    int aim_anim, aim_tick;     /* DAT_1007215a / DAT_10072024 */
    int dyn_frame;              /* DAT_1007216a: frame shown by script op 0x18 while aiming */
    struct { int active; int32_t x, y, z; int angle; } flame;  /* fireball stream (object type 0x4001) */
    int sonic_active;           /* sonic-sword objects (2 x type 0x4006, script ops 0x0c / 0x0b) */
    Spawn spawns[4]; int nspawns;   /* effect objects requested this tick */

    /* pole swinging (state 9): poles registered by the object system, 16.16 positions */
    const int32_t (*poles)[3]; int npoles;

    /* carrying (states 0x14 lift / 0x2f throw): set touch_* each tick for the carryable object Hercules is
     * pressing against (DAT_1007210c; type = object type 2, 4 or 5). carrying = type held (DAT_1007201c),
     * carry_pose = DAT_1006ef98 (the held object follows the player's pose table with this index) */
    void *touch_obj; int touch_type;
    void *carried; int carrying, carry_pose, carry_ang;   /* carry_ang: DAT_1006ef9c (turn swing, added to the held object's rot[1] >> 1) */
    int throw_event;            /* set on the tick a throw starts (1 forward, 2 upward) */

    int won;                    /* DAT_10072170: victory pose requested (player_win) */
    /* NESSUS: Nessus offering his back (DAT_10072120: 1 offered, 2 mounted), his box 3 and position */
    int nes_offer, nes_box_ok, nes_box[6];
    int32_t nes_x, nes_y;
    int signal;
    int fade_req;               /* PLAYER: script op 0x2a: start the fade out (DAT_1007286c = 0xf8) */
    int ext_box_on; Box ext_box;        /* ENG3GAME: attack box supplied by another controller (Pegasus sword) */                 /* last level-flow script op: 0x01 / 0x2a fade / 0x2b level done / 0x29 */
    int sounds[8], nsounds;     /* sound ids requested this tick (negative = stop that id) */
    int wpn_snd;                /* AUDSHELL: DAT_1006ef5c, ticks until the invulnerability gift announces itself (sound 0x14) */
    int step_state, water_y;    /* SOUND: DAT_1006efa8 footstep splash counter, DAT_100718e0 water line */
    /* OBJECTS: depth-plane switching (ENG1 DAT_10071840 / DAT_10071bc4 / DAT_10071c5c, obj+0x96) */
    int zone, zmin, zmax;
    uint8_t zface;              /* 0xff = walking into the screen (+z) */
    int zstep;
    /* OBJECTS: optional extra attribute source (solid level objects); returns attr id or -1 */
    int (*obj_attr)(void *ctx, int x, int y, int z);
    /* SOLIDS: platform objects (FUN_100296e0): distance up / down to the nearest object top, 0x7fff = none */
    int (*obj_floor)(void *ctx, int x, int y, int z, int *up, int *down);
    void *obj_ctx;
} Player;

void player_init(Player *P, const PeImage *pe, const Level *L, int x, int y, int z);
void player_tick(Player *P, uint16_t pad);

/* ---- combat API for enemies / objects ----
 *
 * player_body_box: the player's body box (frame collision box 1, what enemies touch).
 * player_attack_box: the live attack box (frame collision box 2) if the current frame has one; returns the
 *   active HIT_* kinds (0 = the box exists but no blow is live, or no box). An enemy is hit when its own
 *   box overlaps this one and the return value is non-zero (ENG1 FUN_10026de0 / FUN_1000a010).
 * player_hit_damage: port of FUN_10019b40. Called by an enemy that was hit, with the HIT_* kinds it reacts
 *   to; returns the damage to apply (0 if none of them are live) and marks the kind as used, so one blow
 *   deals damage once. Charged punches return the charge (clamped to the difficulty's min/max).
 * player_damage: port of FUN_100266a0. An enemy touching / striking the player requests damage `level`
 *   (0..7, optionally | 0x10 for no hit reaction, | 0x20 for a strong knock-back); the highest request in
 *   a tick wins and is applied by player_tick (FUN_10018c50) unless the player is invulnerable.
 *   src_* = attacker position in 16.16. Returns 0 while the player can't be hurt. */
int player_body_box(const Player *P, Box *b);
int player_frame_box(const Player *P, int k, Box *b);   /* COMBAT: debug, any box index */
int player_attack_box(const Player *P, Box *b);
int player_hit_damage(Player *P, uint16_t mask);
int player_damage(Player *P, int level, int32_t src_x, int32_t src_y, int32_t src_z, int kind);
/* Victory pose (HC_DUNIT) at the end of a level; the script signals level completion via P->signal. */
void player_win(Player *P);
/* NESSUS: FUN_1000bb90 - Nessus bucks Hercules off: place him, face him, give him vx and start a fall. */
void player_nessus_throw(Player *P, int32_t x, int32_t y, uint8_t facing, int32_t vx);
/* Convenience wrapper matching the simple interface: 2D attack rectangle + damage if a blow is live. */
int player_attack_rect(const Player *P, int *x0, int *y0, int *x1, int *y1, int *damage);
