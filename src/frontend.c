#include "frontend.h"
#include "font5x7.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CW 320
#define CH 256
#define CX 160          /* object origin: TITLE.DLL DAT_1002cbe0/cbe4 */
#define CY 120          /* POLISH: the PC build centres title objects at (160,120) (FUN_1001d590) */

/* TITLE.DLL addresses of the tables used below */
#define T_MUSE0 0x10023af8u   /* muse animation tables (FUN_10008140), -1 terminated, loop at 0x17 */
#define T_MUSE1 0x10023668u
#define T_MUSE2 0x100238b0u
#define T_MUSE3 0x10023d40u
#define T_MUSE4 0x10023f88u
#define T_HRUN  0x10024230u   /* Hercules carry-in: run cycle (+0x16) */
#define T_HRUNY 0x100241a4u   /*   per-frame vertical offset of the board while running */
#define T_HLIFT 0x10024278u   /*   lift (+7) */
#define T_HLX   0x100241e2u   /*   per-frame board x/y while lifting */
#define T_HLY   0x10024202u
#define T_HDROP 0x100242b8u   /*   drop (+0x26) */
#define T_HFALL 0x100232b0u   /*   fall away loop (+0x26) */
#define T_SPARK 0x100242e8u   /* sparkle spawn points {int x, int y} x 24 */
#define T_PIDLE 0x10025890u   /* password muse: idle, hit (down/up) */
#define T_PHITU 0x100258b0u
#define T_PHITD 0x10025900u
#define T_PWALK 0x100257e8u   /* walk-in x steps (byte table) */
#define T_PSWD  0x10025a30u   /* passwords: {int v0,v1,v2,v3, code, difficulty} x 42 */
#define T_CARD  0x10025f50u   /* pre-level card muse animation */

static const char *SCR = "M:\\GRAFIX\\MOVIE\\SCREENS\\";

/* POLISH: implemented in fe_flow.inc / fe_pswd.inc (included at the end of this file) */
static void flow_run(FrontEnd *F, int st);
static void new_game(FrontEnd *F);
static void cheats_enter(FrontEnd *F);
static void tally_tick(FrontEnd *F);
static void tally_draw(FrontEnd *F);
static void credits_tick(FrontEnd *F);
static void cheats_tick(FrontEnd *F);
static void cheats_draw(FrontEnd *F);
static void fe_snd(FrontEnd *F, int id);
static void fe_title_sounds(FrontEnd *F, int tally);
static int speech(FrontEnd *F, int n);
static void speech_stop(FrontEnd *F);
static int mouth(FrontEnd *F);
static int rand_n(FrontEnd *F, int n);
static int fe_level_ported(FrontEnd *F, int level) { return !F->level_ok || F->level_ok(level); }

/* ------------------------------------------------------------------ helpers */

static int rnd(FrontEnd *F, int n)
{
    F->rng = F->rng * 0x343FD + 0x269EC3;
    int r = (int)((F->rng >> 16) & 0x7fff);
    return n > 0 ? r % n : 0;
}

static int16_t tbl(const FrontEnd *F, uint32_t va, int i) { return pe_s16(&F->title, va + 2u * (uint32_t)i); }

static uint32_t *tga_load(const FrontEnd *F, const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "%s%s", strchr(name, ':') ? "" : SCR, name);
    uint32_t sz;
    const uint8_t *b = fs_get(F->fs, path, &sz);
    if (!b || sz < 18 + 768) return NULL;
    int idl = b[0], cml = b[5] | b[6] << 8, w = b[12] | b[13] << 8, h = b[14] | b[15] << 8;
    if (b[2] != 1 || b[16] != 8 || w != CW || h != CH) return NULL;
    const uint8_t *pal = b + 18 + idl, *px = pal + cml * 3;
    if ((size_t)(px - b) + (size_t)w * h > sz) return NULL;
    uint32_t *img = malloc(sizeof(uint32_t) * CW * CH);
    uint32_t lut[256] = {0};
    for (int i = 0; i < cml && i < 256; i++)
        lut[i] = 0xff000000u | (uint32_t)pal[i * 3 + 2] << 16 | (uint32_t)pal[i * 3 + 1] << 8 | pal[i * 3];
    /* rows are stored top-down despite the header's origin bit (see tools/assets.py) */
    for (int i = 0; i < CW * CH; i++) img[i] = lut[px[i]];
    return img;
}

static inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* PSX texture modulation + semi-transparency. mode -1 = opaque, 0 = avg, 1 = add, 2 = sub, 3 = add 1/4. */
static inline uint32_t psx_px(uint32_t dst, uint32_t src, int bright, int mode)
{
    int r = (int)((src >> 16) & 255) * bright >> 7, g = (int)((src >> 8) & 255) * bright >> 7, b = (int)(src & 255) * bright >> 7;
    r = clamp255(r); g = clamp255(g); b = clamp255(b);
    if (mode >= 0) {
        int dr = (dst >> 16) & 255, dg = (dst >> 8) & 255, db = dst & 255;
        switch (mode) {
        case 0: r = (dr + r) >> 1; g = (dg + g) >> 1; b = (db + b) >> 1; break;
        case 1: r = clamp255(dr + r); g = clamp255(dg + g); b = clamp255(db + b); break;
        case 2: r = clamp255(dr - r); g = clamp255(dg - g); b = clamp255(db - b); break;
        default: r = clamp255(dr + r / 4); g = clamp255(dg + g / 4); b = clamp255(db + b / 4); break;
        }
    }
    return 0xff000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

static void draw_bg(FrontEnd *F, const uint32_t *img, int bright, int xo, int yo)
{
    if (!img) return;
    if (bright > 255) bright = 0;   /* FUN_10002090: >0xff treated as 0 */
    for (int y = 0; y < CH; y++) {
        int sy = y - yo;
        if (sy < 0 || sy >= CH) continue;
        for (int x = 0; x < CW; x++) {
            int sx = x - xo;
            if (sx < 0 || sx >= CW) continue;
            F->cv[y * CW + x] = psx_px(0, img[sy * CW + sx], bright, -1);
        }
    }
}

static void fill_rect(FrontEnd *F, int x0, int y0, int w, int h, uint32_t rgb, int bright, int mode)
{
    for (int y = y0; y < y0 + h; y++) {
        if (y < 0 || y >= CH) continue;
        for (int x = x0; x < x0 + w; x++) {
            if (x < 0 || x >= CW) continue;
            F->cv[y * CW + x] = psx_px(F->cv[y * CW + x], rgb, bright, mode);
        }
    }
}

/* Draw a sprite frame with its hotspot at object position (x, y) (relative to the screen centre),
 * like FUN_10003100/FUN_10003420: brightness 0x80 = 1.0, optional mirror and rotation (0x400 = 360). */
static void draw_spr(FrontEnd *F, const Image *img, int x, int y, int bright, int mode, int mirror, int rot)
{
    if (!img) return;
    if (bright > 255) bright = 255;
    int hx = CX + x, hy = CY + y;
    if (!rot) {
        for (int sy = 0; sy < img->h; sy++) {
            int dy = hy - img->oy + sy;
            if (dy < 0 || dy >= CH) continue;
            for (int sx = 0; sx < img->w; sx++) {
                uint32_t c = img->px[sy * img->w + sx];
                if (!(c >> 24)) continue;
                int dx = mirror ? hx + img->ox - 1 - sx : hx - img->ox + sx;
                if (dx < 0 || dx >= CW) continue;
                F->cv[dy * CW + dx] = psx_px(F->cv[dy * CW + dx], c, bright, mode);
            }
        }
        return;
    }
    double a = rot * (2 * M_PI / 1024.0), cs = cos(a), sn = sin(a);
    int R = img->w + img->h;
    for (int dy = hy - R; dy <= hy + R; dy++) {
        if (dy < 0 || dy >= CH) continue;
        for (int dx = hx - R; dx <= hx + R; dx++) {
            if (dx < 0 || dx >= CW) continue;
            double ux = dx - hx, uy = dy - hy;
            double lx = cs * ux + sn * uy, ly = -sn * ux + cs * uy;   /* inverse rotation */
            int sx = (int)floor(mirror ? img->ox - 1 - lx : lx + img->ox), sy = (int)floor(ly + img->oy);
            if (sx < 0 || sy < 0 || sx >= img->w || sy >= img->h) continue;
            uint32_t c = img->px[sy * img->w + sx];
            if (!(c >> 24)) continue;
            F->cv[dy * CW + dx] = psx_px(F->cv[dy * CW + dx], c, bright, mode);
        }
    }
}

static const Image *frame(Bank *b, int i) { return b->d ? bank_frame(b, i) : NULL; }

/* 5x7 text (used by the Properties sheet and pause overlay) */
static void text(FrontEnd *F, int x, int y, const char *s, uint32_t rgb)
{
    for (; *s; s++, x += 6) {
        int c = (unsigned char)*s;
        if (c < 32 || c > 126) c = '?';
        const uint8_t *g = FONT5X7[c - 32];
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (g[col] >> row & 1) {
                    int px = x + col, py = y + row;
                    if (px >= 0 && py >= 0 && px < CW && py < CH) F->cv[py * CW + px] = rgb;
                }
    }
}
static int text_w(const char *s) { return (int)strlen(s) * 6 - 1; }

/* ------------------------------------------------------------------ init */

int fe_init(FrontEnd *F, const Fs *fs, const char *fsdir, InputConfig *cfg)
{
    memset(F, 0, sizeof *F);
    F->fs = fs;
    F->cfg = cfg;
    F->rng = 1;
    snprintf(F->fsdir, sizeof F->fsdir, "%s", fsdir);
    char p[1100];
    snprintf(p, sizeof p, "%s/TITLE.DLL", fsdir);
    if (pe_load(&F->title, p)) fprintf(stderr, "warning: %s not found; title animations disabled\n", p);
    F->menub = tga_load(F, "MENUB.TGA");
    F->slot_a = calloc(CW * CH, 4);
    F->slot_b = calloc(CW * CH, 4);
    bank_load(&F->menu1, fs, "M:\\LANGUAGE\\GRAFIX\\CHOP\\TITLE\\MENU1\\ANIMPSX.BIN");
    bank_load(&F->pswd, fs, "M:\\LANGUAGE\\GRAFIX\\CHOP\\TITLE\\PSWD\\ANIMPSX.BIN");
    F->pswd_bg = tga_load(F, "PSWD.TGA");
    F->difficulty = cfg ? cfg->difficulty : 1;
    F->mode = FE_MENU;
    F->mcode = -1;                      /* POLISH: gamestruct[0x12] = no password entered yet */
    F->gs.start_lives = F->gs.lives = cfg && cfg->lives > 0 ? cfg->lives : 3;
    F->gs.difficulty = F->difficulty;
    fe_save_load(F);
    return 0;
}

void fe_free(FrontEnd *F)
{
    if (F->vopen) video_close(&F->vid);
    free(F->menub); free(F->slot_a); free(F->slot_b); free(F->pswd_bg); free(F->lev_bg); free(F->card_bg);
    bank_free(&F->menu1); bank_free(&F->pswd);
    if (F->tally.d) bank_free(&F->tally);
    if (F->cheatbank.d) bank_free(&F->cheatbank);
    free(F->tally_bg); free(F->cheat_bg); free(F->credit_bg); free(F->T); free(F->demo_buf);   /* POLISH */
    if (F->seq_loaded) bank_free(&F->seq);
    pe_free(&F->title);
}

/* ------------------------------------------------------------------ video (FUN_10009980) */

static void video_start_next(FrontEnd *F)
{
    if (F->vopen) { video_close(&F->vid); F->vopen = 0; }
    while (F->vqi < F->vqn) {
        int n = F->vq[F->vqi++];
        if (!video_open_num(&F->vid, F->fsdir, n)) { F->vopen = 1; F->vhave = 0; F->vphase = 0; return; }
        fprintf(stderr, "video %d.ETV not found (set HERCULES_ETV_DIR or copy the CD's DATA folder next to HERCULES.FS)\n", n);
    }
    if (F->flow) { flow_run(F, F->flow); return; }    /* POLISH: continue the main process */
    F->mode = F->after_video;
    if (F->mode == FE_MENU) fe_goto_menu(F);
}

static void play_videos(FrontEnd *F, const int *list, int n, FeMode after)
{
    F->vqn = 0; F->vqi = 0;
    for (int i = 0; i < n && i < 8; i++) F->vq[F->vqn++] = list[i];
    F->after_video = after;
    F->mode = FE_VIDEO;
    video_start_next(F);
}

void fe_boot(FrontEnd *F)
{
    /* FUN_100164b0 at power-on (gamestruct[3] == 0x11): 0x33 -> 0x26 -> 0x27 (video 1, Disney Interactive)
     * -> 0x25 (video 2, countdown) -> 0x24 (video 3, Disney's Hercules title) -> 0xe7 -> 0xe6 main menu.
     * Skipping 1 or 2 jumps straight to 3 (the `(r & 8) != 0` breaks). */
    static const int boot[] = {1, 2, 3};
    char sp[1100]; const char *home = getenv("HOME");
    snprintf(sp, sizeof sp, "%s/Library/Application Support/Hercules/seen_intro", home ? home : ".");
    FILE *sf = fopen(sp, "rb");
    if (sf && !getenv("HERC_INTRO")) { fclose(sf); F->entered = 1; fe_goto_menu(F); return; }   /* seen the logos once: straight to the menu */
    if (sf) fclose(sf);
    if ((sf = fopen(sp, "wb"))) { fputs("1\n", sf); fclose(sf); }
    play_videos(F, boot, 3, FE_MENU);
}

static void tick_video(FrontEnd *F)
{
    if (!F->vopen) { video_start_next(F); return; }
    int skip = F->pressed & (PSX_START | PSX_CROSS | PSX_CIRCLE | PSX_SQUARE | PSX_TRIANGLE);
    if (skip) {
        /* boot sequence: skipping the logo/countdown goes to the title video */
        if (F->vqn == 3 && F->vq[2] == 3 && F->vqi < 3) F->vqi = 2;
        if (F->vqi > 0 && F->vq[F->vqi - 1] == 22 && (skip & PSX_CROSS)) F->vcross = 1;   /* POLISH: "continue?" */
        else if (F->vskip_ends) { F->vqi = F->vqn; if (F->flow == 0xfb) F->flow = 0xf0; }   /* POLISH */
        video_start_next(F);
        return;
    }
    /* ETV runs at 15 fps; the front end ticks at 30 Hz */
    if ((F->vphase++ & 1) == 0 || !F->vhave) {
        if (video_next(&F->vid, F->vframe)) { video_start_next(F); return; }
        F->vhave = 1;
    }
}

/* ------------------------------------------------------------------ main menu */

static void menu_positions(FrontEnd *F) { (void)F; /* FUN_10006fb0 is applied at draw time */ }

void fe_goto_menu(FrontEnd *F)
{
    F->flow = 0;
    F->mode = FE_MENU;
    F->ms = 1;
}

static void board_load(FrontEnd *F, uint32_t *slot)
{
    /* FUN_100071e0's list: t015, t013, t003, t001, t012, t010 (a088 cycles 0..5) */
    static const char *list[6] = {"T015.TGA", "T013.TGA", "T003.TGA", "T001.TGA", "T012.TGA", "T010.TGA"};
    uint32_t *img = tga_load(F, list[F->a088]);
    if (img) { memcpy(slot, img, sizeof(uint32_t) * CW * CH); free(img); }
    if (++F->a088 == 6) F->a088 = 0;
}

static void menu_enter(FrontEnd *F)
{
    /* state 1 */
    if (F->title_sounds != 1) fe_title_sounds(F, 0);    /* POLISH: OPTIONS bank + title music */
    F->a080 = 0; F->a084 = 0; F->a088 = 0;
    board_load(F, F->slot_a);       /* t015 -> VRAM (0x140,0) */
    F->a088 = 0;
    F->a020 = 0; F->a01c = 0; F->a018 = 0x80;
    for (int i = 0; i < 5; i++) F->muse_idx[i] = 0;
    memset(F->spk, 0, sizeof F->spk);
    fe_save_load(F);                                /* CONTINUE: a saved game is the default selection */
    F->a030 = F->save_code ? -1 : 0; F->a040 = 0; F->a038 = 0;
    F->a014 = 1;
    if (!F->entered) {
        /* first time: Hercules carries the board in from the left (FUN_10008320) */
        F->a044 = -0x204; F->a03c = 0;
        F->a07c = 0; F->a01c = 0x80;
        F->hx = -0x15e; F->hy = 0x50; F->hframe = 0x16; F->hvisible = 1;
        F->cc64 = -1; F->cc60 = 0;
        F->ms = 10;
    } else {
        F->a01c = 0x80; F->a03c = 0; F->a044 = 0; F->a07c = 2;
        F->hvisible = 0; F->cc64 = 4;
        F->ms = 2;
    }
}

static void herc_tick(FrontEnd *F)   /* FUN_10008320 */
{
    switch (F->cc64) {
    case 0:
        F->hframe = tbl(F, T_HRUN, F->cc60) + 0x16;
        if (tbl(F, T_HRUN, ++F->cc60) == -1) F->cc60 = 4;
        F->hx += 3;
        F->a044 = F->hx;
        F->a03c = tbl(F, T_HRUNY, F->hframe) - 0xa9 + F->hy;
        if (F->hx < -0x30) return;
        F->hframe = 7; F->cc64 = 1; F->cc60 = 0;
        return;
    case 1:
        F->hframe = tbl(F, T_HLIFT, F->cc60) + 7;
        if (tbl(F, T_HLIFT, ++F->cc60) == -1) {
            F->cc64 = 2; F->cc60 = 0; F->hframe = 0x26; F->a020 = 0x80; F->a03c += 0x14;
            return;
        }
        F->a044 = tbl(F, T_HLX, F->hframe) + F->hx;
        F->a03c = tbl(F, T_HLY, F->hframe) - 0xa9 + F->hy;
        return;
    case 2:
        F->hframe = tbl(F, T_HDROP, F->cc60) + 0x26;
        if (tbl(F, T_HDROP, ++F->cc60) == -1) { F->cc64 = 3; F->cc60 = 0; }
        break;
    case 3:
        F->hframe = tbl(F, T_HFALL, F->cc60) + 0x26;
        if (tbl(F, T_HFALL, ++F->cc60) == -1) F->cc60 = 0;
        F->hy += 6;
        if (F->hy > 0x190) F->cc64 = 4;
        break;
    default:
        return;
    }
    F->a044 = 0;
    if (F->a03c < 0) { F->a03c += 0x14; return; }
    if (F->a014) { fe_snd(F, 0x326); F->a014 = 0; }    /* the board lands */
    if (F->hframe < 0x2e) { F->a03c = -rnd(F, 0x10); return; }
    F->a03c = 0;
    F->a07c = 2;
}

static const uint32_t MUSE_T[5] = {T_MUSE0, T_MUSE1, T_MUSE2, T_MUSE3, T_MUSE4};
static const int MUSE_BASE[5] = {0x3c, 0x5b, 0x7c, 0xaf, 0xce};
/* positions (FUN_10006fb0), shadow rotation (FUN_10006bb0 +0x74) */
static const int MUSE_X[5] = {-0x50, 0x6e, -0x7d, 0x87, 0x50};
static const int MUSE_Y[5] = {0x69, 0x5f, 0x55, 0x4b, 0x69};
static const int MUSE_ROT[5] = {0x70, -0x70, 0x80, -0x60, -0x80};

static void muses_tick(FrontEnd *F)   /* FUN_10008140 */
{
    for (int i = 0; i < 5; i++) {
        F->muse_frame[i] = tbl(F, MUSE_T[i], F->muse_idx[i]) + MUSE_BASE[i];
        F->muse_idx[i]++;
        if (tbl(F, MUSE_T[i], F->muse_idx[i]) == -1) F->muse_idx[i] = 0x17;
    }
    if (++F->a084 == 0x3c) {
        F->a084 = 0;
        if (F->a080 == 2) F->a080 = 3;
        if (F->a080 == 6) F->a080 = 7;
    }
}

static void sparkles_tick(FrontEnd *F)   /* FUN_100199b0(0xf8) + spawn in state 2 */
{
    for (int i = 0; i < 48; i++) {
        FeSparkle *s = &F->spk[i];
        if (!s->active) continue;
        if (s->bright == 0) { s->active = 0; continue; }
        s->bright -= 8;
        if (s->frame > 0xf8) s->frame--;
        s->rot += rnd(F, 0x2f);
    }
    if (rnd(F, 100) < 7) {
        int k = rnd(F, 0x18), f = rnd(F, 3);
        for (int i = 0; i < 48; i++) if (!F->spk[i].active) {
            FeSparkle *s = &F->spk[i];
            s->active = 1; s->frame = 0xf8 + f;
            s->x = (int)pe_u32(&F->title, T_SPARK + 8u * k) - 0x98 + F->a044;
            s->y = (int)pe_u32(&F->title, T_SPARK + 8u * k + 4) - 0x84 + F->a03c;
            s->bright = 0x80; s->rot = rnd(F, 0x1000);
            break;
        }
    }
}

static void board_tick(FrontEnd *F)   /* the loading half of FUN_100071e0 */
{
    switch (F->a080) {
    case 0: board_load(F, F->slot_b); F->a080 = 2; break;
    case 3: if (F->a018 < 1) { F->a018 = 0; F->a080 = 4; } else F->a018 -= 0x10; break;
    case 4: board_load(F, F->slot_a); F->a080 = 6; break;
    case 7: if (F->a018 < 0x80) F->a018 += 0x10; else { F->a018 = 0x80; F->a080 = 0; } break;
    }
}

static void board_draw(FrontEnd *F, int bright)   /* drawing half of FUN_100071e0 */
{
    int u2 = bright * F->a018 >> 7, u1 = (0x80 - F->a018) * bright >> 7;
    switch (F->a080) {
    case 0: case 1: case 2: draw_bg(F, F->slot_a, u2, F->a044, F->a03c); break;
    case 4: case 5: case 6: draw_bg(F, F->slot_b, u1, F->a044, F->a03c); break;
    default:    /* crossfade: old opaque, new on top (FUN_10002090 ignores its semi flag) */
        draw_bg(F, F->a080 == 3 ? F->slot_a : F->slot_b, F->a080 == 3 ? u2 : u1, F->a044, F->a03c);
        draw_bg(F, F->a080 == 3 ? F->slot_b : F->slot_a, F->a080 == 3 ? u1 : u2, F->a044, F->a03c);
        break;
    }
}

static void pass_enter(FrontEnd *F);
static void card_enter(FrontEnd *F, int level);

static int menu_text_frame(int sel) { return sel < 0 ? 1 : sel == 0 ? 2 : sel == 1 ? 3 : sel == 3 ? 4 : 5; }   /* 5 = with CHEATS */

static void menu_tick(FrontEnd *F)
{
    uint16_t held = F->held, pr = F->pressed;
    switch (F->ms) {
    case 1: menu_enter(F); return;
    case 10:    /* fade in MENUB while the menu bank loads, then start the carry-in */
        if (F->a01c < 0x80) F->a01c += 0x10;
        F->a07c = 1; F->cc64 = 0; F->cc60 = 0;
        F->ms = 2;
        return;
    case 4: case 5:
        if (F->ms == 4) { F->a01c = 0x80; F->ms = 5; }
        muses_tick(F); board_tick(F);
        if (F->a01c < 1) { F->ms = 7; return; }
        F->a01c -= 8;
        return;
    case 7:
        F->entered = 1;
        switch (F->ff0) {
        case 1: new_game(F); return;                    /* Start Game: gamestruct[0] = 0, state 0x29 */
        case 0: pass_enter(F); return;                  /* Password */
        case 4: cheats_enter(F); return;                /* POLISH: cheat screen (FUN_100016f0) */
        case 5: flow_run(F, 0xdc); return;              /* POLISH: attract mode */
        default: fe_goto_menu(F); return;
        }
    case 2: break;
    default: return;
    }
    /* state 2 */
    herc_tick(F);
    muses_tick(F);
    sparkles_tick(F);
    board_tick(F);
    if (F->a07c == 2) {
        if (++F->a038 == 900) { F->a038 = 0; F->ff0 = 5; F->ms = 4; return; }   /* POLISH: attract demo */
    }
    if (F->a020 > 0) F->a020 -= 8;
    if (held) F->a038 = 0; else F->a040 = 0;
    if (F->a07c == 2) {
        if (!F->a040) {
            if ((held & PSX_UP) && (F->a030 > 0 || (F->a030 == 0 && F->save_code))) {
                fe_snd(F, 0x301);
                if (--F->a030 == 2) F->a030 = 1;
                F->a040 = 0xf;
            }
            int maxsel = 3 + ((F->gs.cheats & 0x80000000u) != 0);   /* CHEATS item once a cheat code was entered */
            if ((held & PSX_DOWN) && F->a030 < maxsel) {
                fe_snd(F, 0x300);
                if (++F->a030 == 2) F->a030 = 3;
                F->a040 = 0xf;
            }
        } else F->a040--;
    } else if (pr & (PSX_CROSS | PSX_START)) {
        /* skip the carry-in */
        F->a01c = 0x80; F->a03c = 0; F->a044 = 0; F->a07c = 2; F->cc64 = 4;
        F->hvisible = 0; F->a020 = 0x80;
        fe_snd(F, 0x303);
        pr = 0;
    }
    if (F->a07c == 2 && (pr & (PSX_CROSS | PSX_START))) {
        switch (F->a030) {
        case -1: fe_load_game(F); break;                        /* CONTINUE the autosaved game */
        case 0: fe_snd(F, 0x302); F->ff0 = 1; F->ms = 4; break;
        case 1: fe_snd(F, 0x302); F->ff0 = 0; F->ms = 4; break;
        case 3: fe_snd(F, 0x302); F->mode = FE_QUIT; break;     /* EXIT: FUN_1000c950 asks the EXE to quit */
        case 4: fe_snd(F, 0x302); F->ff0 = 4; F->ms = 4; break; /* POLISH: CHEATS */
        }
    }
}

static void menu_draw(FrontEnd *F)
{
    int b = F->ms >= 4 && F->ms <= 7 ? F->a01c : 0x80;
    memset(F->cv, 0, sizeof F->cv);
    if (F->ms == 10 || F->a07c < 2) draw_bg(F, F->menub, F->ms == 10 ? F->a01c : 0x80, 0, 0);
    board_draw(F, b);
    int ox = F->a044, oy = F->a03c;
    /* back-to-front: sparkles, muse shadows, muses, Hercules, logo, menu text */
    for (int i = 47; i >= 0; i--) {
        FeSparkle *s = &F->spk[i];
        if (s->active) draw_spr(F, frame(&F->menu1, s->frame), s->x, s->y, s->bright, 1, 0, s->rot);
    }
    for (int i = 4; i >= 0; i--)
        draw_spr(F, frame(&F->menu1, F->muse_frame[i] ? F->muse_frame[i] : MUSE_BASE[i] + 1),
                 MUSE_X[i] + ox, MUSE_Y[i] - 12 + oy, 0, 0, 0, MUSE_ROT[i]);
    for (int i = 4; i >= 0; i--)
        draw_spr(F, frame(&F->menu1, F->muse_frame[i] ? F->muse_frame[i] : MUSE_BASE[i] + 1),
                 MUSE_X[i] + ox, MUSE_Y[i] + oy, b, -1, 0, 0);
    if (F->hvisible) draw_spr(F, frame(&F->menu1, F->hframe), F->hx, F->hy, 0x80, -1, 0, 0);
    int tf = menu_text_frame(F->a030 < 0 ? 0 : F->a030);
    int tb = F->a030 < 0 ? b / 2 : b;                   /* CONTINUE selected: dim the baked menu text */
    draw_spr(F, frame(&F->menu1, 1), ox + 2, oy + 2, b, 2, 0, 0);
    draw_spr(F, frame(&F->menu1, 1), ox, oy, b, 1, 0, 0);
    draw_spr(F, frame(&F->menu1, tf), ox + 2, oy + 2, tb, 2, 0, 0);
    draw_spr(F, frame(&F->menu1, tf), ox, oy, tb, 1, 0, 0);
    if (F->save_code && F->a07c == 2) {                  /* CONTINUE row (saved game) */
        static const char *LN[12] = {"", "TRAINING", "GAUNTLET", "FOREST", "FOREST", "THEBES", "HYDRA", "MEDUSA", "CYCLOPS", "TITANS", "TORMENT", "VORTEX"};
        int lv = F->save_code >> 16 & 0xff;
        char buf[48];
        snprintf(buf, sizeof buf, "CONTINUE - LEVEL %d %s", lv, lv >= 1 && lv < 12 ? LN[lv] : "");
        int sel = F->a030 < 0, w = text_w(buf) + 1, x0 = (CW - w) / 2, y0 = 78;
        uint32_t col = sel ? 0xffe060u : 0xb89830u;
        for (int pass = 0; pass < 2; pass++) {
            uint32_t c = pass == 0 ? 0x101030u : col; int dx = pass == 0 ? 1 : 0;
            for (int i = 0; buf[i]; i++) {
                int ch = (unsigned char)buf[i]; if (ch < 32 || ch > 126) ch = '?';
                const uint8_t *g = FONT5X7[ch - 32];
                for (int cx = 0; cx < 5; cx++) for (int ry = 0; ry < 7; ry++) if (g[cx] >> ry & 1)
                    {
                        int px = x0 + i * 6 + cx + dx, py = y0 + ry + dx;
                        if (px >= 0 && py >= 0 && px < CW && py < CH) F->cv[py * CW + px] = c;
                    }
            }
        }
    }
    text(F, 2, 2, "build " __DATE__ " " __TIME__, 0xc0c0c0u);   /* so you can tell which build is running */
    if (F->a020 > 0) fill_rect(F, 0, 0, CW, CH, 0xffffffu, F->a020, 1);
    menu_positions(F);
}

/* ------------------------------------------------------------------ password (FUN_1000f460) */

/* symbol stops (frame offsets from VASE_BASE) gathered from the password table */
static const int VASE_STOP[4][11] = {
    {8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 89},
    {8, 16, 24, 32, 40, 48, 56, 65, 73, 81, 89},
    {8, 16, 24, 32, 40, 48, 56, 64, 72, 81, 89},
    {8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 89},
};

#include "fe_pswd.inc"   /* POLISH: Meg / password screen */

/* ------------------------------------------------------------------ pre-level cards (FUN_10014300 family) */

typedef struct { int level; const char *tga, *bank; int tx, ty, tframe, mx, my, mirror, mbase, mend; } CardDef;
static const CardDef CARDS[] = {
    {1,  "SEQ5.TGA",  "SEQ5",  0, -16, 1,   -100, 100, 0, 1, 0x10},   /* FUN_10014860 */
    {2,  "SEQ6.TGA",  "SEQ6",  0, -16, 1,   -100, 100, 0, 1, 0x10},   /* FUN_10014e10 */
    {3,  "SEQ4.TGA",  "SEQ4",  0, 0,   1,    100, 40,  1, 1, 0x10},   /* FUN_10014120 */
    {5,  "SEQ1.TGA",  "SEQ1",  0, 0,   1,    100, 100, 1, 1, 0x10},   /* FUN_10012710 */
    {6,  "SEQ9.TGA",  "SEQ9", -8, -24, 1,    100, 100, 1, 1, 0x10},   /* FUN_10016110 */
    {7,  "SEQ2.TGA",  "SEQ2", -10, -24, 0x10, 100, 100, 1, 0, 0xf},   /* FUN_10013310 */
    {8,  "SEQ3.TGA",  "SEQ3",  0, 0,   1,   -100, 100, 0, 1, 0x10},   /* FUN_100139d0 */
    {9,  "SEQ7.TGA",  "SEQ7",  0, 0,   1,   -100, 100, 0, 1, 0x10},   /* FUN_10015480 */
    {10, "SEQ8.TGA",  "SEQ8",  0, 0,   1,   -100, 40,  0, 1, 0x10},   /* FUN_10015ae0 */
    {11, "SEQ10.TGA", "SEQ10", 0, 0,   0x10, -100, 40, 0, 0, 0xf},    /* FUN_10012cc0 */
};

static const CardDef *card_def(int level)
{
    for (size_t i = 0; i < sizeof CARDS / sizeof CARDS[0]; i++) if (CARDS[i].level == level) return &CARDS[i];
    return NULL;
}

static void card_enter(FrontEnd *F, int level)
{
    F->clevel = level;
    const CardDef *c = card_def(level);
    if (!c) { F->start_level = level; F->mode = FE_PLAY; return; }
    F->flow = 0;
    free(F->card_bg); F->card_bg = tga_load(F, c->tga);
    if (F->seq_loaded) bank_free(&F->seq);
    char p[128]; snprintf(p, sizeof p, "M:\\LANGUAGE\\GRAFIX\\CHOP\\TITLE\\%s\\ANIMPSX.BIN", c->bank);
    F->seq_loaded = !bank_load(&F->seq, F->fs, p);
    F->cs = 10; F->cphase = 0; F->cidx = 0; F->ctoggle = 0; F->cfade = 0x80; F->ctext_b = 0;
    F->cmuse_frame = c->mbase + 1;
    F->mode = FE_CARD;
    if (level == 1 && !F->acec) {
        /* state 100: a new game first plays the story video (table entry 0x12 = 18.ETV) */
        static const int intro[] = {18};
        play_videos(F, intro, 1, FE_CARD);
    }
}

static void card_tick(FrontEnd *F)   /* FUN_10014300 state 2 + FUN_10014790 */
{
    const CardDef *c = card_def(F->clevel);
    if (!c) return;
    if (F->pressed & (PSX_START | PSX_CROSS) && F->cs != 0xc) { F->cs = 0xc; F->cfade = 0x80; }
    if (F->cs == 0xb && F->ctext_b < 0x80) F->ctext_b += 8;
    if (F->cs == 0xc) {
        F->ctext_b = F->cfade;
        if (F->cfade < 1) { F->start_level = F->clevel; F->mode = FE_PLAY; return; }
        F->cfade -= 8;
        return;
    }
    F->ctoggle ^= 1;
    if (F->ctoggle) return;
    if (F->cphase == 0) {
        int v = tbl(F, T_CARD, F->cidx);
        F->cmuse_frame = v + c->mbase;
        F->cidx++;
        int nv = tbl(F, T_CARD, F->cidx);
        if (nv == 10 && F->cs == 10) F->cs = 0xb;
        if (nv == -1) F->cphase = 1;
        if (F->cidx > 400) F->cphase = 1;
    } else if (F->cphase == 1) {
        if (++F->cmuse_frame >= c->mend) F->cphase = 2;
    } else if (F->cphase == 2) {
        F->cs = 0xc; F->cfade = 0x80; F->cphase = 3;
    }
}

static void card_draw(FrontEnd *F)
{
    const CardDef *c = card_def(F->clevel);
    memset(F->cv, 0, sizeof F->cv);
    if (!c) return;
    draw_bg(F, F->card_bg, F->cs == 0xc ? F->cfade : 0x80, 0, 0);
    if (!F->seq_loaded) return;
    draw_spr(F, frame(&F->seq, F->cmuse_frame), c->mx, c->my, F->cs == 0xc ? F->cfade : 0x80, -1, c->mirror, 0);
    draw_spr(F, frame(&F->seq, c->tframe), c->tx + 1, c->ty + 1, F->ctext_b * 2, 2, 0, 0);
    draw_spr(F, frame(&F->seq, c->tframe), c->tx, c->ty, F->ctext_b, 1, 0, 0);
}

void fe_card(FrontEnd *F, int level) { card_enter(F, level); }

/* ------------------------------------------------------------------ Properties sheet (HERCULES.EXE dialogs) */

static const char *TABS[4] = {"keys", "gamepad", "sound", "game play"};
static const char *LEVEL_NAMES[10] = {"training", "gauntlet", "forest", "thebes", "hydra", "medusa", "cyclops", "titans", "torment", "vortex"};
static const int LEVEL_NUMS[10] = {1, 2, 3, 5, 6, 7, 8, 9, 10, 11};

void fe_open_props(FrontEnd *F, FeMode ret)
{
    F->props_return = ret;
    F->mode = FE_PROPS;
    F->ptab = 0; F->prow = 0; F->pwait = 0;
}

static int props_rows(const FrontEnd *F)
{
    switch (F->ptab) {
    case 0: return ACT_COUNT + 2;          /* actions, reset keys, keys on */
    case 1: return PADACT_COUNT + 1;       /* actions, reset */
    case 2: return 3;                      /* sfx, cd music, volume */
    default: return 2 + (F->props_return == FE_MENU ? 10 : 0) + 1;   /* difficulty, lives, levels, OK */
    }
}

static void props_close(FrontEnd *F)
{
    if (F->cfg) { F->cfg->difficulty = F->difficulty; input_save(F->cfg); }
    F->mode = F->props_return;
}

static void props_tick(FrontEnd *F)
{
    uint16_t pr = F->pressed;
    if (F->pwait) return;                  /* waiting for fe_key / fe_pad_button */
    InputConfig *c = F->cfg;
    if (pr & PSX_TRIANGLE) { props_close(F); return; }
    if (pr & PSX_LEFT) { F->ptab = (F->ptab + 3) % 4; F->prow = 0; }
    if (pr & PSX_RIGHT) { F->ptab = (F->ptab + 1) % 4; F->prow = 0; }
    int n = props_rows(F);
    if (pr & PSX_UP) F->prow = (F->prow + n - 1) % n;
    if (pr & PSX_DOWN) F->prow = (F->prow + 1) % n;
    if (!(pr & (PSX_START | PSX_CROSS)) || !c) return;
    switch (F->ptab) {
    case 0:
        if (F->prow < ACT_COUNT) F->pwait = 1;
        else if (F->prow == ACT_COUNT) input_reset_keys(c);
        else c->keys_on ^= 1;
        break;
    case 1:
        if (F->prow < PADACT_COUNT) F->pwait = 1;
        else input_reset_pad(c);
        break;
    case 2:
        if (F->prow == 0) c->sfx_on ^= 1;
        else if (F->prow == 1) c->music_on ^= 1;
        else c->volume = c->volume >= 100 ? 0 : c->volume + 10;
        break;
    default:
        if (F->prow == 0) F->difficulty = (F->difficulty + 1) % 3;
        else if (F->prow == 1) c->lives = c->lives >= 5 ? 1 : c->lives + 1;
        else if (F->props_return == FE_MENU && F->prow < 12) {
            /* "restart level": only available from the title's main menu */
            props_close(F);
            F->entered = 1;
            new_game(F);                                 /* POLISH: a full run starting at that level */
            F->mode = FE_MENU;
            F->gs.level = LEVEL_NUMS[F->prow - 2] - 1;
            flow_run(F, 0x29);
        } else props_close(F);
        break;
    }
}

void fe_key(FrontEnd *F, SDL_Scancode sc)
{
    if (F->mode != FE_PROPS || !F->pwait || !F->cfg) return;
    if (F->ptab == 0) {
        if (sc == SDL_SCANCODE_ESCAPE) { F->pwait = 0; return; }
        F->cfg->key[F->prow] = sc == SDL_SCANCODE_SPACE ? SDL_SCANCODE_UNKNOWN : sc;
        F->pwait = 0;
    } else if (F->ptab == 1 && (sc == SDL_SCANCODE_SPACE || sc == SDL_SCANCODE_ESCAPE)) {
        if (sc == SDL_SCANCODE_SPACE) F->cfg->pad[F->prow] = SDL_CONTROLLER_BUTTON_INVALID;   /* "Hold SPACE to clear" */
        F->pwait = 0;
    }
}

void fe_pad_button(FrontEnd *F, SDL_GameControllerButton b)
{
    if (F->mode != FE_PROPS || !F->pwait || F->ptab != 1 || !F->cfg) return;
    F->cfg->pad[F->prow] = b;
    F->pwait = 0;
}

static void win_box(FrontEnd *F, int x, int y, int w, int h, int sunken)
{
    uint32_t face = 0xffc0c0c0u, hi = 0xffffffffu, sh = 0xff808080u, dk = 0xff000000u;
    for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++)
        if (xx >= 0 && yy >= 0 && xx < CW && yy < CH) F->cv[yy * CW + xx] = face;
    uint32_t tl = sunken ? sh : hi, br = sunken ? hi : dk;
    for (int xx = x; xx < x + w; xx++) { F->cv[y * CW + xx] = tl; F->cv[(y + h - 1) * CW + xx] = br; }
    for (int yy = y; yy < y + h; yy++) { F->cv[yy * CW + x] = tl; F->cv[yy * CW + x + w - 1] = br; }
}

