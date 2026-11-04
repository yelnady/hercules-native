#include "game.h"
#include "fx3.h"   /* FX3 */
#include "fx2.h"   /* FX2 */
#include "fx.h"   /* FX */
#include "sfx.h"     /* SOUND: */
#include "objects_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Sprite bank ids used by the player scripts (ENG1 table at 0x1005cee4: path pointer at id*0x14 - 4). */
static const char *BANK_PATHS[8] = {
    NULL, "P:\\SRC1\\HERO\\HERCULES\\ANIMPSX.BIN", "P:\\SRC1\\HERO\\XTRA_DB1\\ANIMPSX.BIN",
    "P:\\SRC1\\HERO\\HERCULES\\HC_THROW\\ANIMPSX.BIN", "P:\\SRC1\\HERO\\HERCULES\\HC_DIE\\ANIMPSX.BIN",
    "P:\\SRC1\\HERO\\HERCULES\\HC_DUNIT\\ANIMPSX.BIN", NULL, NULL,
};

/* Level start points (x, y, strip) from ENG1's level table (0x1005a6a8, first record of each entry). */
static const struct { const char *name; int x, y, strip; } STARTS[] = {
    {"TRAINING", 1246, 716, 1}, {"FOREST", 990, 588, 0}, {"THEBES", 288, 1164, 0}, {"HYDRA", 2240, 992, 1},
    {"MEDUSA", 4928, 640, 1}, {"TITANS", 192, 832, 1}, {"VORTEX", 144, 416, 1},
};

static char data_dir[512] = ".";      /* SOUND: directory of HERCULES.FS / ENG1.DLL (optional CD-audio rips) */

int game_init(Game *G, const Fs *fs, const char *eng1_path, int scale)
{
    memset(G, 0, sizeof *G);
    {   /* SOUND: */
        snprintf(data_dir, sizeof data_dir, "%s", eng1_path);
        char *sl = strrchr(data_dir, '/');
        if (sl) *sl = 0; else snprintf(data_dir, sizeof data_dir, ".");
    }
    G->fs = fs;
    render_init(&G->R, scale);
    if (pe_load(&G->eng1, eng1_path)) {
        fprintf(stderr, "cannot read %s (copy ENG1.DLL from the installed game next to HERCULES.FS)\n", eng1_path);
        return -1;
    }
    {   /* LEVELS: ENG3.DLL sits next to ENG1.DLL; its tables give the ENG3 levels' start points */
        char p3[600];
        snprintf(p3, sizeof p3, "%s/ENG3.DLL", data_dir);
        G->have_eng3 = pe_load(&G->eng3dll, p3) == 0;
    }
    for (int i = 1; i < 8; i++)
        if (BANK_PATHS[i] && bank_load(&G->banks[i], fs, BANK_PATHS[i]))
            fprintf(stderr, "sprite bank %d missing\n", i);
    return 0;
}

void game_load_sounds(Game *G) { (void)G; }

/* Copy the level's attribute behaviour table out of ENG1 (0x10068af0: 0x48-byte entries keyed by level). */
static void load_level_flags(Game *G)
{
    Level *L = &G->L;
    for (uint32_t a = 0x10068af0; pe_ptr(&G->eng1, a, 0x48); a += 0x48) {
        uint16_t lvl = pe_u16(&G->eng1, a);
        if (lvl == 999) break;
        if (lvl != L->index) continue;
        uint32_t t = pe_u32(&G->eng1, a + 0x44);
        memset(L->flags, 0, sizeof L->flags);
        for (int i = 0; i < 64; i++) L->flags[i] = pe_u16(&G->eng1, t + 2 * i);
        return;
    }
}

/* PLAYER: ENG1 level table row (PTR_DAT_1005a69c + level * 0x18 .. rows of 0x18 bytes: u16 x, u16 y, u16 strip).
 * FUN_10015090 starts Hercules at (x - 0x20, y + 0x40) of row gs[1], the camera at (x, y); a checkpoint
 * (FUN_100139f0) stores its id in gs[2], and a restart copies it to gs[1]. */
