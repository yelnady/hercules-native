/* ENG3GAME: ENG3.DLL gameplay runtime (Training Gauntlet, Cyclops chase, Passageways of Torment).
 *
 * Objects keep the original 0x134-byte object layout (as in ENG3's object pools DAT_10042540 / DAT_10043174 /
 * DAT_100431c0) so the per-type handlers can be ported line by line from the decompilation:
 *   +0/+4/+8 depth, y, lateral (16.16; lateral 0 = path centre line), +0x22 bank, +0x2a state/variant,
 *   +0x2e type, +0x30 timer, +0x32 script index, +0x34 frame (bank 0: model index), +0x38 path, +0x3a scale,
 *   +0x4a brightness, +0x50/+0x54 flags, +0x58 spawn entry, +0x64 script (ENG3 VA), +0x70..+0x76 rotation,
 *   +0x94 facing, +0x95 rising, +0x96 lateral facing, +0x9c..+0xa4 previous position, +0xa8.. speeds,
 *   +0xdc.. per-type data, +0x110 animation list (ENG3 VA) / pushing object (player), +0x114 animation,
 *   +0x116 hit kind, +0x11e move enable, +0x120 child list, +0x126 counter. */
#pragma once
#include <stdint.h>
#include <string.h>
#include "level.h"
#include "render.h"
#include "sprite.h"
#include "eng1.h"

#define E3_MAXOBJ 400     /* GAUNTLET: ENG3 keeps separate pools per class; one bigger pool keeps debris from starving actors */
#define E3_OBJSIZE 0x140

typedef struct E3Obj { uint8_t m[E3_OBJSIZE]; int used, spath, sidx, variant, parent, child[8], nchild;
    int fx2_shadow;   /* FX2: shadow child type requested through FUN_1001d600 (0x2002 / 0x2017 / 0x2018), 0 = none */
} E3Obj;

#define OI(o, off)  (*(int32_t *)((o)->m + (off)))
#define OU(o, off)  (*(uint32_t *)((o)->m + (off)))
#define OS(o, off)  (*(int16_t *)((o)->m + (off)))
#define OW(o, off)  (*(uint16_t *)((o)->m + (off)))
#define OC(o, off)  (*(int8_t *)((o)->m + (off)))
#define OB(o, off)  (*(uint8_t *)((o)->m + (off)))
#define ODEP(o) OI(o, 0)
#define OY(o)   OI(o, 4)
#define OLAT(o) OI(o, 8)

typedef struct {
    int32_t d, y, l;            /* spawn position (16.16) */
    uint16_t flags, state;      /* template flags (low byte = variant), +0x8000 live, +0x4000 gone */
    uint16_t type;
} E3Spawn;

typedef struct E3World {
    E3Obj obj[E3_MAXOBJ];       /* obj[0] = Hercules */
    E3Spawn *sp[16]; int nsp[16];
    uint32_t tinfo, thand, tinfo2, thand2, tinfo4, thand4, tmpl, attrflags;
    int level;                  /* 2 Gauntlet, 8 Cyclops, 10 Torment */
    /* player globals (ENG3 names in comments) */
    int state;                  /* DAT_10041cb4 */
    int ending;                 /* DAT_1003faf0: end object reached, slow run */
    int boost;                  /* DAT_1003fb20: speed power-up running */
    int airborne;               /* DAT_1003fb24 */
    int slowzone;               /* DAT_1003fb2c */
    int lock_l, lock_r;         /* DAT_1003fb40 / DAT_1003fb44 */
    int shield_state;           /* DAT_1003fae4 */
    int fb68, fb78;
    int cam, camtarget;         /* DAT_10040640 camera distance, DAT_1003803c target */
    /* ENG3B: camera object (FUN_1001c720 init, FUN_100222c0 follow, FUN_10022070 view) */
    int cd, cy, clat;           /* DAT_100405e0 depth, DAT_100405e4 y, DAT_100405e8 lateral */
    int camyofs;                /* DAT_10040620: 0xe8 - track height (FUN_10018350); GTE offset y = this - 0x8a */
    int camflags;               /* DAT_1004063a */
    int track, track_alt, track_sel;   /* DAT_10041c64 / DAT_10041c68 / which one (attr 0x51 / 0x50) */
    uint32_t track_tab; int ntrack;    /* DAT_10040670 / DAT_10040785 */
    int cam_iter_y;             /* DAT_10040634 byte 2 */
    int shake;                  /* ENG3B: DAT_1003fadc rumble ticks (FUN_10008120 / FUN_1000d3d0) */
    int end_flag[2];            /* DAT_10037b8c.. one-shot end flags */
    int cyc_path;               /* DAT_10041c68: Cyclops arena path */
    int health, maxhealth, lives, coins, coins2, letters, vases, difficulty;
    int dead_timer, done, gameover, cp;
    int sfx_n; struct { int id, sx, own; } sfx[48];     /* AUDSHELL: own = 0 for FUN_1000bc47(id, 0) */
    uint16_t pad, pressed;
    long ticks;
    Face *faces; int nfaces, capfaces;
    /* ENG3.DLL / map */
    const PeImage *dll;
    const Level *L;
    Bank banks[10];
    struct Scrolls *scr;        /* AUDSHELL: letter display (scrolls.c) */
    uint32_t mdl_tab, poly_tab, tex_tab, shapes, attrtab;   /* map tables (FUN_1001af10) */
    int nmodels;
    int endobj;
    int cyclops;                /* DAT_1003fb0c */
    int camshake;               /* CYCLOPS: DAT_100405fc camera y offset (16.16), set by stomps / falling masonry */                 /* index of the end object (type 7), -1 none */
} E3World;

