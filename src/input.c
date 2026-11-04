#include "input.h"
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

const char *const ACT_NAMES[ACT_COUNT] = {"up", "down", "left", "right", "sword", "jump", "punch", "power punch", "weapon"};
const char *const PADACT_NAMES[PADACT_COUNT] = {"sword", "jump", "punch", "power punch", "weapon", "pause"};

void input_reset_keys(InputConfig *c)
{
    c->key[ACT_UP] = SDL_SCANCODE_UP;
    c->key[ACT_DOWN] = SDL_SCANCODE_DOWN;
    c->key[ACT_LEFT] = SDL_SCANCODE_LEFT;
    c->key[ACT_RIGHT] = SDL_SCANCODE_RIGHT;
    c->key[ACT_SWORD] = SDL_SCANCODE_X;
    c->key[ACT_JUMP] = SDL_SCANCODE_Z;
    c->key[ACT_PUNCH] = SDL_SCANCODE_C;
    c->key[ACT_POWER] = SDL_SCANCODE_D;
    c->key[ACT_WEAPON] = SDL_SCANCODE_S;
    c->keys_on = 1;
}

void input_reset_pad(InputConfig *c)
{
    c->pad[PADACT_SWORD] = SDL_CONTROLLER_BUTTON_B;
    c->pad[PADACT_JUMP] = SDL_CONTROLLER_BUTTON_A;
    c->pad[PADACT_PUNCH] = SDL_CONTROLLER_BUTTON_X;
    c->pad[PADACT_POWER] = SDL_CONTROLLER_BUTTON_Y;
    c->pad[PADACT_WEAPON] = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    c->pad[PADACT_PAUSE] = SDL_CONTROLLER_BUTTON_START;
}

void input_defaults(InputConfig *c)
{
    memset(c, 0, sizeof *c);
    input_reset_keys(c);
    input_reset_pad(c);
    c->sfx_on = 1; c->music_on = 1; c->volume = 100;
    c->difficulty = 1; c->lives = 3;
}

static void cfg_path(char *out, size_t cap, int mkdirs)
{
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(out, cap, "%s/Library/Application Support/Hercules", home);
    if (mkdirs) mkdir(out, 0755);
    size_t n = strlen(out);
    snprintf(out + n, cap - n, "/controls.cfg");
}

int input_load(InputConfig *c)
{
    input_defaults(c);
    char p[1024];
    cfg_path(p, sizeof p, 0);
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char name[64], val[128];
        if (sscanf(line, " %63[^=]= %127[^\n]", name, val) != 2) continue;
        for (char *e = name + strlen(name); e > name && e[-1] == ' '; ) *--e = 0;
        for (int i = 0; i < ACT_COUNT; i++) {
            char k[80]; snprintf(k, sizeof k, "key.%s", ACT_NAMES[i]);
            if (!strcmp(name, k)) {
                SDL_Scancode sc = SDL_GetScancodeFromName(val);
                c->key[i] = !strcmp(val, "none") ? SDL_SCANCODE_UNKNOWN : sc;
            }
        }
        for (int i = 0; i < PADACT_COUNT; i++) {
            char k[80]; snprintf(k, sizeof k, "pad.%s", PADACT_NAMES[i]);
            if (!strcmp(name, k)) c->pad[i] = !strcmp(val, "none") ? SDL_CONTROLLER_BUTTON_INVALID : SDL_GameControllerGetButtonFromString(val);
        }
        if (!strcmp(name, "keys_on")) c->keys_on = atoi(val);
        else if (!strcmp(name, "sfx")) c->sfx_on = atoi(val);
        else if (!strcmp(name, "music")) c->music_on = atoi(val);
        else if (!strcmp(name, "volume")) c->volume = atoi(val);
        else if (!strcmp(name, "difficulty")) c->difficulty = atoi(val);
        else if (!strcmp(name, "lives")) c->lives = atoi(val);
    }
    fclose(f);
    return 0;
}

int input_save(const InputConfig *c)
{
    char p[1024];
    cfg_path(p, sizeof p, 1);
    FILE *f = fopen(p, "w");
    if (!f) return -1;
    fprintf(f, "# Disney's Hercules controls (native port)\n");
    for (int i = 0; i < ACT_COUNT; i++) {
        const char *n = c->key[i] == SDL_SCANCODE_UNKNOWN ? "none" : SDL_GetScancodeName(c->key[i]);
        fprintf(f, "key.%s = %s\n", ACT_NAMES[i], n);
    }
    for (int i = 0; i < PADACT_COUNT; i++) {
        const char *n = c->pad[i] == SDL_CONTROLLER_BUTTON_INVALID ? "none" : SDL_GameControllerGetStringForButton(c->pad[i]);
        fprintf(f, "pad.%s = %s\n", PADACT_NAMES[i], n ? n : "none");
    }
    fprintf(f, "keys_on = %d\nsfx = %d\nmusic = %d\nvolume = %d\ndifficulty = %d\nlives = %d\n",
            c->keys_on, c->sfx_on, c->music_on, c->volume, c->difficulty, c->lives);
    fclose(f);
    return 0;
}

static int padbtn(SDL_GameController *pad, SDL_GameControllerButton b)
{
    return pad && b != SDL_CONTROLLER_BUTTON_INVALID && SDL_GameControllerGetButton(pad, b);
}

