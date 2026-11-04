/* Level objects: enemies, townsfolk, pickups, checkpoints. A port of ENG1.DLL's object system.
 *
 * Spawn data lives in the map (MAP_PSX.BIN): each layer record (map header +0x30 table) holds
 * u16 count at +8 and an s16 self-relative offset at +10 to 8-byte records {u16 x, y, z, template}.
 * The template index selects a 10-byte entry {s16 dx, dy, dz, u16 flags, u16 type} from the per-level
 * table at ENG1 0x10068af0 (+0x40). type & 0x7000 picks the pool: 0 = actors (0x134-byte objects),
 * 0x4000 = 0xec-byte objects, 0x2000 = 0x94-byte objects (pickups, effects), 0x1000 = trigger.
 * type & 0x1ff indexes the pool's type-info and handler tables (same level entry). See
 * analysis/objects_notes.md for the addresses. */
#pragma once
#include <stdint.h>
#include "fs.h"
#include "eng1.h"
#include "level.h"
#include "sprite.h"
#include "render.h"
#include "player.h"

#define OBJ_MAX 160
#define OBJ_BANKS 96

typedef struct {
    int32_t x, y, z;            /* 16.16 */
    uint16_t flags, type;
    uint8_t state;              /* 0 dormant, 1 live, 2 gone for good (collected) */
} SpawnRec;

typedef struct Object {
    int used, kind;
    uint16_t type;
    int spawn;                  /* index into Objects.sp, -1 for spawned effects */
    int32_t x, y, z, px, py, pz;
    uint8_t bank;
    uint32_t script;            /* ENG1 VA (obj+0x64) */
    int pairs;                  /* script format: 1 = {s16 op, s16 arg} (FUN_100283e0), 0 = s16 words (FUN_10028820) */
    int16_t timer, idx, frame;  /* +0x30, +0x32, +0x34 */
    uint8_t facing, zface;      /* +0x94, +0x96 */
    uint32_t flags50, flags54;
    uint32_t anims, movetab;    /* +0x110 animation list, +0xf0 movement table */
    int16_t anim;               /* +0x114 */
    int movei;                  /* +0xf4 */
    int16_t moving;             /* +0x11e (>= 0: apply the movement table) */
    uint16_t v2a;               /* +0x2a, set by script op -0x29 */
    int32_t ztarget;            /* +0xe0 */
    int airborne;               /* +0x25 bit 0 */
    int attr;                   /* solid-object collision attribute (+0xd8), -1 = not solid */
    int life;
    int16_t cbid, count126;     /* +0x124 script callback id, +0x126 AI counter */
    /* ENEMIES: combat fields */
    int ai;                     /* ENEMIES: AI_* behaviour (enemies.c), 0 = none */
    int16_t hp;                 /* +0xee hit points */
    uint8_t inv;                /* +0xec hit-invulnerability countdown */
    uint8_t tag;                /* +0x97 group tag / drop id */
    int cool;                   /* +0x4c sound cooldown */
    int variant;                /* per-AI variant */
    int32_t vx, vy;             /* debris / projectile velocity (16.16) */
    /* SOLIDS: model / group fields (0x134-byte object layout offsets in the comments) */
    int16_t rot[3];             /* +0x70/+0x72/+0x74 model rotation, 0x400 = full turn */
    uint16_t scale;             /* +0x3a, 0x100 = 1.0 */
    uint32_t list;              /* +0x118 group child list / per-type table (type info +8) */
    struct Object *parent;      /* +0x5c */
    struct Object *kid[24]; int nkids;   /* +0x120 children (FUN_1002b130) */
    int32_t d[8];               /* +0xd8..+0xf4 per-type words */
    int32_t v[12];              /* +0xa8..+0xd4: vx, vxmax, xacc, xdec, vy, vymax, yacc, ydec, vz, vzmax, zacc, zdec */
    int16_t box88[6]; int has88;/* +0x88 explicit collision box {x0, y0, z0, w, h, d} */
    int32_t home[3];            /* +0x9c..+0xa4 previous / home position */
    int16_t s98, s9a;           /* +0x98, +0x9a */
    uint8_t b21, b23, b95;      /* +0x21 station index, +0x23 vertex transform, +0x95 */
    uint32_t init_fn, upd_fn;   /* ENG1 init / update handler addresses */
    int carried;                /* held by the player */
    int kidx;                   /* index in the parent's group list */
    int16_t bright;             /* +0x4a brightness / fade, 0x80 = normal */
    int16_t child;              /* overlay sprite frame drawn with the object (+0x60 child), 0 = none */
    int16_t child_bright;
    uint8_t semi;               /* drawn semi-transparent (flags54 0x400 style), additive */
    uint8_t child_semi;
    int32_t w[12];              /* per-boss scratch (named in bosses.c) */
    /* BOSSES: boss-object fields (bosses.c) */
    int16_t s36;                /* ENEMIES3: +0x36 (script op -0x18), Phil's board frame */
    int16_t morph_model;        /* FX2: +0x36 morph target model (vertex transforms 3 / 6) */
    int morph_w;                /* FX2: +0x4c morph weight, 0x100 = all target */
    int route;                  /* ENEMIES3: +0x11c Phil's route index */
    uint8_t palx;               /* LEFTOVERS: +0x1c palette override + 1 (0 = the frame's own, i.e. 0xff) */
    uint32_t mesh; int16_t mesh_n;   /* FX3: draw mode 4 sprite mesh (+0x5c records, +0x36 count; FUN_10026700) */
    uint8_t hidden;             /* LEFTOVERS: flags54 bit 31 clear in the original's "shown" sense: not drawn */
    struct Object *carrier, *victim;   /* LEFTOVERS: harpy food +0xe4 (who carries it) / harpy +0xe8 (what it carries) */
} Object;

