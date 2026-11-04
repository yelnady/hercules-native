#pragma once
#include "fs.h"
#include "level.h"
#include "render.h"
#include "sprite.h"
#include "audio.h"
#include "eng1.h"
#include "player.h"
#include "objects.h"   /* OBJECTS */

enum { IN_LEFT = 1, IN_RIGHT = 2, IN_UP = 4, IN_DOWN = 8, IN_JUMP = 16, IN_PUNCH = 32, IN_SWORD = 64,
       /* FRONTEND: original Power Punch (D) and Weapon / Gift-of-the-Gods select (S) */
       IN_POWER = 128, IN_WEAPON = 256 };

typedef enum { SND_JUMP, SND_LAND, SND_PUNCH, SND_STEP, SND_COUNT } SoundId;

/* LEVELS: ENG3 (into-the-screen) levels */
typedef struct {
    int path, cp, grounded, dead, done, anim, moving, prev, runback;
    float depth, X, y, vy;
    int endrow[16], startrow[16];
    int coins, coins2, vases, scrolls, maxhealth;   /* ENG3B: HUD / tally counters (gamestruct +0x18/+0x14/+0x11/+0x1c/+0x1e) */
} E3Play;

/* LEVELS: Titans (Pegasus flight) */
typedef struct {
    int active;
    int32_t x, y, z, vx, vy, ax, ay, speed;     /* 16.16; ax/ay = scroll anchor (actor 0x36) */
    uint32_t script; int16_t idx, timer, frame; int anim, anim_done;
    int32_t zeus_x, zeus_y; int countdown;
    long ticks; int sounds[4], nsounds; uint16_t prevpad;
    Bank bank;                                  /* P:\SRC1\ALIEN2\PEGASUS */
    /* ENG3GAME: Pegasus damage / death / sword (FUN_100379e0 states 2, 0x23, 0x24, 0x48) */
    int state, invuln, death_timer, zeus_obj;
    uint32_t wing_script, rider_script; long wing_t0;
    /* TITANS2: anchor z (actor 0x36 +8, moved by the 0x46..0x4b markers), Pegasus' screen position, fire */
    int32_t az; int zpush;
    float sx, sy; int have_sxy;
} TitansState;

typedef struct {
    const Fs *fs;
    Level L;
    int have_level;
    Renderer R;
    PeImage eng1;
    Bank banks[8];          /* ENG1 bank ids 1..7 (1 = SRC1\HERO\HERCULES) */
    Player P;
    int dead_ticks;
    Camera cam;
    float camx, camy;
    float camz;             /* PLAYER: ENG1 camera path depth (DAT_10070ca8) */
    int fade_on;            /* PLAYER: set by the interactive loop (headless shots stay un-faded unless FADE_TEST) */
    int fade, fade_dir;     /* PLAYER: DAT_1006ee84 / DAT_1007286c in-game brightness 0..0x80 and its step (level start +8, death -8) */
    int cam_d04;            /* PLAYER: DAT_10070d04 camera height bias (short) */
    int cam_dz;             /* FX2: DAT_10070d18 camera depth offset (Hades zoom) */
    int freecam, debug;
    long ticks;
    Sound snd[SND_COUNT];
    Objects O;              /* OBJECTS: level objects (enemies, pickups, checkpoints) */
    int have_objects;
    E3Play E3;              /* LEVELS */
    Bank e3bank;            /* LEVELS: P:\SRC3\HERO\HERCULES */
    PeImage eng3dll; int have_eng3;   /* LEVELS */
    int titans_done;        /* LEVELS */
    TitansState T;          /* LEVELS */
} Game;

int game_init(Game *G, const Fs *fs, const char *eng1_path, int scale);
int game_load_level(Game *G, const char *name);
void game_load_sounds(Game *G);
void game_place_player(Game *G, int x);
void game_tick(Game *G, int in);
/* PLAYER: port of the ENG1 camera (FUN_1002fb70): init at the level table row, per-tick follow */
void game_camera_init(Game *G, int rowx, int rowy, int zstrip);
void game_camera_follow(Game *G);
void game_draw(Game *G);
/* LEVELS: level flow. Level numbers are the game's (TITLE.DLL / ENG tables): 1 Training, 2 Gauntlet,
 * 3 Forest, 4 Nessus (inside Forest), 5 Thebes, 6 Hydra, 7 Medusa, 8 Cyclops, 9 Titans, 10 Torment, 11 Vortex. */
int game_level_done(Game *G);
const char *game_next_level(const char *name);
const char *game_level_name(int level);
void eng3_game_start(Game *G);
void eng3_game_tick(Game *G, int in);
void eng3_game_draw(Game *G);
void titans_start(Game *G, int x, int y, int z);
void titans_tick(Game *G, uint16_t pad, uint16_t pressed);
void titans_sync_player(Game *G);
void titans_draw(Game *G, Renderer *R);
