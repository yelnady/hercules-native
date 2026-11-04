#include "solids.h"
/* Hercules native engine: entry point, main loop and front-end/game glue.
 * Boots like the original (logos -> intro -> title -> main menu); --level skips straight into a level. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <unistd.h>
#include "fs.h"
#include "level.h"
#include "render.h"
#include "sprite.h"
#include "audio.h"
#include "sfx.h"     /* SOUND: */
#include "game.h"
#include "input.h"      /* FRONTEND: original key layout + Properties sheet */
#include "frontend.h"   /* FRONTEND: TITLE.DLL flow */
#include "objects_int.h"
#include "hud.h"        /* FRONTEND: ENG1 status panel + pause text */

static int file_exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) { fclose(f); return 1; } return 0; }

/* Look for HERCULES.FS next to the binary, in ../Resources (app bundle), or in the working directory. */
static int find_data(char *out, size_t cap, const char *argv0, const char *override)
{
    if (override) { snprintf(out, cap, "%s", override); return file_exists(out); }
    char *base = SDL_GetBasePath();
    const char *cands[] = {"HERCULES.FS", "data/HERCULES.FS", "../Resources/HERCULES.FS", "../data/HERCULES.FS"};
    for (size_t i = 0; i < sizeof cands / sizeof cands[0]; i++) {
        if (base) { snprintf(out, cap, "%s%s", base, cands[i]); if (file_exists(out)) { SDL_free(base); return 1; } }
        snprintf(out, cap, "%s", cands[i]);
        if (file_exists(out)) { if (base) SDL_free(base); return 1; }
    }
    if (base) SDL_free(base);
    (void)argv0;
    return 0;
}

static int save_bmp(const Renderer *R, const char *path)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(R->fb, R->w, R->h, 32, R->w * 4, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return -1;
    int r = SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
    return r;
}

/* FRONTEND: TITLE.DLL level number -> a level this engine can run (ENG3 levels fall through to the next). */
static int restart_level(Game *G)
{
    char name[16];
    snprintf(name, sizeof name, "%s", G->L.name);
    return game_load_level(G, name);
}

static int start_engine_level(Game *G, int level)
{
    const char *name = game_level_name(level);         /* POLISH: TITLE level number -> engine level */
    if (!name) {
        const LevelInfo *best = NULL;
        for (int i = 0; i < NLEVELS; i++)
            if (LEVELS[i].index >= level && (!best || LEVELS[i].index < best->index)) best = &LEVELS[i];
        if (!best) return -1;
        name = best->name;
    }
    return game_load_level(G, name);
}

/* ---- POLISH: front end <-> game glue ---- */
static int level_ok(int level) { return game_level_name(level) != NULL; }

/* PSX pad word (demo recordings, ENG1 default masks) -> IN_* bits */
static int pad_to_in(int w)
{
    int in = 0;
    if (w & PSX_LEFT) in |= IN_LEFT;
    if (w & PSX_RIGHT) in |= IN_RIGHT;
    if (w & PSX_UP) in |= IN_UP;
    if (w & PSX_DOWN) in |= IN_DOWN;
    if (w & PSX_CROSS) in |= IN_JUMP;
    if (w & PSX_SQUARE) in |= IN_SWORD;
    if (w & PSX_CIRCLE) in |= IN_PUNCH;
    if (w & PSX_TRIANGLE) in |= IN_POWER;
    if (w & PSX_R1) in |= IN_WEAPON;
    return in;
}

/* Start the level the front end asked for with the run's lives / difficulty; demos start at a checkpoint. */
static int begin_level(Game *G, FrontEnd *F, Hud *hud, int lv)
{
    if (start_engine_level(G, lv)) return -1;
    G->P.lives = F->gs.lives > 0 ? F->gs.lives : 3;
    G->P.difficulty = F->gs.difficulty;
    G->P.max_health = 0x80;     /* PLAYER */
    if (F->gs.demo == 1 && G->have_objects && F->gs.checkpoint)
        for (int i = 0; i < G->O.nsp; i++) {
            const SpawnRec *r = &G->O.sp[i];
            if ((r->type & 0xfff0) == 0x4000 && r->type >= 0x400a && ((r->type + 6) & 15) == F->gs.checkpoint) {
                G->P.o.x = r->x; G->P.o.y = r->y - 0x80000; G->P.o.z = r->z;
                break;
            }
        }
    hud_reset(hud);
    return 0;
}

static FeStats level_stats(const Game *G, int deaths)
{
    FeStats st = {0};
    st.lives = G->P.lives; st.deaths = deaths;
    if (G->have_objects) {
        st.coins18 = G->O.coins18; st.coins14 = G->O.coins14; st.vases = G->O.letters;
        st.coins1a = G->O.coins18_total; st.coins16 = G->O.coins14_total;   /* MERGE: totals from solids */
        st.scrolls = G->O.scroll_bits;
    }
    if (G->L.eng3) {    /* ENG3B: ENG3 levels keep their own counters */
        st.coins18 = G->E3.coins; st.coins14 = G->E3.coins2; st.vases = G->E3.vases; st.scrolls = G->E3.scrolls;
    }
    return st;
}