typedef struct {
    const Fs *fs;
    const PeImage *pe;
    const Level *L;
    uint32_t ti_actor, h_actor, ti_4000, h_4000, ti_2000, h_2000, tmpl;
    SpawnRec *sp; int nsp;
    int level_end;              /* LEVELS: Hercules reached the end-of-level object (pickup 0x200a, FUN_10013880) */
    Object obj[OBJ_MAX];
    Bank banks[OBJ_BANKS];
    uint8_t bank_tried[OBJ_BANKS];
    uint32_t rng;
    /* Game counters the pickups feed (DAT_10070f24 offsets in the original). */
    int coins18, coins14;       /* +0x18 (type 0x2018) and +0x14 (0x2019), max 1000 */
    int health, health_max;     /* +0x24 / +0x1e */
    int lives;                  /* +0x0c */
    int letters;                /* +0x11 bits (0x201a..0x201d) */
    /* SOLIDS: end-of-level tally (gamestruct offsets) */
    uint16_t scroll_bits;       /* +0x1c: story scrolls collected, bit = scroll id (0x200b +0x2a) */
    int scroll_last, scroll_show;   /* DAT_10072104 id of the last scroll / DAT_10072174 "show the story" request */
    int coins18_total, coins14_total;   /* +0x1a / +0x16: coins of each kind placed in the level */
    int checkpoint;             /* +0x02 */
    int have_cp; int32_t cp_x, cp_y, cp_z;
    int sounds[64], nsounds;    /* sound ids requested this tick */
    int sound_x[64];            /* ENEMIES: path x of the sound source (vertex units) */
    int sound_owner[64];        /* AUDSHELL: emitting object slot + 2; 1 = position only; 0 = no object (FUN_10006da0 param_2 == 0); -2 = stop that owner (sound_x) */
    int health_snd;             /* AUDSHELL: DAT_1007215c, 50-tick cooldown of the health pickup jingle */
    struct Scrolls *scr;        /* AUDSHELL: letter display (scrolls.c) */
    struct Object *cur;         /* AUDSHELL: object whose update is running (owner of obj_sound calls made at its own x) */
    Player *P;                  /* ENEMIES: player during objects_tick */
    int camx;                   /* ENEMIES: camera path x during objects_tick */
    int kills;                  /* ENEMIES: enemies defeated */
    uint16_t spawn_flags;       /* ENEMIES: template flags of the record being spawned */
    int16_t pending_v2a; int have_pending;
    struct Object *hit_obj;     /* SOLIDS: DAT_1006fedc, the solid object found by the last attribute test */   /* SOLIDS: +0x2a preset by a group list (FUN_1002b130) */
    /* ENEMIES2: mini-boss bar (ENG1 DAT_10072188 / DAT_10072184 / DAT_10070832) - main.c copies hud_boss_*
     * into HudState.boss_cmd / boss_damage each tick and clears them. */
    int hud_boss_cmd, hud_boss_damage;
    int mb_active, mb_bar;      /* DAT_10072188 != 0, bar value (0xff full) */
    struct Object *mino;        /* DAT_10071e28 */
    struct Object *mino_rock;   /* the rock it holds (+0x130 link) */
    int mino_seq, mino_drop;    /* DAT_10071e24 pattern index, DAT_1006ffe4 rock drop counter */
    uint16_t mino_v2a;          /* DAT_1006ffd8 + 2 */
    int harpy_slot;             /* DAT_100721ac */
    /* ENEMIES2: Training (Phil's lessons) */
    int t_lesson[16];           /* per-station state (station objects' +0x2a) */
    int t_hits;                 /* DAT_10070ef0: times Hercules was hit during a lesson */
    /* BOSSES: boss fights (bosses.c) */
    int boss_hp, boss_hp_max;   /* boss health (0/0 = no boss) */
    /* LEFTOVERS: camera shake list DAT_10071680 (count + 16.16 y offsets, rebuilt every tick) and the camera
     * height bias DAT_10070d04 (Thebes 0x202d zones), both read by game.c's camera */
    int shake_n; int32_t shake_sum;
    int cam_bias, cam_bias_set;
    int cam_dy;                 /* LEFTOVERS: direct camera y offset this tick (Medusa's quake, DAT_10070cbc) */
    int cam_y;                  /* FX2: camera path y during objects_tick */
    int cam_zoff;               /* FX2: DAT_10071848 camera depth offset request (Hades); smoothed into G->cam_dz */
    int cam_xmax;               /* LEFTOVERS: DAT_10070ce8, right-edge camera lock (0 = none), set by the Nessus arena */
} Objects;

int objects_init(Objects *O, const Fs *fs, const PeImage *pe, const Level *L);
void objects_free(Objects *O);
void objects_tick(Objects *O, Player *P, float camx);
void objects_draw(Objects *O, Renderer *R);
/* Player hook (FUN_1002a170): collision attribute of a solid object at the point, or -1. */
int objects_attr(void *ctx, int x, int y, int z);
/* SOLIDS: platform floors under / above a point (FUN_100296e0); 0x7fff = none. */
int objects_floor(void *ctx, int x, int y, int z, int *up, int *down);
