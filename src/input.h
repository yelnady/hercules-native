/* Player controls, matching the original PC defaults (HERCULES.HLP / HERCULES.EXE CKeyboardPage):
 * arrows move, Z jump, X sword, C punch, D power punch, S weapon. Fixed keys: F1 help, F2 properties,
 * F3 restart level, Esc pause, Enter/Space select, Alt+Enter fullscreen. Remappable and saved to
 * ~/Library/Application Support/Hercules/controls.cfg. */
#pragma once
#include <SDL.h>
#include <stdint.h>

/* Order of the original Properties > Keyboard page. */
enum { ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_SWORD, ACT_JUMP, ACT_PUNCH, ACT_POWER, ACT_WEAPON, ACT_COUNT };
/* Order of the original Properties > Gamepad page. */
enum { PADACT_SWORD, PADACT_JUMP, PADACT_PUNCH, PADACT_POWER, PADACT_WEAPON, PADACT_PAUSE, PADACT_COUNT };

/* PSX pad bits as TITLE.DLL / ENG1.DLL read them (gamestruct +0x5c held, +0x5e pressed). */
enum {
    PSX_SELECT = 0x0001, PSX_START = 0x0008, PSX_L1 = 0x0400, PSX_R1 = 0x0800,
    PSX_UP = 0x0010, PSX_RIGHT = 0x0020, PSX_DOWN = 0x0040, PSX_LEFT = 0x0080,
    PSX_TRIANGLE = 0x1000, PSX_CIRCLE = 0x2000, PSX_CROSS = 0x4000, PSX_SQUARE = 0x8000,
};

typedef struct {
    SDL_Scancode key[ACT_COUNT];
    int keys_on;                                /* "key1on": keyboard control enabled */
    SDL_GameControllerButton pad[PADACT_COUNT]; /* SDL_CONTROLLER_BUTTON_INVALID = cleared */
    int sfx_on, music_on, volume;               /* Properties > Sound */
    int difficulty, lives;                      /* Properties > Game Play */
} InputConfig;

extern const char *const ACT_NAMES[ACT_COUNT];
extern const char *const PADACT_NAMES[PADACT_COUNT];

void input_defaults(InputConfig *c);
void input_reset_keys(InputConfig *c);
void input_reset_pad(InputConfig *c);
int input_load(InputConfig *c);
int input_save(const InputConfig *c);
/* In-game action bits (game.h IN_*) from the keyboard state and an optional controller. */
int input_game_bits(const InputConfig *c, const Uint8 *ks, SDL_GameController *pad);
/* FXI: host pad filter (HERCULES.EXE FUN_00408040): UP beats DOWN, LEFT beats RIGHT. */
int input_socd(int in);
/* FXI: remember a key-down until the next game tick; input_game_tick = held state | latched taps, then SOCD. */
void input_note_key(const InputConfig *c, SDL_Scancode sc);
void input_latch_clear(void);
int input_game_tick(const InputConfig *c, const Uint8 *ks, SDL_GameController *pad);
/* PSX pad word for the front end (menus/password/cards). */
uint16_t input_menu_pad(const InputConfig *c, const Uint8 *ks, SDL_GameController *pad);