static int table_row(Game *G, int row, int *x, int *y, int *strip)
{
    uint32_t t = pe_u32(&G->eng1, 0x1005a69c + 24 * (uint32_t)G->L.index);
    if (!t) return 0;
    *x = pe_u16(&G->eng1, t + 0x18 * (uint32_t)row);
    *y = pe_u16(&G->eng1, t + 0x18 * (uint32_t)row + 2);
    *strip = pe_u16(&G->eng1, t + 0x18 * (uint32_t)row + 4) & 0xff;
    return 1;
}

static int g_respawn;      /* PLAYER: place_start called for a restart after death (FUN_1001f320 reloads the level) */

static void place_start(Game *G)
{
    int x = 64 * 3 + 32, y = 0, strip = 0, rx = 0, ry = 0, use_row = 0;
    for (size_t i = 0; i < sizeof STARTS / sizeof STARTS[0]; i++)
        if (!strcmp(STARTS[i].name, G->L.name)) { x = STARTS[i].x; y = STARTS[i].y; strip = STARTS[i].strip; }
    int row = G->have_objects && G->O.have_cp && G->O.checkpoint >= 0 && G->O.checkpoint < 8 ? G->O.checkpoint : 0;
    if (G->L.index != 9 && !G->L.mode && table_row(G, row, &rx, &ry, &strip)) {   /* PLAYER: start / restart at the table row (ring arenas keep their own start) */
        x = rx - 0x20; y = ry + 0x40; use_row = 1;
    }
    if (G->L.mode) x %= 64 * 64;
    /* COMBAT: keep lives / difficulty across respawns (reset on game over) */
    int lives = G->P.lives, diff = G->P.difficulty, maxhp = G->P.max_health, keep = G->P.pe != NULL && !G->P.game_over;
    int z = strip * 128;
    if (!use_row && G->have_objects && G->O.have_cp) {      /* OBJECTS: respawn at the last checkpoint */
        x = G->O.cp_x >> 16; y = (G->O.cp_y >> 16) - 8; z = G->O.cp_z >> 16;
    }
    player_init(&G->P, &G->eng1, &G->L, x, y, z);
    G->P.banks = G->banks;
    if (keep) { G->P.lives = lives; G->P.difficulty = diff; if (maxhp >= 0x80) G->P.max_health = maxhp; }
    if (G->have_objects) { G->P.obj_attr = objects_attr; G->P.obj_ctx = &G->O; }   /* OBJECTS */
    G->T.active = 0;
    if ((G->L.index == 9 || (g_respawn && (G->L.index == 1 || G->L.index == 3 || G->L.index == 5))) && G->have_objects) {   /* TITANS2 / PLAYER: the level restarts from the checkpoint with fresh actors */
        int cp = G->O.checkpoint, hc = G->O.have_cp; int32_t cx = G->O.cp_x, cy = G->O.cp_y, cz = G->O.cp_z;
        int c18 = G->O.coins18, c14 = G->O.coins14, let = G->O.letters; uint16_t sb = G->O.scroll_bits;
        objects_free(&G->O);
        G->have_objects = objects_init(&G->O, G->fs, &G->eng1, &G->L) == 0;
        G->O.checkpoint = cp; G->O.have_cp = hc; G->O.cp_x = cx; G->O.cp_y = cy; G->O.cp_z = cz;
        G->O.coins18 = c18; G->O.coins14 = c14; G->O.letters = let; G->O.scroll_bits = sb;
        if (G->have_objects) { G->P.obj_attr = objects_attr; G->P.obj_ctx = &G->O; }
    }
    if (G->L.index == 9) { titans_start(G, x, y, z); titans_sync_player(G); }   /* LEVELS: Pegasus flight */
    G->camx = (float)x;
    G->camy = (float)y - 52;
    if (use_row && !G->L.mode) game_camera_init(G, rx, ry, strip);      /* PLAYER: camera starts on the row */
    G->dead_ticks = 0;
    G->fade = 0; G->fade_dir = 8;       /* PLAYER: FUN_10020300 / FUN_1001f320: fade in over 16 ticks */
}

void game_place_player(Game *G, int x)
{
    player_init(&G->P, &G->eng1, &G->L, x, 64, G->P.o.z >> 16);   /* DOLLS: keep the level's start plane */
    G->P.banks = G->banks;      /* COMBAT */
    if (G->have_objects) { G->P.obj_attr = objects_attr; G->P.obj_ctx = &G->O; }   /* OBJECTS */
    G->camx = (float)x;
    G->camy = 64;
}

