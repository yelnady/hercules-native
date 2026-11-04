/* In-game HUD and pause text, ported from ENG1.DLL FUN_10032bd0 (status panel), FUN_10012040 (gift orbs,
 * charge meters, boss bar state machine) and FUN_10001f40 / FUN_10002450 (pause / quit text). All art comes
 * from LANGUAGE\SRCLANG\STPANEL1.DAT, a 256x512 8-bit texture page the PC build loads at VRAM (0,512)
 * (FUN_10032b10), drawn through the level palette. */
#pragma once
#include "fs.h"
#include "eng1.h"
#include "level.h"
#include "render.h"

/* Boss bar commands (ENG1 DAT_10072188 transitions requested by boss objects). */
enum { HUD_BOSS_NONE = 0, HUD_BOSS_START = 2, HUD_BOSS_START_FADE = 5, HUD_BOSS_END = 7 };

/* Filled in by gameplay. Units are ENG1's (gamestruct fields in comments).
 * POLISH: everything after gift_b is new; zero means "not present", so old callers keep working. */
typedef struct {
    int health, max_health;     /* +0x24 / +0x1e: bar length = max*0x92/256 px; max_health 0 = default full bar */
    int lives, coins;           /* +0x0c lives (shown on the left orb), coins (not drawn: the original has no counter) */
    int gift_a, gift_b;         /* legacy: fixed orb icons when > 0 (debug / screenshots) */
    int life_pickup;            /* DAT_100721a0: set for one tick when a life is collected (left orb spins to the count) */
    int weapon;                 /* DAT_1006efb4: selected Gift of the Gods 0..4 (right orb icon, table 0x10055310) */
    int weapon_ammo;            /* gamestruct +0x24+2*weapon: weapon charge meter (0..0xff) */
    int charge;                 /* DAT_10072194: power-punch charge (0..0xff), shown by the right-hand meter */
    int boss_cmd;               /* one-shot HUD_BOSS_* request (DAT_10072188 = 2 / 5 / 7) */
    int boss_damage;            /* DAT_10072184: damage dealt to the boss this tick (bar target drops by it) */
    int timer_ticks;            /* gamestruct+6 == 3 (time-trial cheat): remaining ticks, -1 = off */
} HudState;

typedef struct {
    const uint8_t *page;        /* STPANEL1.DAT, 256 x 512 */
    uint8_t icon[200][2];       /* ENG1 0x1005dd80: orb icon index -> (col, row) in the 24x24 grid at v 0x38 */
    int nicon;
    int16_t life_tbl[12];       /* ENG1 0x10055330: left-orb icon per lives count */
    int16_t wpn_tbl[6];         /* ENG1 0x10055310: right-orb icon per weapon */
    int shown_max, shown_hp;    /* +0x20 / +0x22, animated toward the targets */
    int orb_a, orb_b;           /* DAT_10070820 lo / hi: current orb icon index */
    int orb_a_target, orb_b_target, orb_a_up, orb_b_up;     /* DAT_1007219c / 1007219e, DAT_100721a4 / 100721a2 */
    int orb_timer, orb_tick;    /* DAT_100721a6, DAT_1006ef50 */
    int meter_wpn, meter_pow;   /* DAT_1007082c lo / hi */
    int boss_state, boss_x, boss_y, boss_shown, boss_target, boss_fade, boss_mode, boss_wait;  /* 0x10072188.. */
    int weapon_prev;
} Hud;

/* pause menu states (gamestruct+4 values in FUN_10001f40) */
enum { PAUSE_CONTINUE = 2, PAUSE_QUIT = 3, PAUSE_SURE_NO = 4, PAUSE_SURE_YES = 5 };

int hud_init(Hud *H, const Fs *fs);
void hud_set_eng1(Hud *H, const PeImage *eng1);   /* POLISH: icon / lives / weapon tables from ENG1.DLL */
void hud_reset(Hud *H);                           /* POLISH: call on level start (orbs and boss bar reset) */
void hud_tick(Hud *H, const HudState *s);
void hud_draw(Hud *H, Renderer *R, const Level *L, const HudState *s);
void hud_draw_pause(Hud *H, Renderer *R, const Level *L, int pause_state);
