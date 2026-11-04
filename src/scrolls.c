/* AUDSHELL: the HERCULES letter display (ENG1 FUN_10012620 + FUN_10012790 + FUN_10012960; ENG3 FUN_10016f30 + FUN_10017090 +
 * FUN_10017130).
 *
 * Collecting a letter / story scroll sets DAT_10072174 = 1 and DAT_10072104 = id. The state machine then slides eight letter
 * icons (screen-space pool objects) down from the top of the screen (to y = 70 in ENG1, 50 in ENG3), holds them 0x8c ticks, and
 * slides them back (100 ticks). The icon of the letter just found plays its letter animation (script table, banks LETTERH/E/R/C/
 * U/L/E/S); the others show a still from the pickup bank, dimmed (semi-transparent, abr 3) until their letter is owned. Each icon
 * starts moving 4*k ticks after the previous one and ticks sound 0xd when it does. */
#include "scrolls.h"
#include <string.h>

#define NSCR 8

typedef struct {
    int idx, timer, frame, bank, active;      /* tiny script runner for the active icon's animation */
} Anim;

struct Scrolls {
    int state, timer, cur;
    int32_t y[NSCR], target[NSCR];
    int delay[NSCR], hold[NSCR];
    Anim a[NSCR];
};

Scrolls *scrolls_new(void) { return calloc(1, sizeof(Scrolls)); }
void scrolls_free(Scrolls *S) { free(S); }
void scrolls_reset(Scrolls *S) { memset(S, 0, sizeof *S); }

void scrolls_trigger(Scrolls *S, int id)
{
    S->cur = id & 7;
    S->state = 1;
}

/* script: words (ENG1 FUN_10028820 format: w0 = default time, op words < 0 with an argument word) or pairs (ENG3: {s16 frame or
 * op, s16 time/arg}). Returns after setting a.frame (and a.bank for -0x12). */
static void anim_step(const ScrollCfg *C, Anim *a, uint32_t script)
{
    const PeImage *pe = C->pe;
    if (--a->timer >= 0) return;
    for (int guard = 0; guard < 64; guard++) {
        a->idx++;
        if (!C->pairs) {
            int16_t v = pe_s16(pe, script + 2u * (uint32_t)a->idx);
            if (v >= 0) { a->frame = v; a->timer = pe_s16(pe, script); return; }
            if (v == -0x12) { a->idx++; a->bank = pe_s16(pe, script + 2u * (uint32_t)a->idx); }
            else if (v == -1) a->idx = 0;
            else if (v != -0x28 && v != -0x27 && v != -0x2e) a->idx++;
        } else {
            int16_t v = pe_s16(pe, script + 4u * (uint32_t)a->idx), arg = pe_s16(pe, script + 4u * (uint32_t)a->idx + 2);
            if (v >= 0) { a->frame = v; a->timer = arg; return; }
            if (v == -0x12) a->bank = arg;
            else if (v == -1) a->idx = -1;
        }
    }
}

void scrolls_tick(Scrolls *S, const ScrollCfg *C, void (*tick_sound)(void *, int), void *ctx)
{
    const PeImage *pe = C->pe;
    switch (S->state) {
    case 0:
        memset(S->y, 0, sizeof S->y);
        return;
    case 1:
        S->state = 2;
        S->timer = 0x8c;
        for (int k = 0; k < NSCR; k++) {
            S->y[k] = 0; S->delay[k] = k * 4; S->target[k] = C->ty;
            memset(&S->a[k], 0, sizeof S->a[k]);
            if (k == S->cur) { S->hold[k] = 0x3c; S->a[k].active = 1; S->a[k].bank = C->home_bank; }
        }
        break;
    case 2:
        if (--S->timer < 1) S->state = 3;
        break;
    case 3:
        S->state = 4;
        S->timer = 100;
        for (int k = 0; k < NSCR; k++) { S->delay[k] = k * 4; S->target[k] = 0; }
        break;
    case 4:
        if (--S->timer < 1) S->state = 0;
        break;
    }
    for (int k = 0; k < NSCR; k++) {            /* FUN_10012790 / FUN_10017090 */
        Anim *a = &S->a[k];
        if (S->delay[k] == 0) {
            for (int i = 0; i < (C->steps ? C->steps : 6); i++) {
                if (S->y[k] < S->target[k]) S->y[k] += 0x10000;
                if (S->target[k] < S->y[k]) { S->y[k] -= 0x10000; if (S->y[k] < 0x10000) S->y[k] = 0; }
            }
        } else if (--S->delay[k] == 0 && tick_sound) tick_sound(ctx, (int)(pe_u32(pe, C->tx + 4u * (uint32_t)k) >> 16));
        if (a->active) {
            anim_step(C, a, pe_u32(pe, C->tscript + 4u * (uint32_t)k));
            if (S->hold[k] == 0) { if (a->bank == C->home_bank) { a->idx = 0; a->timer = 0; } }
            else S->hold[k]--;
        }
    }
    (void)pe;
}

void scrolls_draw(Scrolls *S, const ScrollCfg *C, Renderer *R, uint32_t owned,
                  Bank *(*get_bank)(void *, int), void *ctx)
{
    if (S->state == 0) return;
    const PeImage *pe = C->pe;
    for (int k = 0; k < NSCR; k++) {
        if (S->y[k] == 0) continue;
        const Anim *a = &S->a[k];
        int bank = a->active ? a->bank : C->home_bank;
        int frame = a->active && a->frame ? a->frame : pe_s16(pe, C->tframe + 4u * (uint32_t)k);
        Bank *b = get_bank(ctx, bank ? bank : C->home_bank);
        const Image *img = b ? bank_frame(b, frame) : NULL;
        if (!img) continue;
        int x = (int)(pe_u32(pe, C->tx + 4u * (uint32_t)k) >> 16), y = (int)(S->y[k] >> 16);
        if (owned >> k & 1) render_sprite2d(R, img, x, y, 1.0f, 0, -2.0f, 0.0f);
        else render_sprite2d_ex(R, img, x, y, 1.0f, 0, -2.0f, 0.0f, 3);       /* flags54 |= 7: not yet owned */
    }
}
