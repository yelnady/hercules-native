/* Front end: a port of TITLE.DLL's startup flow (process FUN_100164b0), main menu (FUN_10007510 with
 * FUN_10006bb0/6fb0/71e0/8140/8320), password screen (FUN_1000f460 + object handler FUN_1000d910),
 * pre-level cards (FUN_10014300 family), plus the PC build's Properties sheet (HERCULES.EXE
 * COptionsSheet: keys / gamepad / sound / game play) and the in-game pause.
 * Graphics come from HERCULES.FS (GRAFIX\MOVIE\SCREENS\*.TGA, LANGUAGE\GRAFIX\CHOP\TITLE\*\ANIMPSX.BIN),
 * animation/position tables are read from TITLE.DLL at runtime, videos are the CD's DATA\<n>.ETV. */
#pragma once
#include <stdint.h>
#include "fs.h"
#include "eng1.h"
#include "sprite.h"
#include "render.h"
#include "video.h"
#include "input.h"

typedef enum { FE_VIDEO, FE_MENU, FE_PASSWORD, FE_CARD, FE_PROPS, FE_PLAY, FE_PAUSE, FE_QUIT,
               FE_TALLY, FE_CREDITS, FE_CHEATS } FeMode;     /* POLISH: tally / credits / cheats screens */

/* POLISH: how a level ended (ENG1 gamestruct+3 exit codes as TITLE.DLL's process FUN_100164b0 sees them). */
enum { FE_RES_DONE = 0x31, FE_RES_GAMEOVER = 0x22, FE_RES_QUIT = 0x13 };

/* POLISH: TITLE.DLL's view of the gamestruct (DAT_1002bb44) that survives between levels. */
typedef struct {
    int level;              /* [0]  TITLE level number (1 training .. 11 vortex, 12 movies) */
    int checkpoint;         /* [1]  start checkpoint (demos) */
    int demo;               /* [6]  1 = attract demo, 3 = time-trial cheat */
    int demo_idx, demo_var; /* [7] demo table index, [8] DEMO*.DAT index */
    int start_lives;        /* [0xb] */
    int lives;              /* [0xc] */
    int deaths;             /* [0xd] deaths this level */
    int difficulty;         /* [0xe] */
    int continues;          /* [0x10] */
    int vases;              /* [0x11] mystery password vases found (bits 0..3) */
    int coins14, coins16, coins18, coins1a;   /* [0x14..0x1a] coins collected / totals */
    int scrolls;            /* [0x1c] HERCULES story scrolls (bits 0..7) */
    uint32_t cheats;        /* [0x30] 8 = keep lives, 0x10 = continue, 0x800000 = vases, bit31 = cheat menu */
    int cheat_mode;         /* [0x2e] 0 = full cheat screen, 1 = level select only */
} FeGame;

/* POLISH: statistics handed over by the game when a level ends. */
typedef struct { int lives, deaths, coins14, coins16, coins18, coins1a, vases, scrolls; } FeStats;

typedef struct { int used, x, y, z, frame, bright, depth, rot, seq; uint32_t flags; Bank *bank; } FeObj;

typedef struct { const Image *img; int x, y, bright, mode, mirror, rot; } FeSpr;

typedef struct {
    int active, frame, x, y, bright, rot;
} FeSparkle;