int game_load_level(Game *G, const char *name)
{
    if (G->have_objects) objects_free(&G->O);    /* OBJECTS */
    G->have_objects = 0;
    if (G->have_level) level_free(&G->L);
    G->have_level = 0;
    if (level_load(&G->L, G->fs, name)) return -1;
    G->have_level = 1;
    sprite_set_level_palette((const uint8_t (*)[3])G->L.pal);   /* FX2: sprite colours 0x80..0xff = level CIF */
    G->cam_dz = 0;   /* FX2 */
    G->R.ofy = 128;                                         /* LEVELS */
    G->R.light = getenv("FX3_NOLIGHT") ? 0 : fx3_level_light(&G->L);   /* FX3: level light (map header colour) */
    if (G->L.eng3) {                                        /* LEVELS: ENG3 level */
        printf("level %s (ENG3): %d paths, %d faces\n", name, G->L.e3->npaths, G->L.nfaces);
        if (G->have_eng3) sfx_load_level_tbl(G->fs, &G->eng3dll, 0x1003853cu, G->L.index, data_dir);   /* AUDSHELL: ENG3 levels had no sound at all */
        eng3_game_start(G);
        return 0;
    }
    load_level_flags(G);
    G->have_objects = objects_init(&G->O, G->fs, &G->eng1, &G->L) == 0;   /* OBJECTS */
    if (G->have_objects) printf("objects: %d spawn records\n", G->O.nsp);
    if (G->have_objects && getenv("SPDUMP")) for (int i = 0; i < G->O.nsp; i++) printf("sp %04x %d %d %d\n", G->O.sp[i].type, G->O.sp[i].x >> 16, G->O.sp[i].y >> 16, G->O.sp[i].z >> 16);
    sfx_load_level(G->fs, &G->eng1, G->L.index, data_dir);     /* SOUND: */
    printf("level %s: %d strips x %d cols x %d rows, %d faces, %d segments, mode %d\n", name, G->L.nstrips,
           G->L.ncols, G->L.nrows, G->L.nfaces, G->L.nsegs, G->L.mode);
    place_start(G);
    return 0;
}

static uint16_t to_pad(int in)
{
    uint16_t p = 0;
    if (in & IN_LEFT) p |= PAD_LEFT;
    if (in & IN_RIGHT) p |= PAD_RIGHT;
    if (in & IN_UP) p |= PAD_UP;
    if (in & IN_DOWN) p |= PAD_DOWN;
    /* FRONTEND: original ENG1 pad masks (DAT_1004d958 -> gamestruct +0x6c..+0x76) */
    if (in & IN_SWORD) p |= PAD_SQUARE;         /* +0x6c sword */
    if (in & IN_JUMP) p |= PAD_CROSS;           /* +0x6e jump */
    if (in & IN_PUNCH) p |= PAD_CIRCLE;         /* +0x70 punch */
    if (in & IN_POWER) p |= PAD_TRIANGLE;       /* +0x72 power punch */
    if (in & IN_WEAPON) p |= 0x0800;            /* +0x76 next weapon (R1) */
    return p;
}

static void update_camera(Game *G);
static Game *sfx_game;
/* AUDSHELL: screen x / depth the original keeps in obj+0xc / obj+0x10 (FUN_1002e3b0: GTE SZ >> 2, 160 at the player
 * plane) and whether the object is in this frame's draw list (obj+0x68). Returns 0 if the object is gone. */
static int obj_sfx_pos(void *ctx, SfxPos *p)
{
    Game *G = sfx_game;
    int slot = (int)(intptr_t)ctx - 2;
    if (!G || !G->have_objects || slot < 0 || slot >= OBJ_MAX || !G->O.obj[slot].used) return 0;
    const Object *o = &G->O.obj[slot];
    float wx, wz;
    level_path_to_world(&G->L, o->x / 65536.0f, o->z / 65536.0f, &wx, &wz, NULL);
    const Camera *c = &G->cam;
    float dx = wx - c->x, dz = wz - c->z;
    float xc = dx * c->rx + dz * c->rz, zc = dx * c->fx + dz * c->fz;
    if (zc < 96.0f) { p->hidden = 1; p->depth = 0; return 1; }              /* behind the near plane: not drawn */
    p->sx = 160 + (int)(xc * 320.0f / zc);
    p->depth = (int)(zc * 0.5f);
    p->hidden = p->sx < -80 || p->sx > 400;   /* big sprites are still listed with their centre a little off screen */
    return 1;
}

