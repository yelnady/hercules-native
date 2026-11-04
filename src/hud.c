#include "hud.h"
#include <string.h>

int hud_init(Hud *H, const Fs *fs)
{
    memset(H, 0, sizeof *H);
    uint32_t sz;
    H->page = fs_get(fs, "M:\\LANGUAGE\\SRCLANG\\STPANEL1.DAT", &sz);
    if (sz < 256 * 512) H->page = NULL;
    /* fallback icon table (first 16 entries of ENG1 0x1005dd80) until hud_set_eng1 is called */
    static const uint8_t GIFT[16][2] = {{3,0},{0,9},{0,10},{0,11},{0,12},{0,13},{0,14},{0,15},{0,16},{0,17},{0,18},{2,0},{2,1},{1,9},{1,10},{1,11}};
    memcpy(H->icon, GIFT, sizeof GIFT);
    H->nicon = 16;
    hud_reset(H);
    return H->page ? 0 : -1;
}

void hud_set_eng1(Hud *H, const PeImage *pe)
{
    const uint8_t *t = pe_ptr(pe, 0x1005dd80u, sizeof H->icon);
    if (t) { memcpy(H->icon, t, sizeof H->icon); H->nicon = 200; }
    for (int i = 0; i < 12; i++) H->life_tbl[i] = pe_s16(pe, 0x10055330u + 2u * (uint32_t)i);
    for (int i = 0; i < 6; i++) H->wpn_tbl[i] = pe_s16(pe, 0x10055310u + 2u * (uint32_t)i);
    hud_reset(H);
}

void hud_reset(Hud *H)
{
    H->shown_max = H->shown_hp = 0;
    H->orb_a = H->orb_a_target = 0x7d;
    H->orb_b = H->orb_b_target = H->wpn_tbl[0] ? H->wpn_tbl[0] : 0xb9;
    H->orb_a_up = H->orb_b_up = 0;
    H->orb_timer = 1;               /* expires on the first tick: the left orb spins to the lives count */
    H->orb_tick = 0;
    H->meter_wpn = H->meter_pow = 0;
    H->boss_state = 0; H->boss_x = 0x154; H->boss_y = 0xd0; H->boss_shown = H->boss_target = 0;
    H->boss_fade = 0; H->boss_mode = 0; H->boss_wait = 0;
    H->weapon_prev = 0;
}

/* FUN_10033620: textured sprite (x, y, u, v, w, h) from the panel page; semi = PSX code 0x66 (50% blend) */
static void blitb(Renderer *R, const Level *L, const uint8_t *page, int x, int y, int u, int v, int w, int h, int semi, int bright)
{
    if (!page || !L || w <= 0 || h <= 0) return;
    int S = R->scale;
    for (int yy = 0; yy < h; yy++) {
        int tv = v + yy;
        if (tv < 0 || tv >= 512) continue;
        for (int xx = 0; xx < w; xx++) {
            int tu = u + xx;
            if (tu < 0 || tu >= 256) continue;
            int c = page[tv * 256 + tu];
            if (!c) continue;                   /* texel 0 = transparent */
            uint32_t rgb = (uint32_t)(L->pal[c][0] * bright >> 7) << 16 | (uint32_t)(L->pal[c][1] * bright >> 7) << 8 | (uint32_t)(L->pal[c][2] * bright >> 7);
            for (int sy = 0; sy < S; sy++) {
                int py = (y + yy) * S + sy;
                if (py < 0 || py >= R->h) continue;
                uint32_t *row = R->fb + (size_t)py * R->w;
                for (int sx = 0; sx < S; sx++) {
                    int px = (x + xx) * S + sx;
                    if (px < 0 || px >= R->w) continue;
                    if (semi) {
                        uint32_t d = row[px];
                        row[px] = 0xff000000u | (((d >> 1) & 0x7f7f7f) + ((rgb >> 1) & 0x7f7f7f));
                    } else row[px] = 0xff000000u | rgb;
                }
            }
        }
    }
}