static void props_draw(FrontEnd *F)
{
    /* dim whatever was on screen (the menu) like the modal dialog over the game window */
    for (int i = 0; i < CW * CH; i++) F->cv[i] = (F->cv[i] >> 1) & 0xff7f7f7fu;
    const int X = 20, Y = 16, W = 280, H = 224;
    win_box(F, X, Y, W, H, 0);
    for (int yy = Y + 2; yy < Y + 13; yy++) for (int xx = X + 2; xx < X + W - 2; xx++) F->cv[yy * CW + xx] = 0xff000080u;
    text(F, X + 5, Y + 4, "Hercules Properties", 0xffffffffu);
    int tx = X + 6;
    for (int t = 0; t < 4; t++) {
        int tw = text_w(TABS[t]) + 10;
        win_box(F, tx, Y + 17 - (t == F->ptab ? 2 : 0), tw, 13 + (t == F->ptab ? 2 : 0), 0);
        text(F, tx + 5, Y + 20 - (t == F->ptab ? 1 : 0), TABS[t], 0xff000000u);
        tx += tw + 1;
    }
    win_box(F, X + 6, Y + 30, W - 12, H - 52, 0);
    int ry = Y + 36, rx = X + 14;
    InputConfig *c = F->cfg;
    char buf[96];
    int nrows = props_rows(F);
    for (int r = 0; r < nrows && c; r++) {
        int y = ry + r * 11;
        const char *label = "", *value = "";
        switch (F->ptab) {
        case 0:
            if (r < ACT_COUNT) {
                label = ACT_NAMES[r];
                value = F->pwait && F->prow == r ? "press a key..." :
                        c->key[r] == SDL_SCANCODE_UNKNOWN ? "(none)" : SDL_GetScancodeName(c->key[r]);
            } else if (r == ACT_COUNT) label = "[ reset keys ]";
            else { label = "keys"; value = c->keys_on ? "[x] ON" : "[ ] OFF"; }
            break;
        case 1:
            if (r < PADACT_COUNT) {
                label = PADACT_NAMES[r];
                const char *bn = c->pad[r] == SDL_CONTROLLER_BUTTON_INVALID ? "(none)" : SDL_GameControllerGetStringForButton(c->pad[r]);
                value = F->pwait && F->prow == r ? "press a button..." : bn ? bn : "?";
            } else label = "[ reset buttons ]";
            break;
        case 2:
            if (r == 0) { label = "sound effects"; value = c->sfx_on ? "[x]" : "[ ]"; }
            else if (r == 1) { label = "cd music"; value = c->music_on ? "[x]" : "[ ]"; }
            else { label = "sound volume"; snprintf(buf, sizeof buf, "%d%%", c->volume); value = buf; }
            break;
        default:
            if (r == 0) { label = "difficulty"; value = F->difficulty == 0 ? "easy" : F->difficulty == 1 ? "medium" : "hard"; }
            else if (r == 1) { label = "lives"; snprintf(buf, sizeof buf, "%d", c->lives); value = buf; }
            else if (F->props_return == FE_MENU && r < 12) { snprintf(buf, sizeof buf, "restart level: %s", LEVEL_NAMES[r - 2]); label = buf; }
            else label = "[ OK ]";
            break;
        }
        if (r == F->prow) for (int yy = y - 2; yy < y + 9; yy++) for (int xx = rx - 4; xx < X + W - 14; xx++) F->cv[yy * CW + xx] = 0xff000080u;
        uint32_t col = r == F->prow ? 0xffffffffu : 0xff000000u;
        text(F, rx, y, label, col);
        if (*value) text(F, rx + 120, y, value, col);
    }
    if (F->ptab == 3 && F->props_return != FE_MENU)
        text(F, rx, ry + 40, "These options are only available", 0xff000000u),
        text(F, rx, ry + 50, "during the Title's main menu.", 0xff000000u);
    const char *hint = F->ptab == 1 ? "Hold SPACE to clear a button selection." :
                       "Arrows: choose  Enter: change  Esc: close";
    text(F, X + 8, Y + H - 16, hint, 0xff000000u);
}