void game_tick(Game *G, int in)
{
    const Level *L = &G->L;
    if (L->eng3) { eng3_game_tick(G, in); return; }        /* LEVELS */
    G->ticks++;
    if (getenv("WEAPON")) {   /* FXW: test hook WEAPON=n (1 fire, 2 lightning, 3 sonic, 4 invulnerable) selects a Gift with full charges */
        int w = atoi(getenv("WEAPON"));
        if (w >= 1 && w <= 4) { if (G->ticks == 1 || G->P.ammo[w] < 200) { G->P.ammo[w] = 255; } if (G->ticks == 1) G->P.weapon = w; }
    }
    if (G->P.fade_req) { G->P.fade_req = 0; G->fade_dir = -8; }       /* PLAYER: script op 0x2a (death fade out) */
    if (G->fade_dir) {                                                /* FUN_10005d50 */
        G->fade += G->fade_dir;
        if (G->fade >= 0x80) { G->fade = 0x80; G->fade_dir = 0; }
        else if (G->fade <= 0) { G->fade = 0; G->fade_dir = 0; }
    }
    if (G->freecam) {
        float sp = 12;
        if (in & IN_LEFT) G->camx -= sp;
        if (in & IN_RIGHT) G->camx += sp;
        if (in & IN_UP) G->camy -= sp;
        if (in & IN_DOWN) G->camy += sp;
        return;
    }
    if (!L->mode && !G->T.active) game_camera_follow(G);   /* PLAYER: FUN_1002fb70 runs before the player update */
    if (G->T.active) {                                      /* LEVELS: Titans: Pegasus replaces the player */
        uint16_t pad = to_pad(in);
        titans_tick(G, pad, pad & ~G->T.prevpad);
        G->T.prevpad = pad;
        titans_sync_player(G);
        G->P.nsounds = 0;
        for (int i = 0; i < G->T.nsounds && i < 8; i++) G->P.sounds[G->P.nsounds++] = G->T.sounds[i];
    } else
    player_tick(&G->P, to_pad(in));
    {   /* SOUND: play what the animation scripts asked for; the player is drawn at depth 160 */
        SfxPos pos = {160 + (int)(G->P.o.x / 65536.0f - G->camx), 160, 0};
        for (int i = 0; i < G->P.nsounds; i++) {
            int id = G->P.sounds[i];
            if (getenv("SND_LOG")) printf("SND t=%ld state=%d frame=%d id=%x\n", G->ticks, G->P.state, G->P.o.frame, id & 0xffff);   /* PLAYER: test hook */
            if (id >= 0) sfx_play((uint16_t)id, 1, &pos);
            else sfx_stop((uint16_t)-id, 1);
        }
    }
    if (G->P.dead && ++G->dead_ticks > 45) { g_respawn = 1; place_start(G); g_respawn = 0; }
    if (G->have_objects) {
        G->O.cam_y = (int)G->camy;   /* FX2 */
        objects_tick(&G->O, &G->P, G->camx);   /* OBJECTS */
        titans_apply_hits(G);                  /* FXB: Titans actors hurt Pegasus in the frame they touch it (FUN_10037e30) */
        for (int k = 0; k < OBJ_MAX; k++)       /* FX2: Forest water 0x4b (FUN_1000d460): scroll texture record 0 by -16 a frame */
            if (G->O.obj[k].used && G->O.obj[k].type == 0x4b && G->L.index == 3) { fx2_tex_scroll(&G->L, 0, 0, -16); break; }
        {   /* FX2: FUN_10026730 - DAT_10070d18 eases toward the DAT_10071848 request each tick */
        int d = G->cam_dz - G->O.cam_zoff;
        if (d < -0x10 || d > 0x10) G->cam_dz -= d >> 3;
        else if (d > 0) G->cam_dz -= (d >> 1) + (d & 1);
        else if (d < 0) G->cam_dz += (-d >> 1) + (-d & 1);
        }
        if (G->O.level_end && !G->P.won) player_win(&G->P);   /* LEVELS: victory pose, then script op 0x2b */
        update_camera(G);                          /* AUDSHELL: emitters are projected with the camera of the last frame */
        sfx_game = G;
        for (int i = 0; i < G->O.nsounds; i++) {   /* ENEMIES: each sound at its source's screen x */
            int ow = G->O.sound_owner[i];
            if (ow == -4) { sfx_stop_owner_loops(G->O.sound_x[i]); continue; }
            if (ow == -3) { sfx_stop((uint16_t)G->O.sounds[i], 0); continue; }                              /* FUN_10007f8e(id, 0) */
            if (ow == -2) { sfx_stop_owner(G->O.sound_x[i]); continue; }      /* script op -0x2e: FUN_10007ef6 */
            SfxPos pos = {160 + (int)(G->O.sound_x[i] - G->camx), 160, 0};
            if (ow == 0) { sfx_play((uint16_t)G->O.sounds[i], 0, NULL); continue; }   /* FUN_10006da0(id, 0) */
            if (ow >= 2) {                                                   /* emitter object: real screen x / depth, tracked */
                obj_sfx_pos((void *)(intptr_t)ow, &pos);
                sfx_play_ex((uint16_t)G->O.sounds[i], ow, &pos, obj_sfx_pos, (void *)(intptr_t)ow);
            } else sfx_play((uint16_t)G->O.sounds[i], 1, &pos);
        }
    }
    sfx_tick();                                    /* AUDSHELL: FUN_1000666b - tracked voices follow their emitters */

    if (!L->mode && !G->T.active) return;      /* PLAYER: ENG1 side-scrollers use game_camera_follow (start of the tick) */
    /* Camera: follow with a little lead in the facing direction. */
    const Obj *o = &G->P.o;
    float px = o->x / 65536.0f, py = o->y / 65536.0f;
    float tx = px + (o->facing ? -24 : 24), ty = py - 52;
    if (G->T.active) { tx = G->T.ax / 65536.0f + 24; ty = G->T.ay / 65536.0f - 100; }   /* LEVELS: anchor 0x36 */
    if (L->mode) {
        float ring = 64 * 64, d = tx - G->camx;
        if (d > ring / 2) G->camx += ring;
        if (d < -ring / 2) G->camx -= ring;
    }
    G->camx += (tx - G->camx) * 0.15f;
    if (G->have_objects) ty += G->O.cam_bias / 2;   /* LEFTOVERS: DAT_10070d04 camera height bias (Thebes 0x202d) */
    G->camy += (ty - G->camy) * 0.15f;
    if (!L->mode) {
        float lo = 64 + 160, hi = L->ncols * 64 - 64 - 160;
        if (G->have_objects && G->O.cam_xmax && hi > G->O.cam_xmax) hi = (float)G->O.cam_xmax;   /* LEFTOVERS: DAT_10070ce8 */
        if (G->camx < lo) G->camx = lo;
        if (G->camx > hi) G->camx = hi;
    }
}