static void blit(Renderer *R, const Level *L, const uint8_t *page, int x, int y, int u, int v, int w, int h, int semi)
{
    blitb(R, L, page, x, y, u, v, w, h, semi, 0x80);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* One orb step (FUN_10012040): spin toward the target; crossing a 12-frame group boundary jumps into the
 * target icon's group from the opposite side. */
static int orb_step(int cur, int target, int up)
{
    if (cur == target) return cur;
    if (!up) { cur--; if (((cur % 12) + 12) % 12 == 0) cur = target + 6; }
    else { cur++; if (cur % 12 == 0) cur = target - 5; }
    return cur;
}

void hud_tick(Hud *H, const HudState *s)
{
    int mx = s && s->max_health > 0 ? s->max_health : 0x100;
    int hp = s && s->max_health > 0 ? s->health : mx;
    /* FUN_10032bd0 steps the shown values by 1 per tick toward the targets (+0x24 -> +0x22, +0x1e -> +0x20) */
    if (!H->shown_max) { H->shown_max = mx; H->shown_hp = hp; }
    for (int i = 0; i < 2; i++) H->shown_hp += H->shown_hp < hp ? 1 : H->shown_hp > hp ? -1 : 0;
    for (int i = 0; i < 3; i++) H->shown_max += H->shown_max < mx ? 1 : H->shown_max > mx ? -1 : 0;
    if (!s) return;

    /* left orb: alternates between Hercules (0x7d) and the lives count every 0x7f ticks; a life pickup
     * shows the count within 10 ticks (DAT_100721a0 / DAT_100721a6) */
    static int pickup;
    if (s->life_pickup) pickup = 1;
    if (pickup && H->orb_timer > 10) H->orb_timer = 10;
    if (H->orb_timer > 0x200) H->orb_timer = 0x200;
    if (--H->orb_timer == 0) {
        if (!pickup && H->orb_a_target != 0x7d) { H->orb_a_target = 0x7d; H->orb_a_up = 0; }
        else {
            int lv = clampi(s->lives, 0, 9);
            H->orb_a_up = 1;
            H->orb_a_target = H->life_tbl[lv] ? H->life_tbl[lv] : 5 + 12 * lv;
            pickup = 0;
        }
        H->orb_timer = 0x7f;
    }
    /* right orb: the selected Gift of the Gods (0x10055310), spinning the way the selection moved */
    int w = clampi(s->weapon, 0, 4);
    if (w != H->weapon_prev) { H->orb_b_up = w < H->weapon_prev; H->weapon_prev = w; }
    H->orb_b_target = H->wpn_tbl[w] ? H->wpn_tbl[w] : H->orb_b_target;
    if ((--H->orb_tick & 1) != 0) H->orb_a = orb_step(H->orb_a, H->orb_a_target, H->orb_a_up);
    H->orb_b = orb_step(H->orb_b, H->orb_b_target, H->orb_b_up);

    /* charge meters (DAT_1007082c): hi = power punch (DAT_10072194), lo = selected weapon's charges */
    int want = (s->charge + 7) >> 4; if (want > 15) want = 0x10;
    int cur = (H->meter_pow + 7) >> 4; if (cur > 15) cur = 0x10;
    if (want != cur) {
        if (want < cur) { H->meter_pow -= 0x10; if (H->meter_pow < 0) H->meter_pow = 0; }
        else { H->meter_pow += 0x10; if (H->meter_pow > 0xfe) H->meter_pow = 0xff; }
    }
    want = w == 0 ? 0x10 : (s->weapon_ammo + 7) >> 4; if (want > 15) want = 0x10;
    cur = (H->meter_wpn + 7) >> 4; if (cur > 15) cur = 0x10;
    if (want != cur) {
        if (want < cur) { H->meter_wpn -= 0x10; if (H->meter_wpn < 0) H->meter_wpn = 0; }
        else { H->meter_wpn += 0x10; if (H->meter_wpn > 0xfe) H->meter_wpn = 0xff; }
    }

    /* boss bar (DAT_10072188 state machine) */
    if (s->boss_cmd) H->boss_state = s->boss_cmd;
    switch (H->boss_state) {
    case 0: H->boss_x = 0x154; H->boss_mode = 0; break;
    case 1: case 8:
        if (s->boss_damage) { H->boss_target -= s->boss_damage; if (H->boss_target < 0) H->boss_target = 0; }
        if (H->boss_shown + H->boss_target == 0) { H->boss_wait = 0x19; H->boss_state = H->boss_state == 1 ? 4 : 7; }
        break;
    case 2: H->boss_x = 0x140; H->boss_y = 0xd0; H->boss_state = 3; H->boss_shown = H->boss_target = 0; H->boss_mode = 1; break;
    case 3:
        if (H->boss_x - 8 < 0xd1) { H->boss_state = 1; H->boss_x = 0xd0; H->boss_target = 0xff; }
        else H->boss_x -= 8;
        break;
    case 4:
        if (H->boss_wait) { H->boss_wait--; break; }
        H->boss_x += 8;
        if (H->boss_x > 0x148) H->boss_state = 0;
        break;
    case 5: H->boss_state = 6; H->boss_x = 0xd0; H->boss_y = 0xd0; H->boss_shown = H->boss_target = 0; H->boss_mode = 2; H->boss_fade = 0; break;
    case 6:
        H->boss_fade += 4;
        if (H->boss_fade > 0x80) { H->boss_fade = 0x80; H->boss_target = 0xff; H->boss_state = 8; }
        break;
    case 7:
        H->boss_fade -= 8;
        if (H->boss_fade < 1) { H->boss_state = 0; H->boss_fade = 0; H->boss_mode = 0; }
        break;
    }
    for (int i = 0; i < 2; i++) H->boss_shown += H->boss_shown < H->boss_target ? 1 : H->boss_shown > H->boss_target ? -1 : 0;
}

void hud_draw(Hud *H, Renderer *R, const Level *L, const HudState *s)
{
    if (!H->page) return;
    const uint8_t *P = H->page;
    if (!H->shown_max) hud_tick(H, s);
    int len = H->shown_max * 0x92 >> 8;
    if (len < 0x1c) len = 0x1c; else if (len > 0xff) len = 0xff;
    int fill = H->shown_hp * 0x92 >> 8;
    if (fill < 0) fill = 0; else if (fill > len - 1) fill = len - 1;

    /* gift orbs: icon index -> DAT_1005dd80 {col,row}, copied to (0,0x38) / (0x18,0x38) by FUN_10002c60 */
    int ga = s && s->gift_a > 0 ? s->gift_a : H->orb_a, gb = s && s->gift_b > 0 ? s->gift_b : H->orb_b;
    ga = ga < 0 || ga >= H->nicon ? 0 : ga;
    gb = gb < 0 || gb >= H->nicon ? 0 : gb;
    const uint8_t (*GIFT)[2] = (const uint8_t (*)[2])H->icon;

    /* time-trial countdown (gamestruct+6 == 3) */
    if (s && s->timer_ticks >= 0 && s->timer_ticks != 0) {
        int t = (s->timer_ticks + 0x1e) / 0x3c;
        blit(R, L, P, 0x18, 0xd0, 0xf0, (t / 10 % 10) * 0x10 + 0x28, 0x10, 0x10, 0);
        blit(R, L, P, 0x25, 0xd0, 0xf0, (t % 10) * 0x10 + 0x28, 0x10, 0x10, 0);
    }
    /* charge meters, drawn under the clouds: weapon charges right (u 200), power punch left (u 0xd0) */
    if (H->meter_wpn) {
        int k = H->meter_wpn * 0x12 >> 8;
        blit(R, L, P, 0x116, 0x27 - k, 200, 0x12 - k, 0x18, k, 0);
    }
    if (H->meter_pow) {
        int k = H->meter_pow * 0x14 >> 8;
        blit(R, L, P, 0x3f, 0x39 - k, 0xd0, 0x26 - k, 0x18, k, 0);
    }

    blit(R, L, P, 0xec, 0x14, 0x88, 0, 0x40, 0x1f, 0);                  /* right cloud */
    blit(R, L, P, len + 0x41, 0x17, 200, 0x50, 0x24, 0x10, 0);            /* bar end cap */
    blit(R, L, P, 0x10, 0x14, 0, 0, 0x50, 0x25, 0);                       /* left cloud */
    blit(R, L, P, 0x60, 0x24, 0xc0, 0x20, 0x10, 5, 0);
    blit(R, L, P, 0x2b, 0x17, GIFT[ga][0] * 24, 0x38 + GIFT[ga][1] * 24, 0x18, 0x18, 0);
    blit(R, L, P, 0x103, 0x17, GIFT[gb][0] * 24, 0x38 + GIFT[gb][1] * 24, 0x18, 0x18, 0);
    int body = len - 0xc, n = body / 0x40, x = 0x5c;
    for (int i = 0; i < n; i++, x += 0x40) blit(R, L, P, x, 0x1a, 0x18, 0x28, 0x40, 0xd, 0);
    blit(R, L, P, x, 0x1a, 0x18, 0x28, body & 0x3f, 0xd, 0);
    /* lit part (v 0x28) then unlit part (v 0x30), in 24-pixel tiles */
    int full = fill / 0x18, rem = fill % 0x18;
    for (int i = 0; i < full; i++) blit(R, L, P, 0x52 + i * 0x18, 0x1d, 0, 0x28, 0x18, 6, 0);
    blit(R, L, P, 0x52 + full * 0x18, 0x1d, 0, 0x28, rem, 6, 0);
    int k = len / 0x18 - full, left = len;
    int sx;
    if (k == 0) { left = len - fill; sx = fill + 0x52; blit(R, L, P, sx, 0x1d, rem, 0x30, left, 6, 0); goto boss; }
    blit(R, L, P, fill + 0x52, 0x1d, rem, 0x30, 0x18 - rem, 6, 0);
    k--;
    int base = (full + 1) * 0x18;
    for (int i = 0; i < k; i++) blit(R, L, P, base + 0x52 + i * 0x18, 0x1d, 0, 0x30, 0x18, 6, 0);
    left = len - base - k * 0x18;
    if (left > 0) blit(R, L, P, base + 0x52 + k * 0x18, 0x1d, 0, 0x30, left, 6, 0);
boss:
    /* boss bar (FUN_10032bd0 tail, FUN_10033420): 0x40 px, slides in from the right or fades in */
    if (H->boss_mode) {
        int b = H->boss_mode == 2 ? H->boss_fade : 0x80;
        int k = clampi(H->boss_shown * 0x40 >> 8, 0, 0x3f), bx = H->boss_x, by = H->boss_y;
        blitb(R, L, P, bx - 0x14, by, 0xa0, 0x50, 0x24, 0x10, 0, b);
        blitb(R, L, P, bx + 0x30, by, 200, 0x50, 0x24, 0x10, 0, b);
        blitb(R, L, P, bx, by + 3, 0x18, 0x28, 0x40, 0xd, 0, b);
        int full = k / 0x18, rm = k % 0x18;
        for (int i = 0; i < full; i++) blitb(R, L, P, bx + i * 0x18, by + 6, 0, 0x28, 0x18, 6, 0, b);
        blitb(R, L, P, bx + full * 0x18, by + 6, 0, 0x28, rm, 6, 0, b);
        blitb(R, L, P, bx + k, by + 6, rm, 0x30, 0x18 - rm, 6, 0, b);
        int nfull = (0x40 - k - (0x18 - rm)) / 0x18, x = (full + 1) * 0x18;
        for (int i = 0; i < nfull; i++, x += 0x18) blitb(R, L, P, bx + x, by + 6, 0, 0x30, 0x18, 6, 0, b);
        int last = 0x40 - x;
        if (last > 0) blitb(R, L, P, bx + x, by + 6, 0, 0x30, last, 6, 0, b);
    }
}

/* FUN_10002450: text from the glyph table at ENG1 0x1004d760 {u, v, width}, height 0x12 */
static const uint8_t GLYPH[31][3] = {
    {96,40,20},{122,77,15},{116,40,13},{129,40,18},{147,40,13},{0,0,0},{137,77,17},{0,0,0},{160,40,8},{0,0,0},
    {0,0,0},{168,40,12},{96,59,23},{180,40,21},{201,40,18},{120,59,14},{134,59,19},{153,59,15},{168,59,12},{180,59,15},
    {195,59,16},{0,0,0},{0,0,0},{0,0,0},{211,59,17},{0,0,0},{228,59,11},{219,40,14},{233,40,6},{0,4,0},{0,0,187},
};

static void ptext(Hud *H, Renderer *R, const Level *L, int x, int y, const char *s, int clut)
{
    for (; *s; s++) {
        unsigned g = ((unsigned)(unsigned char)*s - 1u) & 0x1f;
        if (g == 0x1f) { x += 0x10; continue; }
        blit(R, L, H->page, x, y, GLYPH[g][0], GLYPH[g][1], GLYPH[g][2], 0x12, clut == 4);
        x += GLYPH[g][2];
    }
}

void hud_draw_pause(Hud *H, Renderer *R, const Level *L, int st)
{
    if (!H->page) return;
    switch (st) {   /* FUN_10001f40: selected item clut 5 (opaque), the other clut 4 (semi-transparent) */
    case PAUSE_CONTINUE: case PAUSE_QUIT:
        ptext(H, R, L, 0x72, 0x50, "PAUSED", 3);
        ptext(H, R, L, 0x62, 0x78, "CONTINUE", st == PAUSE_CONTINUE ? 5 : 4);
        ptext(H, R, L, 0x83, 0x8c, "QUIT", st == PAUSE_CONTINUE ? 4 : 5);
        break;
    case PAUSE_SURE_NO: case PAUSE_SURE_YES:
        ptext(H, R, L, 0x3f, 0x50, "ARE YOU SURE \\", 3);
        ptext(H, R, L, 0x8d, 0x78, "NO", st == PAUSE_SURE_NO ? 5 : 4);
        ptext(H, R, L, 0x8b, 0x8c, "YES", st == PAUSE_SURE_NO ? 4 : 5);
        break;
    }
}