typedef struct FrontEnd {
    const Fs *fs;
    PeImage title;
    char fsdir[1024];
    InputConfig *cfg;
    FeMode mode, props_return;
    uint16_t held, pressed;
    uint32_t rng;
    long tick;
    uint32_t cv[320 * 256];

    /* video queue */
    Video vid; int vopen;
    int vq[8], vqn, vqi;
    FeMode after_video;
    uint32_t vframe[320 * 240];
    int vhave, vphase;

    /* images (320x256 ARGB) */
    uint32_t *menub, *slot_a, *slot_b, *pswd_bg, *lev_bg, *card_bg;
    Bank menu1, pswd, seq;
    int seq_loaded;

    /* main menu (TITLE.DLL globals in comments) */
    int ms;                 /* process state param_1[1] */
    int entered;            /* DAT_1002a08c */
    int a07c, a01c, a018, a020, a080, a084, a088, a038, a040, a030, ff0, a014;
    int a044, a03c;         /* scene offset carried by Hercules */
    int cc64, cc60;
    int hx, hy, hframe, hvisible;
    int muse_idx[5], muse_frame[5];
    FeSparkle spk[48];

    /* password */
    int ps, pa28, pfacing, pcur, pidle, phit, pdir, ptimer, pbright, presult, pfade;
    int vase_frame[4], vase_target[4];

    /* pre-level card */
    int clevel, cs, cphase, cidx, ctoggle, cfade, ctext_b, cmuse_frame;

    /* properties dialog */
    int ptab, prow, pwait;

    /* outputs */
    int start_level;        /* ENG level number requested (FE_PLAY), 0 = none */
    int difficulty;
    int dev;

    /* POLISH: game flow (FUN_100164b0), shared object list, tally / credits / cheats / attract */
    FeGame gs;
    int flow, acec;         /* pending main-process state after the queued videos; DAT_1002acec */
    int vskip_ends, vcross; /* skipping the current video ends the queue; Cross pressed during video 22 */
    int title_sounds;       /* the OPTIONS bank is loaded */
    FeObj obj[192]; int objseq;
    struct FeTally *T;
    Bank tally, cheatbank;
    uint32_t *tally_bg, *cheat_bg, *credit_bg;
    int cr_idx, cr_count, cr_fade, cr_state, cr_fadedir;
    int ch_state, ch_fade, ch_idx;
    const uint16_t *demo_pad; int demo_len, demo_pos;   /* attract playback (M:\DEMO\DEMOn_m.DAT words) */
    uint8_t *demo_buf;
    int speech_t, speech_len, speech_n;   /* current speech clip (lip sync) */
    int vq2[8], vq2n;       /* second video list (state 0xfa) */
    int (*level_ok)(int level);   /* set by main.c: 1 if this engine can run the TITLE level number */
    int save_code;          /* last saved game (level << 16 | difficulty << 8 | lives), 0 = none */
    /* password screen (FUN_1000d910 Meg object) */
    int mstate, msub, midx, mcount, mx, mmirror, mframe, mret, mhitdone, mcode;
} FrontEnd;

int fe_init(FrontEnd *F, const Fs *fs, const char *fsdir, InputConfig *cfg);
void fe_free(FrontEnd *F);
void fe_boot(FrontEnd *F);                  /* logos -> intro -> title/menu */
void fe_goto_menu(FrontEnd *F);
void fe_open_props(FrontEnd *F, FeMode ret);
void fe_card(FrontEnd *F, int level);        /* pre-level card (level 1 plays the story video first) */
void fe_tick(FrontEnd *F, uint16_t pad);    /* 30 Hz */
void fe_key(FrontEnd *F, SDL_Scancode sc);  /* raw key presses (rebinding in Properties) */
void fe_pad_button(FrontEnd *F, SDL_GameControllerButton b);
void fe_draw(FrontEnd *F, Renderer *R);     /* full-screen front end */
void fe_draw_help(FrontEnd *F, Renderer *R);       /* F1 controls page */

/* ---- POLISH: game flow API (port of TITLE.DLL FUN_100164b0 states 0x29 / 0xd2 / 0xd3 / 0x8c.. / 200 / 0xdc) ----
 * The front end owns the run (level order, lives, continues, difficulty); main.c reports how each level ended.
 *   fe_level_end: call once when a level finishes (FE_RES_*); stats = what the game collected.
 *   fe_wants_demo: after fe_tick, non-zero when an attract demo should start (F->start_level is the level,
 *                  F->gs.checkpoint the start checkpoint); then feed fe_demo_pad() each game tick.
 *   fe_demo_pad: next recorded PSX pad word, or -1 when the demo is over (end it with FE_RES_QUIT). */
void fe_level_end(FrontEnd *F, int result, const FeStats *stats);
int fe_wants_demo(const FrontEnd *F);
int fe_demo_pad(FrontEnd *F);
void fe_load_game(FrontEnd *F);     /* F3 on the main menu: continue the saved game */
void fe_save_load(FrontEnd *F);     /* read the saved game code from disk (called by fe_init) */