static void update_camera(Game *G)
{
    const Level *L = &G->L;
    float pz = G->freecam ? 0 : G->P.o.z / 65536.0f;
    if (G->T.active && !G->freecam) pz = G->T.az / 65536.0f;   /* TITANS2: the camera follows the anchor (DAT_100734f4) */
    float wx, wz;
    if (G->have_objects && !G->freecam) {   /* FX2: FUN_10026730 - DAT_10070d18 eases toward the DAT_10071848 request */
        pz += (float)G->cam_dz;
    }
    float cz = pz - 320;
    if (!L->mode && !G->T.active && !G->freecam) cz = G->camz;      /* PLAYER: DAT_10070ca8, eased towards pz - 320 + DAT_10070d18 */
    level_path_to_world(L, G->camx, cz, &wx, &wz, NULL);
    level_path_frame(L, G->camx, cz, &G->cam.rx, &G->cam.rz, &G->cam.fx, &G->cam.fz);
    G->cam.x = wx; G->cam.z = wz; G->cam.y = G->camy + (float)G->P.shake_y;   /* COMBAT: shake */
    if (G->have_objects && G->O.shake_n)        /* LEFTOVERS: FUN_10026730 averages the player shake with DAT_10071680 */
        G->cam.y = G->camy + (float)(((double)G->P.shake_y * 65536.0 + G->O.shake_sum) / (G->O.shake_n + 1) / 65536.0);
    if (G->have_objects) G->cam.y += (float)(G->O.cam_dy >> 16);   /* LEFTOVERS: Medusa's quake. BLACKFIX: DAT_10070cbc is 16.16 (FUN_1002f840 uses >>16); adding it raw moved the camera 262144 units */
    G->cam.path_x = G->camx;
    G->cam.cam_z = pz - 320; G->cam.cam_z_set = 1;      /* RENDERFIX: DAT_10070ca8 */
}