/* --fe-script characters for headless front-end tests: one per tick */
static uint16_t script_pad(char c)
{
    switch (c) {
    case 'S': return PSX_START;
    case 'X': return PSX_CROSS;
    case 'T': return PSX_TRIANGLE;
    case 'U': return PSX_UP;
    case 'D': return PSX_DOWN;
    case 'L': return PSX_LEFT;
    case 'R': return PSX_RIGHT;
    default: return 0;
    }
}

int main(int argc, char **argv)
{
    const char *data = NULL, *level = NULL, *shot = NULL, *fe_shot = NULL;
    int scale = 3, shot_x = -1, shot_frames = 0, debug = 0, dev = 0, fe_mode = -1;
    int shot_every = 30, test_ammo = 0, pole_x = -1, pole_y = 0;
    static int32_t test_pole[1][3];        /* COMBAT: --every N = state log interval */
    const char *dumpwav = NULL;   /* SOUND: */
    const char *script = "";    /* per-tick input for --shot: R/L/J/P/. characters, repeated */
    const char *fe_script = "";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) level = argv[++i];
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--fe-shot") && i + 1 < argc) fe_shot = argv[++i];
        else if (!strcmp(argv[i], "--fe-script") && i + 1 < argc) fe_script = argv[++i];
        else if (!strcmp(argv[i], "--fe-start") && i + 1 < argc) fe_mode = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--x") && i + 1 < argc) shot_x = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) shot_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--debug")) debug = 1;
        else if (!strcmp(argv[i], "--dev")) dev = 1;
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) shot_every = atoi(argv[++i]);   /* COMBAT */
        else if (!strcmp(argv[i], "--ammo") && i + 1 < argc) test_ammo = atoi(argv[++i]);     /* COMBAT */
        else if (!strcmp(argv[i], "--pole") && i + 1 < argc) sscanf(argv[++i], "%d,%d", &pole_x, &pole_y);
        else if (!strcmp(argv[i], "--sfxlog")) sfx_set_log(stdout);                     /* SOUND: */
        else if (!strcmp(argv[i], "--dumpwav") && i + 1 < argc) dumpwav = argv[++i];     /* SOUND: */
    }
    if (scale < 1) scale = 1;
    if (scale > 6) scale = 6;

    char fspath[PATH_MAX];
    if (!find_data(fspath, sizeof fspath, argv[0], data)) {
        fprintf(stderr, "HERCULES.FS not found (use --data /path/to/HERCULES.FS)\n");
        return 1;
    }
    Fs fs;
    if (fs_open(&fs, fspath)) { fprintf(stderr, "cannot read %s\n", fspath); return 1; }

    char fsdir[PATH_MAX];
    snprintf(fsdir, sizeof fsdir, "%s", fspath);
    char *slash = strrchr(fsdir, '/');
    if (slash) *slash = 0; else snprintf(fsdir, sizeof fsdir, ".");
    char eng1[PATH_MAX + 16];
    snprintf(eng1, sizeof eng1, "%s/ENG1.DLL", fsdir);
    static Game G;
    if (game_init(&G, &fs, eng1, scale)) return 1;
    G.debug = debug;

    static InputConfig cfg;
    input_load(&cfg);
    static FrontEnd F;
    fe_init(&F, &fs, fsdir, &cfg);
    F.dev = dev;
    F.level_ok = level_ok;     /* POLISH */

    if (shot) {     /* headless gameplay screenshot (unchanged test path) */
        if (game_load_level(&G, level ? level : "THEBES")) return 1;
        if (shot_x >= 0) game_place_player(&G, shot_x);
        if (shot_x >= 0 && getenv("PLACE_Z")) G.P.o.z = G.P.o.pz = atoi(getenv("PLACE_Z")) * 65536;   /* DEPTH: test hook */
        if (getenv("LIGHT_DUMP")) {   /* RENDERFIX: attribute lighting fields */
            const Level *L = &G.L; int hist[16][4] = {{0}}; int used[4096] = {0};
            printf("lighttab off %ld end %u nattr %d\n", (long)(L->lighttab - L->d), L->lighttab_end, L->nattr);
            for (int a = 0; a < L->nattr; a++) { int lt = L->attr[4 * a + 2] | L->attr[4 * a + 3] << 8; hist[lt & 7][(lt >> 3) & 1]++; used[(lt >> 4) & 0xfff]++; }
            for (int r = 0; r < 8; r++) printf(" ramp %d: smooth %d flat %d\n", r, hist[r][0], hist[r][1]);
            int shown = 0; for (int a = 0; a < L->nattr && shown < 24; a++) { int lt = L->attr[4 * a + 2] | L->attr[4 * a + 3] << 8; if (!(a % 7) || 1) { int ix = lt >> 4; printf(" attr %d seg %d light %04x -> ramp %d flat %d tabidx %d tab %02x %02x %02x %02x flatidx %d\n", a, L->attr[4*a] | L->attr[4*a+1] << 8, lt, lt & 7, (lt >> 3) & 1, ix, L->lighttab[ix * 4], L->lighttab[ix * 4 + 1], L->lighttab[ix * 4 + 2], L->lighttab[ix * 4 + 3], (lt & 0x3fc0) >> 6); shown++; } }
        }
        if (getenv("OTB_CORR")) {   /* RENDERFIX: otb vs the face's mean z offset from the cell origin, in slots (z/2) */
            double sd = 0, sd2 = 0; long n = 0; int hist[40] = {0};
            for (int i = 0; i < G.L.nfaces; i++) { const Face *F = &G.L.faces[i];
                float mz = (F->p[0].z + F->p[1].z + F->p[2].z + F->p[3].z) * 0.25f;
                float pred = 64.0f + (-(mz - F->org.z)) / 2.0f;     /* straight segment facing -z */
                float e = (float)F->otb - pred; sd += e; sd2 += e * e; n++; int b = (int)floorf(e / 4) + 20; if (b < 0) b = 0; if (b > 39) b = 39; hist[b]++; }
            printf("otb - predicted: mean %.2f rms %.2f n %ld\n", sd / n, sqrt(sd2 / n), n);
            for (int b = 0; b < 40; b++) if (hist[b]) printf("  [%d..%d) %d\n", (b - 20) * 4, (b - 19) * 4, hist[b]);
        }
        if (getenv("SEMI_STATS")) {
            int n = 0, hist[8] = {0}; for (int i = 0; i < G.L.nfaces; i++) if (G.L.faces[i].flags & 0x20) { n++; hist[G.L.faces[i].light >> 5]++; }
            printf("semi faces %d of %d; light [0-31 %d, 32-63 %d, 64-95 %d, 96-127 %d, 128-159 %d, 160+ %d %d]\n", n, G.L.nfaces, hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], hist[6] + hist[7]);
        }
        if (getenv("OTB_HIST")) {   /* RENDERFIX: face ordering-table bias byte histogram per strip */
            for (int st = 0; st < G.L.nstrips; st++) {
                int h[8] = {0}, mn = 999, mx = -1, n = 0;
                for (int c = 0; c < G.L.ncols; c++) { int bi = st * G.L.ncols + c; for (int k = 0; k < G.L.bcount[bi]; k++) { int o = G.L.faces[G.L.bstart[bi] + k].otb; h[o >> 5]++; if (o < mn) mn = o; if (o > mx) mx = o; n++; } }
                printf("strip %d z=%d faces=%d otb min %d max %d  [0-31 %d, 32-63 %d, 64-95 %d, 96-127 %d, 128-159 %d, 160-191 %d, 192-223 %d, 224+ %d]\n", st, G.L.strip_z[st], n, mn, mx, h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);
            }
        }
        if (getenv("ATTR_SCAN")) {   /* DEPTH: dump attribute flags 0x30 / solid histogram */
            int hist[128] = {0};
            for (int st = 0; st < G.L.nstrips; st++) for (int c = 0; c < G.L.ncols; c++) {
                int lo = 99, hi = -1, last = -1;
                for (int r = 0; r < G.L.nrows; r++) {
                    int a = (level_cell(&G.L, st, c, r) >> 24) & 0x7f;
                    if (a) hist[a]++;
                    (void)lo; (void)hi; (void)last;
                }
            }
            if (getenv("ATTR_WHERE")) { int w = atoi(getenv("ATTR_WHERE")); for (int st = 0; st < G.L.nstrips; st++) for (int c = 0; c < G.L.ncols; c++) for (int r = 0; r < G.L.nrows; r++) if (((level_cell(&G.L, st, c, r) >> 24) & 0x7f) == w) printf("  A%d strip %d col %d row %d (z~%d x~%d y~%d)\n", w, st, c, r, st * 128, c * 64, r * 64 - 32); }
            if (getenv("CELL_DUMP")) { int c0 = atoi(getenv("CELL_DUMP")); for (int st = 0; st < G.L.nstrips; st++) { for (int c = c0; c < c0 + 8; c++) { printf("S%d C%d:", st, c); for (int r = 8; r < G.L.nrows; r++) { uint32_t q = level_cell(&G.L, st, c, r); printf(" %c%03x/%02x", q >> 31 ? 'a' : '-', (q >> 1) & 0x3ff, (q >> 24) & 0x7f); } printf("\n"); } } }
            for (int a = 1; a < 128; a++) if (hist[a]) printf("attr %d cells=%d flags=%x\n", a, hist[a], G.L.flags[a]);
            if (getenv("BLOCK_DUMP")) { int b = (int)strtol(getenv("BLOCK_DUMP"), 0, 0); const Level *L = &G.L;
                uint32_t bt = L->blocks, ea = bt - 4 + (uint32_t)b * 4; int i = (int16_t)(L->d[ea] | L->d[ea+1] << 8) + b; uint32_t a = bt - 4 + (uint32_t)i * 4;
                int nv = L->d[a + 2]; int mnx = 999, mxx = -999, mny = 999, mxy = -999, mnz = 999, mxz = -999;
                for (int k = 0; k < nv; k++) { uint32_t v = L->d[a+4+4*k] | L->d[a+5+4*k] << 8 | L->d[a+6+4*k] << 16 | (uint32_t)L->d[a+7+4*k] << 24;
                    int vx = (int8_t)(v & 0xff), vy = (int16_t)((v >> 8) & 0xffff), vz = (int8_t)(v >> 24);
                    if (vx < mnx) mnx = vx; if (vx > mxx) mxx = vx; if (vy < mny) mny = vy; if (vy > mxy) mxy = vy; if (vz < mnz) mnz = vz; if (vz > mxz) mxz = vz; }
                printf("block %d: nv=%d x %d..%d y %d..%d z %d..%d\n", b, nv, mnx, mxx, mny, mxy, mnz, mxz); }
            if (getenv("MODEL_DUMP")) { int mi = atoi(getenv("MODEL_DUMP")); const Level *L = &G.L; uint32_t r = model_rec(L, mi);
                int nbox = L->d[r + 2]; uint32_t g = r + 10 + 8u * nbox; int nv = (int16_t)(L->d[g] | L->d[g+1] << 8);
                printf("model %d rec=%x nbox=%d off=%08x nv=%d\n", mi, r, nbox, L->d[r+4]|L->d[r+5]<<8|L->d[r+6]<<16|(uint32_t)L->d[r+7]<<24, nv);
                for (int k = 0; k < nv && k < 40; k++) { uint32_t q = L->d[g+2+4*k] | L->d[g+3+4*k] << 8 | L->d[g+4+4*k] << 16 | (uint32_t)L->d[g+5+4*k] << 24;
                    printf("  v%d: b0=%d s16=%d b3=%d\n", k, (int8_t)q, (int16_t)(q >> 8), (int8_t)(q >> 24)); } }
            { printf("strip_z:"); for (int i = 0; i < G.L.nstrips; i++) printf(" %d", G.L.strip_z[i]); printf("\n"); }
            printf("nstrips=%d ncols=%d nrows=%d\n", G.L.nstrips, G.L.ncols, G.L.nrows);
        }
        if (getenv("HERC_AUDIO") && SDL_Init(SDL_INIT_AUDIO) == 0) audio_init();     /* AUDSHELL: exercise the voice / tracking logic headless (dummy driver) */
        if (dumpwav) { printf("dumped %d samples to %s\n", sfx_dump_wavs(dumpwav), dumpwav); return 0; }  /* SOUND: */
        if (getenv("ATTR_Q")) { int qx, qy, qz; sscanf(getenv("ATTR_Q"), "%d,%d,%d", &qx, &qy, &qz);
            for (int zz = qz - 192; zz <= qz + 192; zz += 32) { int u, d, in; level_probe(&G.L, qx, qy, zz, &u, &d, &in);
                printf("Q hit=%04x z=%d level_attr=%d objattr=%d probe up=%d down=%d inside=%d\n", G.have_objects && G.O.hit_obj ? G.O.hit_obj->type : 0, zz, level_attr(&G.L, qx, qy, zz), G.have_objects ? objects_attr(&G.O, qx, qy, zz) : -9, u, d, in); } }
        if (getenv("CP_LIST") && G.have_objects) for (int i = 0; i < G.O.nsp; i++) { const SpawnRec *r = &G.O.sp[i];
            if ((r->type & 0xfff0) == 0x4000 && r->type >= 0x400a) printf("CP type %04x id %d at %d,%d,%d\n", r->type, (r->type + 6) & 15, r->x >> 16, r->y >> 16, r->z >> 16); }
        for (int w = 1; w < 5; w++) G.P.ammo[w] = test_ammo;     /* COMBAT: --ammo N test hook */
        size_t sl = strlen(script);
        for (int i = 0; i < shot_frames; i++) {
            int in = 0;
            if (sl) {
                char c = script[(size_t)i % sl];
                in = c == 'R' ? IN_RIGHT : c == 'L' ? IN_LEFT : c == 'J' ? IN_JUMP : c == 'P' ? IN_PUNCH :
                     c == 'r' ? IN_RIGHT | IN_JUMP : c == 'l' ? IN_LEFT | IN_JUMP : 0;
                /* COMBAT: S sword, T charge punch, D down, d down+punch, j jump+sword, k jump+punch,
                 * u jump+down, H / K = take a light / heavy hit from the right (test hook) */
                if (c == 'S') in = IN_SWORD;
                if (c == 'T') in = IN_POWER;
                if (c == 'D') in = IN_DOWN;
                if (c == 'd') in = IN_DOWN | IN_PUNCH;
                if (c == 'j') in = IN_JUMP | IN_SWORD;
                if (c == 'k') in = IN_JUMP | IN_PUNCH;
                if (c == 'u') in = IN_JUMP | IN_DOWN;
                if (c == 'W') in = IN_WEAPON;
                if (c == 'G') { G.P.touch_obj = &G; G.P.touch_type = 2; }   /* pressing against a crate */
                if (c == 'U') in = IN_UP;                      /* OBJECTS: plane switch up */
                if (c == 'A') in = IN_SWORD | IN_UP;
                if (c == 'H' || c == 'K')
                    player_damage(&G.P, c == 'H' ? 2 : 7, G.P.o.x + 0x140000, G.P.o.y, G.P.o.z, 0);
            }
            if (pole_x >= 0) {      /* COMBAT: --pole x,y test hook (the object system registers real poles) */
                test_pole[0][0] = pole_x << 16; test_pole[0][1] = pole_y << 16; test_pole[0][2] = G.P.o.z;
                G.P.poles = (const int32_t (*)[3])test_pole; G.P.npoles = 1;
            }
            if (getenv("FADE_TEST")) G.fade_on = 1;
            if (getenv("PKILL") && i == atoi(getenv("PKILL"))) G.P.health = 0;   /* PLAYER: test hook, kill Hercules at frame N */
            if (getenv("HIT_TPX") && i == 20) { G.P.o.x = atoi(getenv("HIT_TPX")) << 16; G.P.o.y -= 0x100000; if (getenv("HIT_TPZ")) G.P.o.z = atoi(getenv("HIT_TPZ")) << 16; if (getenv("HIT_TPY")) G.P.o.y = atoi(getenv("HIT_TPY")) << 16; }   /* COMBATFIX: test teleport (keeps the depth plane) */
            if (getenv("HIT_SPAWN") && i == 40 && G.have_objects) {   /* COMBATFIX: HIT_SPAWN=type,dx[,dz] spawns an object in front of Hercules */
                unsigned ty; int dx = 80, dz = 0, dy = 0; sscanf(getenv("HIT_SPAWN"), "%x,%d,%d,%d", &ty, &dx, &dz, &dy);
                Object *so = obj_spawn(&G.O, (uint16_t)ty, G.P.o.x + (dx << 16), G.P.o.y + (dy << 16), G.P.o.z + (dz << 16));
                if (so) { so->facing = dx > 0 ? 0xff : 0; so->flags54 = (so->flags54 & ~0x10u) | (so->facing & 0x10); }
            }
            game_tick(&G, in);
            if (getenv("INSIDE_LOG") && G.have_objects) {   /* DEPTH: audit hook, player body inside solid geometry? */
                int px = G.P.o.x >> 16, py = G.P.o.y >> 16, pz = G.P.o.z >> 16, up, dn, ins;
                level_probe(&G.L, px, py - 4, pz, &up, &dn, &ins);
                int oa = objects_attr(&G.O, px, py - 24, pz), os = oa >= 0 && (G.L.flags[oa & 127] & 1) && !(G.L.flags[oa & 127] & 0x8000);
                int la = level_attr(&G.L, px, py - 24, pz), ls = la > 0 && (G.L.flags[la] & 1);
                if (ins || os || ls) printf("INSIDE t=%d x=%d y=%d z=%d probe_inside=%d obj=%d(type %04x) lvl=%d state=%d\n", i + 1, px, py, pz, ins, os, G.O.hit_obj ? G.O.hit_obj->type : 0, ls, G.P.state);
            }
            if (getenv("BF_SWEEP") && (i + 1) % atoi(getenv("BF_SWEEP")) == 0) {   /* BLACKFIX: mean luminance sampler */
                game_draw(&G);
                double sum = 0; long n = (long)G.R.w * G.R.h;
                for (long k = 0; k < n; k++) { uint32_t c = G.R.fb[k]; sum += 0.299 * (c >> 16 & 255) + 0.587 * (c >> 8 & 255) + 0.114 * (c & 255); }
                printf("LUM t=%d %.1f\n", i + 1, sum / n);
            }
            if (getenv("LEVEL_DONE_LOG") && game_level_done(&G)) { printf("LEVEL DONE at t=%d next=%s\n", i + 1, game_next_level(G.L.name) ? game_next_level(G.L.name) : "(end)"); break; }   /* LEVELS */
            if (getenv("SHOT_SEQ") && (i + 1) % shot_every == 0) {   /* DOLLS: frame-by-frame BMPs SHOT_SEQ=prefix */
                char nm[512]; game_draw(&G); snprintf(nm, sizeof nm, "%s%04d.bmp", getenv("SHOT_SEQ"), i + 1); save_bmp(&G.R, nm);
            }
            if ((i + 1) % shot_every == 0) {
                Box ab;
                int k = player_attack_box(&G.P, &ab);
                printf("t=%d x=%d y=%d z=%d state=%d frame=%d bank=%d hp=%d lives=%d hit=%x", i + 1, G.P.o.x >> 16,
                       G.P.o.y >> 16, G.P.o.z >> 16, G.P.state, G.P.o.frame, G.P.o.bank, G.P.health, G.P.lives, k);
                if (getenv("WALL_DBG") && G.have_objects) { int a = objects_attr(&G.O, (G.P.o.x>>16)+30, (G.P.o.y>>16)-8, G.P.o.z>>16); printf(" WALL attr=%d hit=%04x", a, G.O.hit_obj ? G.O.hit_obj->type : 0); }
                if (k) printf(" box=(%d,%d)-(%d,%d)", ab.x0, ab.y0, ab.x1, ab.y1);
                if (G.P.o.flags54 & 0x800000) printf(" overlay=%d", G.P.child_frame);
                if (G.P.weapon) printf(" wpn=%d ammo=%d", G.P.weapon, G.P.ammo[G.P.weapon]);
                if (G.P.flame.active) printf(" flame=(%d,%d)a%d", G.P.flame.x >> 16, G.P.flame.y >> 16, G.P.flame.angle);
                for (int k = 0; k < G.P.nspawns; k++) printf(" spawn=%x", G.P.spawns[k].id);
                printf("\n");
                if (G.have_objects) {   /* OBJECTS */
                    int live = 0; for (int k = 0; k < OBJ_MAX; k++) live += G.O.obj[k].used;
                    printf("  objects live=%d coins=%d/%d health=%d checkpoint=%d end=%d won=%d sig=%x\n", live, G.O.coins18, G.O.coins14, G.O.health, G.O.checkpoint, G.O.level_end, G.P.won, G.P.signal);
                }
            }
        }
        game_draw(&G);
        if (getenv("SOLID_AUDIT") && G.have_objects) solids_audit(&G.O, &G.R);
        if (getenv("OBJ_DEBUG") && G.have_objects)   /* OBJECTS: list live objects */
            for (int k = 0; k < OBJ_MAX; k++) if (G.O.obj[k].used) {
                const Object *ob = &G.O.obj[k];
                printf("  obj %3d type %04x kind %d pos %d,%d,%d bank %d frame %d script %08x anim %d\n", k, ob->type, ob->kind,
                       ob->x >> 16, ob->y >> 16, ob->z >> 16, ob->bank, ob->frame, ob->script, ob->anim); if (ob->type==0x4031||ob->type==0x3f||ob->type==0x46||ob->type==0x202c) printf("     scale=%d rot=%d,%d,%d s98=%d d=%d,%d,%d flags54=%x flags50=%x attr=%d\n", ob->scale, ob->rot[0], ob->rot[1], ob->rot[2], ob->s98, ob->d[0], ob->d[1], ob->d[2], ob->flags54, ob->flags50, ob->attr);
            }
        int r = save_bmp(&G.R, shot);
        printf("faces=%d items drawn, saved %s (%d)\n", G.L.nfaces, shot, r);
        return r ? 1 : 0;
    }

    if (fe_shot) {  /* headless front-end screenshot: --fe-start 0 boot / 1 menu / 2 password / 3 props */
        if (fe_mode <= 0) fe_boot(&F);
        else if (fe_mode == 1) fe_goto_menu(&F);
        else if (fe_mode == 2) { fe_goto_menu(&F); F.entered = 1; F.ms = 7; F.ff0 = 0; }
        else if (fe_mode == 3) { fe_goto_menu(&F); fe_tick(&F, 0); fe_open_props(&F, FE_MENU); }
        else if (fe_mode >= 10) fe_card(&F, fe_mode - 10);
        /* POLISH test entries: 4 tally (Thebes), 5 tally + password reward + all scrolls, 6 Titans on easy
         * (tally -> credits), 7 cheat screen, 8 game over (continue prompt), 9 attract mode */
        else if (fe_mode >= 4 && fe_mode <= 9) {
            FeStats st = {3, 1, 40, 60, 10, 20, 0, 0x5b};
            F.gs.level = fe_mode == 6 ? 9 : 5; F.gs.difficulty = fe_mode == 6 ? 0 : 1; F.gs.lives = 3; F.gs.start_lives = 3;
            if (fe_mode == 5) { st.vases = 0xf; st.scrolls = 0xff; }
            if (fe_mode == 4 || fe_mode == 5 || fe_mode == 6) fe_level_end(&F, FE_RES_DONE, &st);
            else if (fe_mode == 7) { F.gs.cheats |= 0x80000000u; fe_goto_menu(&F); fe_tick(&F, 0); F.entered = 1; F.ms = 4; F.ff0 = 4; }
            else if (fe_mode == 8) { F.gs.continues = 1; fe_level_end(&F, FE_RES_GAMEOVER, &st); }
            else { F.entered = 1; fe_goto_menu(&F); fe_tick(&F, 0); F.a038 = 898; }
        }
        size_t sl = strlen(fe_script);
        for (int i = 0; i < shot_frames; i++) {
            fe_tick(&F, sl ? script_pad(fe_script[(size_t)i % sl]) : 0);
            if (F.mode == FE_PLAY) break;
            if (getenv("FE_TRACE") && i % 30 == 0) printf("t=%d mode=%d ms=%d flow=%d level=%d\n", i, F.mode, F.ms, F.flow, F.gs.level);   /* POLISH */
        }
        printf("front end mode %d (menu state %d, a07c %d, sel %d), start_level %d\n", F.mode, F.ms, F.a07c, F.a030, F.start_level);
        if (F.mode == FE_PLAY && F.start_level) {
            if (start_engine_level(&G, F.start_level)) return 1;
            game_draw(&G);
            static Hud sh; static HudState ss;
            hud_init(&sh, &fs);
            hud_set_eng1(&sh, &G.eng1);     /* POLISH: HUD test (HERC_HUD_TEST=1 boss bar + charge meters) */
            ss.lives = 3; ss.timer_ticks = -1;
            if (getenv("HERC_HUD_TEST")) {
                ss.health = 0x50; ss.max_health = 0x80; ss.weapon = 2; ss.weapon_ammo = 0x90; ss.charge = 0xc0;
                ss.boss_cmd = HUD_BOSS_START;
                for (int t = 0; t < 80; t++) { hud_tick(&sh, &ss); ss.boss_cmd = 0; ss.boss_damage = t == 60 ? 0x60 : 0; }
            } else for (int t = 0; t < 4; t++) hud_tick(&sh, &ss);
            hud_draw(&sh, &G.R, &G.L, &ss);
            if (getenv("HERC_SHOT_PAUSE")) hud_draw_pause(&sh, &G.R, &G.L, atoi(getenv("HERC_SHOT_PAUSE")));
        } else fe_draw(&F, &G.R);
        return save_bmp(&G.R, fe_shot) ? 1 : 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    audio_init();
    game_load_sounds(&G);
    SDL_Window *win = SDL_CreateWindow("Disney's Hercules  [build " __DATE__ " " __TIME__ "]", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       G.R.w, G.R.h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, G.R.w, G.R.h);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, G.R.w, G.R.h);
    SDL_GameController *pad = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }

    /* FRONTEND: boot into the original startup flow unless --level asks for a level directly */
    if (level) {
        if (game_load_level(&G, level)) return 1;
        F.mode = FE_PLAY;
    } else fe_boot(&F);
    int current_level = 0, help = 0, deaths = 0, was_dead = 0, prev_lives = 0;   /* POLISH */
    static Hud hud;
    hud_init(&hud, &fs);
    hud_set_eng1(&hud, &G.eng1);    /* POLISH */
    static HudState hud_state;      /* FRONTEND: filled by combat at merge (health/max/lives/coins/gifts) */
    int pause_state = PAUSE_CONTINUE;

    const double tick = 1.0 / 30.0;     /* the PSX game logic runs at 30 Hz */
    double acc = 0;
    Uint64 last = SDL_GetPerformanceCounter();
    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Keycode k = e.key.keysym.sym;
                fe_key(&F, e.key.keysym.scancode);
                if (k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)) {
                    Uint32 fl = SDL_GetWindowFlags(win);
                    SDL_SetWindowFullscreen(win, fl & SDL_WINDOW_FULLSCREEN_DESKTOP ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                } else if (F.mode == FE_PLAY) {
                    if (k == SDLK_ESCAPE) { F.mode = FE_PAUSE; pause_state = PAUSE_CONTINUE; sfx_play(0x339, 0, NULL); }   /* AUDSHELL: FUN_10002290 state 0x40 */
                    else if (k == SDLK_F1 || k == SDLK_h) help ^= 1;      /* H/O/R: aliases for Macs whose F-row sends media keys */
                    else if (k == SDLK_F2 || k == SDLK_o) fe_open_props(&F, FE_PLAY);
                    else if ((k == SDLK_F3 || k == SDLK_r) && G.have_level) restart_level(&G);
                    else if (dev && k >= SDLK_1 && k < SDLK_1 + NLEVELS) game_load_level(&G, LEVELS[k - SDLK_1].name);
                    else if (dev && k == SDLK_F5) G.debug ^= 1;
                    else if (dev && k == SDLK_F6) G.freecam ^= 1;
                    else if (dev && k == SDLK_F12) save_bmp(&G.R, "hercules_shot.bmp");
                } else if (F.mode == FE_PAUSE) {
                    /* ENG1 pause (FUN_10001f40): CONTINUE / QUIT, then ARE YOU SURE? NO / YES */
                    int up = k == SDLK_UP || e.key.keysym.scancode == cfg.key[ACT_UP];
                    int down = k == SDLK_DOWN || e.key.keysym.scancode == cfg.key[ACT_DOWN];
                    int ok = k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE || e.key.keysym.scancode == cfg.key[ACT_JUMP];
                    if (k == SDLK_ESCAPE) { F.mode = FE_PLAY; pause_state = PAUSE_CONTINUE; sfx_play(0x338, 0, NULL); }
                    else if (up || down) {
                        sfx_play(0x337, 0, NULL);       /* AUDSHELL: FUN_10002290 cursor move */
                        if (pause_state <= PAUSE_QUIT) pause_state = pause_state == PAUSE_CONTINUE ? PAUSE_QUIT : PAUSE_CONTINUE;
                        else pause_state = pause_state == PAUSE_SURE_NO ? PAUSE_SURE_YES : PAUSE_SURE_NO;
                    } else if (ok) {
                        sfx_play(0x338, 0, NULL);       /* AUDSHELL: FUN_10002290 select */
                        if (pause_state == PAUSE_CONTINUE) F.mode = FE_PLAY;
                        else if (pause_state == PAUSE_QUIT) pause_state = PAUSE_SURE_NO;
                        else if (pause_state == PAUSE_SURE_NO) pause_state = PAUSE_QUIT;
                        else { pause_state = PAUSE_CONTINUE; F.mode = FE_PLAY; fe_level_end(&F, FE_RES_QUIT, NULL); }   /* POLISH */
                    }
                    else if (k == SDLK_F3 || k == SDLK_r) { if (G.have_level) restart_level(&G); F.mode = FE_PLAY; pause_state = PAUSE_CONTINUE; }
                    else if (k == SDLK_F2 || k == SDLK_o) fe_open_props(&F, FE_PAUSE);
                } else if (F.mode == FE_PROPS) {
                    /* handled by fe_key / fe_tick */
                } else if (k == SDLK_F2 && F.mode == FE_MENU) fe_open_props(&F, FE_MENU);
                else if (k == SDLK_F3 && F.mode == FE_MENU) fe_load_game(&F);      /* POLISH: F3 load game */
                else if (k == SDLK_F1 && F.mode == FE_MENU) help ^= 1;             /* POLISH: F1 help on the menu */
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                fe_pad_button(&F, (SDL_GameControllerButton)e.cbutton.button);
                if (e.cbutton.button == cfg.pad[PADACT_PAUSE]) {
                    if (F.mode == FE_PLAY) { F.mode = FE_PAUSE; pause_state = PAUSE_CONTINUE; sfx_play(0x339, 0, NULL); }
                    else if (F.mode == FE_PAUSE) { F.mode = FE_PLAY; sfx_play(0x338, 0, NULL); }
                }
            } else if (e.type == SDL_CONTROLLERDEVICEADDED && !pad) pad = SDL_GameControllerOpen(e.cdevice.which);
        }
        sfx_set_options(cfg.sfx_on, cfg.music_on, cfg.volume);      /* AUDSHELL: Properties > Sound */
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        int in = input_game_bits(&cfg, ks, pad);
        uint16_t menu_pad = input_menu_pad(&cfg, ks, pad);

        Uint64 now = SDL_GetPerformanceCounter();
        acc += (double)(now - last) / SDL_GetPerformanceFrequency();
        last = now;
        if (acc > 0.25) acc = 0.25;
        while (acc >= tick) {
            if (F.mode == FE_PLAY) {
                if (F.start_level) {
                    int lv = F.start_level;
                    F.start_level = 0;
                    if (begin_level(&G, &F, &hud, lv)) { F.entered = 1; fe_goto_menu(&F); }
                    else { current_level = lv; deaths = 0; was_dead = 0; prev_lives = G.P.lives; }
                }
                if (G.have_level && F.mode == FE_PLAY) {
                    if (fe_wants_demo(&F)) {            /* POLISH: attract demo playback (ENG1 FUN_10006005) */
                        int w = fe_demo_pad(&F);
                        if (w < 0 || in || menu_pad) { fe_level_end(&F, FE_RES_QUIT, NULL); acc -= tick; continue; }
                        in = pad_to_in(w);
                    }
                    G.fade_on = 1;      /* PLAYER: in-game fade in / out */
                    G.P.cheat_lives = (F.gs.cheats & 8) != 0;
                    game_tick(&G, in);
                    hud_state.health = G.P.health; hud_state.max_health = G.P.max_health;     /* MERGE: combat -> HUD */
                    hud_state.lives = G.P.lives;
                    hud_state.coins = G.have_objects ? G.O.coins18 : 0;
                    hud_state.life_pickup = G.P.lives > prev_lives;    /* POLISH: orb / meters / timer */
                    prev_lives = G.P.lives;
                    hud_state.weapon = G.P.weapon;
                    hud_state.weapon_ammo = G.P.weapon > 0 && G.P.weapon < 5 ? G.P.ammo[G.P.weapon] : 0;
                    hud_state.charge = G.P.charge;
                    hud_state.timer_ticks = -1;
                    if (G.have_objects) {     /* MERGE: boss bar from objects (minotaur, bosses) */
                        if (G.O.hud_boss_cmd) hud_state.boss_cmd = G.O.hud_boss_cmd;
                        hud_state.boss_damage = G.O.hud_boss_damage;
                        G.O.hud_boss_cmd = 0; G.O.hud_boss_damage = 0;
                    }
                    hud_tick(&hud, &hud_state);
                    hud_state.boss_cmd = 0; hud_state.boss_damage = 0;
                    /* POLISH: level end -> TITLE flow (tally, story video, next level / continue / credits) */
                    if (G.P.dead && !was_dead) {
                        deaths++;
                        if ((F.gs.cheats & 8) && G.P.game_over) { G.P.game_over = 0; G.P.lives = 1; }
                    }
                    was_dead = G.P.dead;
                    if (F.gs.demo != 1 && game_level_done(&G)) {
                        FeStats st = level_stats(&G, deaths);
                        fe_level_end(&F, FE_RES_DONE, &st);
                    } else if (F.gs.demo != 1 && G.P.dead && G.P.game_over && G.dead_ticks > 40) {
                        FeStats st = level_stats(&G, deaths);
                        fe_level_end(&F, FE_RES_GAMEOVER, &st);
                    }
                }
            } else if (F.mode != FE_PAUSE) {
                fe_tick(&F, menu_pad);
                if (F.mode == FE_QUIT) running = 0;
            }
            acc -= tick;
        }
        if ((F.mode == FE_PLAY || F.mode == FE_PAUSE) && G.have_level) {
            game_draw(&G);
            hud_draw(&hud, &G.R, &G.L, &hud_state);
            if (F.mode == FE_PAUSE) hud_draw_pause(&hud, &G.R, &G.L, pause_state);
            if (help) fe_draw_help(&F, &G.R);
        } else {
            if (F.mode == FE_PROPS && F.props_return != FE_MENU && G.have_level) game_draw(&G);
            fe_draw(&F, &G.R);
            if (help && F.mode == FE_MENU) fe_draw_help(&F, &G.R);   /* POLISH */
        }
        (void)help; (void)current_level;
        SDL_UpdateTexture(tex, NULL, G.R.fb, G.R.w * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }
    input_save(&cfg);
    fe_free(&F);
    audio_shutdown();
    SDL_Quit();
    return 0;
}