/* ------------------------------------------------------------------ dispatch */

void fe_tick(FrontEnd *F, uint16_t pad)
{
    F->pressed = pad & ~F->held;
    F->held = pad;
    F->tick++;
    switch (F->mode) {
    case FE_VIDEO: tick_video(F); break;
    case FE_MENU: menu_tick(F); break;
    case FE_PASSWORD: pass_tick(F); break;
    case FE_CARD: card_tick(F); break;
    case FE_PROPS: props_tick(F); break;
    case FE_TALLY: tally_tick(F); break;           /* POLISH */
    case FE_CREDITS: credits_tick(F); break;       /* POLISH */
    case FE_CHEATS: cheats_tick(F); break;         /* POLISH */
    default: break;
    }
}

static void present(FrontEnd *F, Renderer *R, int src_h)
{
    /* POLISH: the PC build runs 320x240: title screens are drawn from the top and their last 16 lines fall
     * off the bottom (HERCULES.EXE stores the requested 320x256 at gamestruct+0x130 but never reads it) */
    for (int y = 0; y < R->h; y++) {
        int sy = y * 240 / R->h;
        const uint32_t *src = (src_h == 240 ? F->vframe : F->cv) + (size_t)sy * CW;
        uint32_t *dst = R->fb + (size_t)y * R->w;
        for (int x = 0; x < R->w; x++) dst[x] = src[x * CW / R->w];
    }
}