/* e3game.c */
E3Obj *e3_spawn(E3World *W, int type, int32_t d, int32_t y, int32_t l, int path);
void e3_remove(E3World *W, E3Obj *o);
int e3_anim(E3World *W, E3Obj *o);                 /* FUN_1001dbc0 */
void e3_set_anim(E3World *W, E3Obj *o, int n);     /* FUN_1001e2e0 */
int e3_overlap(E3World *W, E3Obj *a, E3Obj *b);    /* FUN_1001eff0 */
int e3_offscreen(E3World *W, E3Obj *o, int margin);/* FUN_1001b920 */
void e3_drop(E3World *W, E3Obj *o);                /* FUN_1001eeb0 */
void e3_hurt(E3World *W, int kind, E3Obj *by);     /* FUN_10019200 */
void e3_win(E3World *W);
E3Obj *e3_child(E3World *W, E3Obj *parent, int32_t d, int32_t y, int32_t l, int type);   /* FUN_1001f410 */
void e3_move(E3Obj *o);                            /* FUN_1001f550 */
void e3_move_lat(E3Obj *o);                        /* FUN_1001f620 */
int e3_cos(int a);                                 /* DAT_100407b4 table, 4096 = 1.0 */
int e3_model_box(E3World *W, E3Obj *o, int16_t b[6]);
void e3_cyclops_attr(E3World *W, int a);                           /* FUN_10019c20 */
void e3_sound(E3World *W, int id, E3Obj *o);       /* FUN_1000bc47 */
void e3_scroll_trigger(E3World *W, int id);        /* AUDSHELL: FUN_10016f30 letter display */
uint32_t e3_rand(int n);                           /* FUN_10001010 */
int e3_floor(E3World *W, int d, int y, int l, int path, int *up);
/* e3types.c: per-type handlers (init / update) */
int e3b_type_init(E3World *W, E3Obj *o);    /* ENG3B: e3types2.c */
int e3b_type_update(E3World *W, E3Obj *o);
/* GAUNTLET: e3_gauntlet.c (Gauntlet types) + exports from e3game.c */
int e3g_type_init(E3World *W, E3Obj *o);
int e3g_type_update(E3World *W, E3Obj *o);
void e3g_pilot(E3World *W, int *target, int *jump, int *slow);
extern int e3g_speech;
int e3_landed(E3World *W, E3Obj *o);                               /* FUN_1001eaf0 */
int e3_attr_at(E3World *W, int d, int y, int l, int path);         /* FUN_1001b4a0 */
int e3_type_init(E3World *W, E3Obj *o);
/* TORMENT: e3_torment.c (Torment types) and e3game.c exports */
int e3t_type_init(E3World *W, E3Obj *o);
int e3t_type_update(E3World *W, E3Obj *o);
void e3t_attr(E3World *W, int a);
int e3_attr_at(E3World *W, int d, int y, int l, int path);   /* FUN_1001b4a0 */
void e3_kill(E3World *W);                                    /* FUN_10019a30 */
void e3_type_update(E3World *W, E3Obj *o);
