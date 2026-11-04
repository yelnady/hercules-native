/* TITANS2: Battle of the Titans (level 9) actors, ported from ENG1.DLL (level-9 handler table 0x10061f80).
 * Background Titans and their attacks, harpy waves, the spy, the gods, ice / lava / rock / tornado
 * projectiles. See analysis/levels_notes.md ("Titans: actors"). */
#pragma once
#include "objects.h"

/* Level-9 globals the actors share with the Pegasus code (titans.c fills them every tick). */
typedef struct {
    int32_t ax, ay, az;         /* scroll anchor actor 0x36 (DAT_100707e4) position, 16.16 */
    int32_t *speed;             /* DAT_100707e8: scroll speed (the spy slows it down) */
    int *zpush;                 /* DAT_100707f8: anchor z drift requested by the 0x46..0x4b markers */
    int32_t px, py, pz;         /* Pegasus (DAT_10072860) */
    int pstate;                 /* Pegasus state (+0x2a): 0x23 dying, 0x24 hurt */
    int pbody[6], have_pbody;   /* Pegasus body box (+0x88) */
    int sword[6], sword_on;     /* DAT_10071440 / DAT_1006fec4: Pegasus' live attack box */
    float psx, psy;             /* Pegasus screen position (+0xc / +0xe), 320x240 units */
    int camx, camy;             /* camera (DAT_10070ca0) */
    long frame;                 /* gamestruct +0x38 frame counter */
    /* level state */
    int titan_cam;              /* DAT_100707f4: camera x when the current background Titan appeared (0 = none) */
    int shake_i; int32_t shake; /* DAT_1006ffec / DAT_10070cbc: rock-impact camera shake */
    int wave;                   /* DAT_1006fff4: next harpy wave list */
    int form;                   /* DAT_1006fff8: formation variant counter */
    int drop;                   /* DAT_100655a4: formation reward index */
    int spiral;                 /* DAT_1006ffe8 */
    int tornado_lock;           /* DAT_10064474 */
    const Bank *peg_bank;       /* TITANS3: Pegasus' bank = the level-9 sprite palette */
} TitansCtx;

TitansCtx *titans_ai_ctx(void);
void titans_ai_level_start(int checkpoint);              /* FUN_1003c170 */
int titans_ai_init(Objects *O, Object *o, uint32_t init); /* 1 if the init handler is one of ours */
void titans_ai_tick(Objects *O, Object *o);
void titans_ai_draw(Objects *O, Renderer *R, Object *o);