void fe_draw(FrontEnd *F, Renderer *R)
{
    switch (F->mode) {
    case FE_VIDEO:
        if (!F->vhave) memset(F->vframe, 0, sizeof F->vframe);
        present(F, R, 240);
        return;
    case FE_MENU: menu_draw(F); break;
    case FE_PASSWORD: pass_draw(F); break;
    case FE_CARD: card_draw(F); break;
    case FE_PROPS:
        if (F->props_return == FE_MENU) menu_draw(F);
        else {  /* opened in game: use the current game frame as the backdrop */
            for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++)
                F->cv[y * CW + x] = R->fb[(size_t)(y * R->h / CH) * R->w + (size_t)x * R->w / CW];
        }
        props_draw(F);
        break;
    case FE_TALLY: tally_draw(F); break;           /* POLISH */
    case FE_CREDITS: memset(F->cv, 0, sizeof F->cv); draw_bg(F, F->credit_bg, F->cr_fade, 0, 0); break;
    case FE_CHEATS: cheats_draw(F); break;
    default: memset(F->cv, 0, sizeof F->cv); break;
    }
    present(F, R, CH);
}

void fe_draw_help(FrontEnd *F, Renderer *R)
{
    /* F1: the original opened HERCULES.HLP; show its default-controls page over the game */
    (void)F;
    static const char *lines[] = {
        "HERCULES HELP - KEYBOARD", "",
        "Arrows     move", "Z          jump", "X          sword", "C          punch",
        "D          power punch", "S          weapon (Gift of the Gods)", "",
        "F1 or H help   F2 or O options   F3 or R restart", "Esc pause   Alt+Enter full screen", "",
        "Press F1 to close",
    };
    int S = R->scale, n = (int)(sizeof lines / sizeof lines[0]);
    int x0 = 40, y0 = 40, w = 240, h = 16 + n * 10;
    for (int y = y0 * S; y < (y0 + h) * S && y < R->h; y++)
        for (int x = x0 * S; x < (x0 + w) * S && x < R->w; x++) R->fb[(size_t)y * R->w + x] = 0xffc0c0c0u;
    for (int i = 0; i < n; i++) {
        const char *s = lines[i];
        for (int k = 0; s[k]; k++) {
            int c = (unsigned char)s[k];
            if (c < 32 || c > 126) continue;
            const uint8_t *g = FONT5X7[c - 32];
            for (int col = 0; col < 5; col++) for (int row = 0; row < 7; row++) if (g[col] >> row & 1)
                for (int yy = 0; yy < S; yy++) for (int xx = 0; xx < S; xx++) {
                    int px = (x0 + 8 + k * 6 + col) * S + xx, py = (y0 + 8 + i * 10 + row) * S + yy;
                    if (px < R->w && py < R->h) R->fb[(size_t)py * R->w + px] = i == 0 ? 0xff000080u : 0xff000000u;
                }
        }
    }
}

#include "fe_flow.inc"   /* POLISH: game flow, tally, credits, cheats, attract */