static int pad_dirs(SDL_GameController *pad)
{
    int d = 0;
    if (!pad) return 0;
    int ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    int ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (ax < -12000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) d |= IN_LEFT;
    if (ax > 12000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) d |= IN_RIGHT;
    if (ay < -12000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) d |= IN_UP;
    if (ay > 12000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) d |= IN_DOWN;
    return d;
}

/* FXI: the PC host's pad aggregator (HERCULES.EXE FUN_00408040) resolves opposite directions before ENG1
 * ever sees the pad word: "if UP then clear DOWN; if LEFT then clear RIGHT" (a PSX d-pad can't press both,
 * so the engine never handles L+R together). Without this a keyboard player who reverses direction while
 * the old arrow is still down (the normal way to turn around) got L+R: the walk state kept going the old way
 * until the old key was released, and in the air the facing flipped every tick (FUN_100174e0). */
int input_socd(int in)
{
    if (in & IN_UP) in &= ~IN_DOWN;
    if (in & IN_LEFT) in &= ~IN_RIGHT;
    return in;
}

/* FXI: FUN_00407f30 returns 0 for the whole keyboard while Alt is held (Alt+Enter is the window toggle). */
static int alt_held(const Uint8 *ks)
{
    return ks && (ks[SDL_SCANCODE_LALT] || ks[SDL_SCANCODE_RALT]);
}

static int raw_game_bits(const InputConfig *c, const Uint8 *ks, SDL_GameController *pad)
{
    static const int bit[ACT_COUNT] = {IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_SWORD, IN_JUMP, IN_PUNCH, IN_POWER, IN_WEAPON};
    int in = 0;
    if (c->keys_on && ks && !alt_held(ks))
        for (int i = 0; i < ACT_COUNT; i++)
            if (c->key[i] != SDL_SCANCODE_UNKNOWN && ks[c->key[i]]) in |= bit[i];
    in |= pad_dirs(pad);
    static const int pbit[PADACT_PAUSE] = {IN_SWORD, IN_JUMP, IN_PUNCH, IN_POWER, IN_WEAPON};
    for (int i = 0; i < PADACT_PAUSE; i++) if (padbtn(pad, c->pad[i])) in |= pbit[i];
    return in;
}

int input_game_bits(const InputConfig *c, const Uint8 *ks, SDL_GameController *pad)
{
    return input_socd(raw_game_bits(c, ks, pad));       /* FXI */
}

/* FXI: key-down latch. The loop polls the keyboard state once per 1/30 s tick, so a tap that starts and
 * ends between two samples (a quick turn-around or jump tap) was lost entirely: that is "the button
 * sometimes does nothing". Every key-down event is remembered until the next tick consumes it, so a tap is
 * always seen for at least one tick (the original's GetAsyncKeyState poll can't lose a tap that long on
 * Windows, where key-down lasts longer than a tick; a held key behaves exactly as before). */
static int g_latch;

void input_note_key(const InputConfig *c, SDL_Scancode sc)
{
    static const int bit[ACT_COUNT] = {IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_SWORD, IN_JUMP, IN_PUNCH, IN_POWER, IN_WEAPON};
    if (!c->keys_on || sc == SDL_SCANCODE_UNKNOWN) return;
    for (int i = 0; i < ACT_COUNT; i++) if (c->key[i] == sc) g_latch |= bit[i];
}

void input_latch_clear(void) { g_latch = 0; }

int input_game_tick(const InputConfig *c, const Uint8 *ks, SDL_GameController *pad)
{
    int in = raw_game_bits(c, ks, pad);
    if (!alt_held(ks)) in |= g_latch;
    g_latch = 0;
    return input_socd(in);
}

uint16_t input_menu_pad(const InputConfig *c, const Uint8 *ks, SDL_GameController *pad)
{
    int in = input_game_bits(c, ks, pad);
    uint16_t p = 0;
    if (ks) {   /* the arrows always navigate menus */
        if (ks[SDL_SCANCODE_UP]) in |= IN_UP;
        if (ks[SDL_SCANCODE_DOWN]) in |= IN_DOWN;
        if (ks[SDL_SCANCODE_LEFT]) in |= IN_LEFT;
        if (ks[SDL_SCANCODE_RIGHT]) in |= IN_RIGHT;
        if (ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER] || ks[SDL_SCANCODE_SPACE]) p |= PSX_START;
        if (ks[SDL_SCANCODE_ESCAPE]) p |= PSX_TRIANGLE;
    }
    in = input_socd(in);        /* FXI: same host filter as in game */
    if (in & IN_UP) p |= PSX_UP;
    if (in & IN_DOWN) p |= PSX_DOWN;
    if (in & IN_LEFT) p |= PSX_LEFT;
    if (in & IN_RIGHT) p |= PSX_RIGHT;
    if (in & IN_JUMP) p |= PSX_CROSS;
    if (in & IN_SWORD) p |= PSX_SQUARE;     /* ENG1 defaults: sword SQUARE, punch CIRCLE */
    if (in & IN_PUNCH) p |= PSX_CIRCLE;
    if (in & IN_POWER) p |= PSX_TRIANGLE;
    if (in & IN_WEAPON) p |= PSX_R1;
    if (padbtn(pad, c->pad[PADACT_PAUSE])) p |= PSX_START;
    if (padbtn(pad, SDL_CONTROLLER_BUTTON_BACK)) p |= PSX_TRIANGLE;
    return p;
}