static void draw_debug(Game *G)
{
    const Level *L = &G->L;
    Renderer *R = &G->R;
    int z = G->P.o.z >> 16;
    for (int x = (int)G->camx - 220; x < (int)G->camx + 220; x += 4) {
        int y = -64;
        for (int n = 0; n < 12; n++) {
            int surf;
            if (level_floor(L, x, y, z, &surf) >= 0x7fff) break;
            float wx, wz, sx, sy, zc;
            level_path_to_world(L, (float)x, (float)z, &wx, &wz, NULL);
            if (render_project(R, wx, (float)surf, wz, &sx, &sy, &zc))
                render_rect(R, (int)sx - 1, (int)sy - 1, 2, 2, 0xffff00ff);
            y = surf + 8;
        }
        for (int y2 = (int)G->camy - 160; y2 < (int)G->camy + 160; y2 += 8) {
            int a = level_attr(L, x, y2, z);
            if (!a) continue;
            uint16_t f = a > 0 && a < 128 ? L->flags[a] : 0;
            uint32_t col = (f & 0x3000) ? 0xff00ffff : (f & 1) ? 0x80ff4000 : 0x400080ff;
            float wx, wz, sx, sy, zc;
            level_path_to_world(L, (float)x, (float)z, &wx, &wz, NULL);
            if (render_project(R, wx, (float)y2, wz, &sx, &sy, &zc)) render_rect(R, (int)sx - 1, (int)sy - 1, 2, 2, col);
        }
    }
}

/* COMBAT: HIT_BOXES=1 debug overlay: body (green), attack (red), box 3 (blue) of the player and every object,
 * hotspots as yellow crosses. Boxes are drawn as their front face at the box's mid z. */
static void hb_box(Game *G, const int b[6], uint32_t col)
{
    float c[4][2], sx, sy, zc;
    int xs[2] = {b[0], b[3]}, ys[2] = {b[1], b[4]};
    int zm = (b[2] + b[5]) / 2;
    for (int i = 0; i < 4; i++) {
        float wx, wz;
        level_path_to_world(&G->L, (float)xs[i & 1], (float)zm, &wx, &wz, NULL);
        if (!render_project(&G->R, wx, (float)ys[i >> 1], wz, &sx, &sy, &zc)) return;
        c[i][0] = sx; c[i][1] = sy;
    }
    int x0 = (int)c[0][0], x1 = (int)c[1][0], y0 = (int)c[0][1], y1 = (int)c[2][1];
    render_rect(&G->R, x0, y0, x1 - x0 + 1, 1, col); render_rect(&G->R, x0, y1, x1 - x0 + 1, 1, col);
    render_rect(&G->R, x0, y0, 1, y1 - y0 + 1, col); render_rect(&G->R, x1, y0, 1, y1 - y0 + 1, col);
}

static void hb_cross(Game *G, int32_t x, int32_t y, int32_t z)
{
    float wx, wz, sx, sy, zc;
    level_path_to_world(&G->L, x / 65536.0f, z / 65536.0f, &wx, &wz, NULL);
    if (!render_project(&G->R, wx, y / 65536.0f, wz, &sx, &sy, &zc)) return;
    render_rect(&G->R, (int)sx - 3, (int)sy, 7, 1, 0xffffff00); render_rect(&G->R, (int)sx, (int)sy - 3, 1, 7, 0xffffff00);
}

static void draw_hitboxes(Game *G)
{
    static const uint32_t col[4] = {0, 0xff00ff00, 0xffff0000, 0xff4080ff};
    Box pb;
    if (player_body_box(&G->P, &pb)) { int b[6] = {pb.x0, pb.y0, pb.z0, pb.x1, pb.y1, pb.z1}; hb_box(G, b, 0xffffffff); }
    Box ab;
    if (player_attack_box(&G->P, &ab)) { int b[6] = {ab.x0, ab.y0, ab.z0, ab.x1, ab.y1, ab.z1}; hb_box(G, b, 0xffff0000); }
    hb_cross(G, G->P.o.x, G->P.o.y, G->P.o.z);
    if (G->P.o.bank < 8) {   /* sprite image bounds (magenta) */
        const Image *im = bank_frame(&G->banks[G->P.o.bank], G->P.o.frame);
        if (im) {
            int fl = (G->P.o.flags54 & 0x10) != 0, x0 = fl ? -(im->w - im->ox) : -im->ox, b[6] = {(G->P.o.x >> 16) + x0, (G->P.o.y >> 16) - im->oy, G->P.o.z >> 16,
                (G->P.o.x >> 16) + x0 + im->w, (G->P.o.y >> 16) - im->oy + im->h, G->P.o.z >> 16};
            hb_box(G, b, 0xffff00ff);
        }
        if (getenv("HIT_BOXES_LOG")) {
            for (int k = 1; k <= 3; k++) { Box q; if (player_frame_box(&G->P, k, &q)) printf("  pbox%d (%d,%d)-(%d,%d) z %d..%d\n", k, q.x0 - (G->P.o.x >> 16), q.y0 - (G->P.o.y >> 16), q.x1 - (G->P.o.x >> 16), q.y1 - (G->P.o.y >> 16), q.z0 - (G->P.o.z >> 16), q.z1 - (G->P.o.z >> 16)); }
            if (im) printf("  img w=%d h=%d ox=%d oy=%d frame=%d\n", im->w, im->h, im->ox, im->oy, G->P.o.frame);
        }
    }
    if (!G->have_objects) return;
    for (int i = 0; i < OBJ_MAX; i++) {
        const Object *o = &G->O.obj[i];
        if (!o->used || o->frame <= 0) continue;
        int any = 0;
        for (int k = 1; k <= 3; k++) { int b[6]; if (obj_box(&G->O, o, k, b)) { hb_box(G, b, col[k]); any = 1; } }
        if (any) hb_cross(G, o->x, o->y, o->z);
        if (any && getenv("HIT_AUDIT")) {   /* box 1 against the drawn image rectangle, both relative to the hotspot */
            Bank *bk = obj_bank(&G->O, o->bank);
            const Image *im = bk ? bank_frame(bk, o->frame) : NULL;
            int b[6];
            if (im && obj_box(&G->O, o, 1, b)) {
                int fl = (o->flags54 & 0x10) != 0, sc = o->scale ? o->scale : 0x100;
                int ix0 = fl ? -(im->w - im->ox) : -im->ox, ix1 = ix0 + im->w, iy0 = -im->oy, iy1 = iy0 + im->h;
                ix0 = ix0 * sc >> 8; ix1 = ix1 * sc >> 8; iy0 = iy0 * sc >> 8; iy1 = iy1 * sc >> 8;
                int bx0 = b[0] - (o->x >> 16), bx1 = b[3] - (o->x >> 16), by0 = b[1] - (o->y >> 16), by1 = b[4] - (o->y >> 16);
                int ox = (bx1 < ix1 ? bx1 : ix1) - (bx0 > ix0 ? bx0 : ix0), oy = (by1 < iy1 ? by1 : iy1) - (by0 > iy0 ? by0 : iy0);
                int ba = (bx1 - bx0) * (by1 - by0);
                int pct = ox > 0 && oy > 0 && ba > 0 ? 100 * ox * oy / ba : 0;
                printf("AUDIT type %04x bank %d frame %d box x %d..%d y %d..%d  img x %d..%d y %d..%d  inside %d%%%s\n", o->type, o->bank, o->frame,
                       bx0, bx1, by0, by1, ix0, ix1, iy0, iy1, pct, pct < 60 ? "  <-- MISALIGNED" : "");
            }
        }
    }
}

void game_draw(Game *G)
{
    const Level *L = &G->L;
    if (L->eng3) { eng3_game_draw(G); return; }            /* LEVELS */
    update_camera(G);
    uint32_t bg = 0xff000000u | (uint32_t)L->bg[0] << 16 | (uint32_t)L->bg[1] << 8 | L->bg[2];
    render_begin(&G->R, L, &G->cam, bg);
    render_level(&G->R);
    if (G->T.active && !G->freecam) titans_draw(G, &G->R);   /* LEVELS */
    else if (!G->freecam) {
        const Obj *o = &G->P.o;
        float wx, wz;
        level_path_to_world(L, o->x / 65536.0f, o->z / 65536.0f, &wx, &wz, NULL);
        const Image *img = o->bank < 8 ? bank_frame(&G->banks[o->bank], o->frame) : NULL;
        /* COMBAT: hurt flash blend, and the sword-trail overlay (child object, bank 1) */
        int flip = (o->flags54 & 0x10) != 0;
        if (G->P.blend_mode >= 0)
            render_sprite_ex(&G->R, img, wx, o->y / 65536.0f, wz, flip, -8, G->P.blend_mode, G->P.blend_level);
        else render_sprite(&G->R, img, wx, o->y / 65536.0f, wz, flip, -8);
        /* FX: Hercules' 0x2024 shadow child (FUN_10015090 -> FUN_10027cd0(player, 0x2024)); brightness 0xe0 */
        fx_shadow_draw(&G->R, L, G->have_objects ? (void *)&G->O : NULL, img, o->x, o->y, o->z, flip, 0x100, 0xe0,
                       G->P.water_y);
        if ((o->flags54 & 0x800000) && G->P.child_frame > 0) {
            const Image *ov = bank_frame(&G->banks[1], G->P.child_frame);
            if (G->P.child_trans) render_sprite_ex(&G->R, ov, wx, o->y / 65536.0f, wz, flip, -9, 1, 255);
            else render_sprite(&G->R, ov, wx, o->y / 65536.0f, wz, flip, -9);
        }
    }
    if (G->have_objects) objects_draw(&G->O, &G->R);   /* OBJECTS */
    render_flush(&G->R);
    if (G->fade_on && G->fade < 0x80 && !G->freecam) {      /* PLAYER: in-game fade (DAT_1006ee84); interactive play only */
        uint32_t *px = G->R.fb;
        for (int i = 0; i < G->R.w * G->R.h; i++) {
            uint32_t c = px[i];
            uint32_t r = ((c >> 16) & 0xff) * (uint32_t)G->fade >> 7, g = ((c >> 8) & 0xff) * (uint32_t)G->fade >> 7, b = (c & 0xff) * (uint32_t)G->fade >> 7;
            px[i] = 0xff000000u | r << 16 | g << 8 | b;
        }
    }
    if (G->debug) draw_debug(G);
    if (getenv("HIT_BOXES")) draw_hitboxes(G);   /* COMBAT */
}

/* ---- LEVELS: level flow ----
 * Game order and "next level" from TITLE.DLL FUN_10012070 (1->2->3->5->6->7->8->9->10->11->12 = end).
 * Level 4 (Nessus) is played inside the Forest map; it has no map of its own in HERCULES.FS. */
static const char *ORDER[13] = {NULL, "TRAINING", "GAUNTLET", "FOREST", NULL, "THEBES", "HYDRA", "MEDUSA",
                                "CYCLOPS", "TITANS", "TORMENT", "VORTEX", NULL};

const char *game_level_name(int level) { return level >= 0 && level < 13 ? ORDER[level] : NULL; }

const char *game_next_level(const char *name)
{
    for (int i = 1; i < 12; i++)
        if (ORDER[i] && !strcmp(ORDER[i], name))
            for (int j = i + 1; j < 12; j++) if (ORDER[j]) return ORDER[j];
    return NULL;    /* after Vortex: the game is finished (end credits) */
}

int game_level_done(Game *G)
{
    if (!G->have_level) return 0;
    if (G->L.eng3) return G->E3.done;                       /* end of the last path */
    if (G->P.signal == 0x2b) return 1;                      /* script op 0x2b after the victory pose */
    if (G->titans_done) return 1;
    /* NESSUS: the Forest ends only through the Nessus fight (player_win -> script op 0x2b above) */
    return 0;
}
