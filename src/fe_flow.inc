/* POLISH: game flow, level-complete tally, password reward, credits, cheats screen and attract mode.
 * Included at the end of frontend.c (shares its static helpers). Ports of TITLE.DLL:
 *   FUN_100164b0  main process: level order (state 0x29), story videos after a level (0xd3), game over
 *                 videos (0x8c..0x96), continue prompt (200 / 0xc9), endings (0xcd / 0xcf / 0xfa), attract (0xdc)
 *   FUN_1001c500  level tally + password reward (with FUN_1001bc60 .. FUN_1001b900, FUN_1001ac90 ..)
 *   FUN_10001a70  credits slideshow           FUN_100016f0 / FUN_10001000 / FUN_10001300  cheats screen
 * Object model: TITLE objects are {x, y, z 16.16; frame +0x34; brightness +0x4a; OT depth +0x3e; flags +0x54}.
 * FUN_100195f0 projects x * (0x400 - z) >> 10 around the screen origin; FUN_10003d10 sorts by depth. */

#include "sfx.h"

/* ------------------------------------------------------------------ objects (FUN_10017ad0 / FUN_10009350) */

#define OF_VIS 0x80000000u
#define OF_SEMI 4u
#define OF_MIRROR 0x10u

static FeObj *obj_new(FrontEnd *F, Bank *b, int frame)
{
    for (int i = 0; i < (int)(sizeof F->obj / sizeof F->obj[0]); i++) {
        FeObj *o = &F->obj[i];
        if (o->used) continue;
        memset(o, 0, sizeof *o);
        o->used = 1; o->bank = b; o->frame = frame; o->bright = 0x80; o->depth = 0x100;
        o->flags = OF_VIS; o->seq = ++F->objseq;
        return o;
    }
    return NULL;
}

static void obj_free(FeObj **o) { if (*o) { (*o)->used = 0; *o = NULL; } }
static void objs_clear(FrontEnd *F) { memset(F->obj, 0, sizeof F->obj); }

static int obj_cmp(const void *a, const void *b)
{
    const FeObj *x = *(const FeObj *const *)a, *y = *(const FeObj *const *)b;
    if (x->depth != y->depth) return y->depth - x->depth;     /* deeper first */
    return y->seq - x->seq;                                      /* earliest-created in front */
}

static void objs_draw(FrontEnd *F)
{
    const FeObj *list[192]; int n = 0;
    for (int i = 0; i < 192; i++) if (F->obj[i].used && (F->obj[i].flags & OF_VIS)) list[n++] = &F->obj[i];
    qsort(list, (size_t)n, sizeof list[0], obj_cmp);
    for (int i = 0; i < n; i++) {
        const FeObj *o = list[i];
        int k = 0x400 - (o->z >> 16);
        int sx = (o->x >> 16) * k >> 10, sy = (o->y >> 16) * k >> 10;
        int mode = (o->flags & OF_SEMI) ? (int)(o->flags & 3) : -1;
        int b = o->bright < 0 ? 0 : o->bright;
        if (mode < 0 && b == 0) continue;
        draw_spr(F, frame(o->bank, o->frame), sx, sy, b, mode, (o->flags & OF_MIRROR) != 0, o->rot & 0x3ff);
    }
}

static int rand_n(FrontEnd *F, int n) { return rnd(F, n); }  /* FUN_1000c4a0 (EXE rand() % n) */

static void fe_snd(FrontEnd *F, int id)   /* FUN_100054f0(id, obj): title sounds come from the OPTIONS / TALLY banks */
{
    if (!F->title_sounds) { sfx_load_title(F->fs, F->fsdir, 0); F->title_sounds = 1; }
    SfxPos pos = {0xa0, 0xa0, 0};
    sfx_play((uint16_t)id, 1, &pos);
}

static void fe_title_sounds(FrontEnd *F, int tally)
{
    sfx_load_title(F->fs, F->fsdir, tally);
    F->title_sounds = 1 + tally;
}

/* FUN_1000c4d0: speech clip (returns its length in ticks); FUN_1000cc90: mouth frame from the output level */
static int speech(FrontEnd *F, int n)
{
    if (!F->title_sounds) fe_title_sounds(F, 0);
    F->speech_len = sfx_speech(F->fs, n);
    F->speech_t = 0; F->speech_n = n;
    return F->speech_len;
}
static void speech_stop(FrontEnd *F) { sfx_speech_stop(); F->speech_len = 0; }
static int mouth(FrontEnd *F)
{
    static int smooth;
    int lv = sfx_speech_level(F->speech_t);
    smooth += (lv - smooth) >> 1;
    if (smooth > 0x1e) return 5;
    if (smooth > 0x18) return 4;
    if (smooth > 0x12) return 2;
    return smooth < 7 ? 1 : 3;
}

/* sparkle particles (FUN_10019a40 spawn, FUN_10019870 update): 48 slots */
typedef struct { FeObj *o; int vy; } Spark;

/* ------------------------------------------------------------------ tally state (FUN_1001c500 globals) */

typedef struct FeTally {
    int state;                  /* process state: 1 tally, 2 password reward, 0xfffe done */
    int b1d0, afe4, afe8, b178, b08c, aff0, b318, b328, afd4, b320, afdc, b324, b308;
    int af68, aff4, afc4, b35c, acfc, afe0, adc4, b354, b090, afcc, b010, b358;
    int cc64, cc60, cc68, afec, b360, b00c, b350;
    int coin_state[20], coin_vx[20], coin_vy[20];
    int let_state[8], let_tx[8], let_ty[8], let_vx[8], let_vy[8], let_t[8];
    int acf8, b100, vstate[4], pw[4];
    FeObj *title, *title_s, *sub, *sub_s, *row[7], *coinA, *coinB, *runner, *pegasus, *wink;
    FeObj *phil, *bubble, *coin[20], *let[8], *rate, *rate_s, *cont, *cont_s;
    FeObj *vase[4], *hdr1, *hdr1_s, *hdr2, *hdr2_s;
    Spark spk[48];
} FeTally;

static void spark_spawn(FrontEnd *F, int base, int x, int y, int z, int off, int bright)
{
    FeTally *T = F->T;
    if (T->b1d0 == 4) return;
    for (int i = 0; i < 48; i++) {
        if (T->spk[i].o) continue;
        FeObj *o = obj_new(F, &F->tally, base + off);
        if (!o) return;
        o->x = x; o->y = y; o->z = z; o->bright = bright; o->depth = 10;
        o->rot = rand_n(F, 0x1000); o->flags |= 5;
        T->spk[i].o = o; T->spk[i].vy = 0x10000;
        return;
    }
}

static void spark_tick(FrontEnd *F, int limit)   /* FUN_10019870 */
{
    static const int jit[16] = {0, 0, 2, 0, 1, 0, 0, 0, 0, 0, -2, 0, -1, 0, 0, 0};
    FeTally *T = F->T;
    for (int i = 0; i < 48; i++) {
        FeObj *o = T->spk[i].o;
        if (!o) continue;
        if (o->bright > 0) {
            o->bright -= 4;
            if (limit < o->frame) o->frame--;
            o->x += jit[(o->y >> 16) & 0xf] * 0x10000;
            o->y += T->spk[i].vy;
            o->rot += rand_n(F, 7);
            if (o->y < 0x1000001) continue;
        }
        obj_free(&T->spk[i].o);
    }
}

/* frame-attached point (FUN_10018600) is not exposed by the bank decoder: use the sprite's centre */
static void attach_point(FrontEnd *F, const FeObj *o, int *dx, int *dy)
{
    const Image *im = frame(o->bank, o->frame);
    *dx = 0; *dy = im ? -(im->h / 2) : 0;
    (void)F;
}

/* ---- level tally helpers (each a port of the named TITLE function) ---- */

static void t_bc60(FrontEnd *F)
{
    FeTally *T = F->T;
    if (T->b308 == 8) T->b178++;
    if (T->b178 && T->afe0 == 2) {
        for (int i = 0; i < 7; i++) T->row[i]->flags |= OF_VIS;
        T->coinA->flags |= OF_VIS;
        T->afe0 = 1; T->adc4 = 1;
    }
    if (T->b178 > 0x78 && T->acf8 == 0) T->acf8 = 1;
}

static void t_b710(FrontEnd *F)     /* "CONTINUE!" earned (all eight scrolls) */
{
    FeTally *T = F->T;
    switch (T->acfc) {
    case 0:
        T->cont = obj_new(F, &F->tally, 0x31c); T->cont_s = obj_new(F, &F->tally, 0x31c);
        T->cont_s->depth = 0x1e; T->cont_s->bright = 0; T->cont->depth = 0x14; T->cont->bright = 0;
        T->cont_s->flags |= 6; T->cont_s->x = T->cont_s->y = 0x20000;
        T->cont->flags &= ~OF_VIS; T->cont_s->flags &= ~OF_VIS;
        T->acfc = 99;
        break;
    case 1:
        T->cont->flags |= OF_VIS; T->cont_s->flags |= OF_VIS;
        if (T->cont->bright < 0x80) T->cont->bright += 0x10; else T->acfc = 2;
        break;
    case 2:
        if (T->b308 == 8) {
            T->cont->y -= 0x40000; T->cont_s->y -= 0x40000;
            if (T->cont->y < -0x1000000) T->acfc = 3;
        }
        break;
    case 3: T->cont->flags &= ~OF_VIS; T->cont_s->flags &= ~OF_VIS; break;
    case 4: T->cont->bright = T->afe4; break;
    }
    if (T->acfc) T->cont_s->bright = T->cont->bright * 2;
}

static void t_a630(FrontEnd *F)     /* character dropping in on the left */
{
    FeTally *T = F->T;
    switch (T->afc4) {
    case 0:
        T->pegasus = obj_new(F, &F->tally, 0x21c);
        T->pegasus->depth = 0x32; T->pegasus->x = -0x7d0000; T->pegasus->y = -0x15e0000;
        T->afc4 = 2;
        break;
    case 2:
        if (T->pegasus->y < 0x6a0000) { T->pegasus->y += 0x80000; break; }
        fe_snd(F, 0x431f);
        T->afc4 = 3; T->pegasus->y = 0x6a0000;
        break;
    case 3:
        if ((T->b35c ^= 1) == 0 && ++T->pegasus->frame == 0x229) { T->pegasus->frame = 0x20b; T->afc4 = 4; }
        break;
    case 4:
        if ((T->b35c ^= 1) == 0 && ++T->pegasus->frame == 0x21c) T->pegasus->frame = 0x20b;
        break;
    }
    T->pegasus->bright = T->afe4;
}

static void t_a2c0(FeObj *a, FeObj *b, FeObj *c, FeObj *d, int v)     /* 4-digit counter */
{
    a->frame = v / 1000 + 0x5a; b->frame = v % 1000 / 100 + 0x5a; c->frame = v % 100 / 10 + 0x5a; d->frame = v % 10 + 0x5a;
    if (v < 1000) a->flags &= ~OF_VIS; else a->flags |= OF_VIS;
    if (v < 100) b->flags &= ~OF_VIS; else b->flags |= OF_VIS;
    if (v < 10) c->flags &= ~OF_VIS; else c->flags |= OF_VIS;
}

static void t_a3a0(FrontEnd *F)     /* percentage row */
{
    static const int RX[7] = {32, 96, 104, 116, 128, 140, 156};    /* 0x100265b8 */
    FeTally *T = F->T;
    switch (T->afe0) {
    case 0:
        for (int i = 0; i < 7; i++) {
            FeObj *o = T->row[i] = obj_new(F, &F->tally, 0x5a);
            o->depth = 0x1e; o->x = (RX[i] - 0x40) * 0x10000; o->y = -0x340000; o->z = T->afd4; o->flags &= ~OF_VIS;
        }
        T->row[0]->frame = 100; T->row[1]->frame = 0x65; T->row[6]->frame = 0x66;
        T->afe0 = 2;
        break;
    case 1:
        if (T->afd4 < 1 && T->afdc < T->b318) {
            if (T->afdc % 10 == 0) fe_snd(F, 0x31b);
            T->afdc++;
        }
        if (T->afdc == T->b318 && T->afd4 == 0 && T->cc64 != 2) {
            T->phil->frame = 3; T->afe0 = 3; T->cc64 = 2;
            int r = T->aff0;
            speech(F, r < 10 ? 0x16 : r < 0x14 ? 0x1a : r < 0x28 ? 0x1c : r < 0x3c ? 0x22 : r < 0x50 ? 0x21 : r < 100 ? 0x1b : 0x1f);
        }
        t_a2c0(T->row[2], T->row[3], T->row[4], T->row[5], T->afdc);
        break;
    case 3:
        if (T->b328 <= T->aff0 && T->b08c && T->b324 < T->b328) {
            fe_snd(F, 0x31d);
            T->coin_state[T->b324] = 0;
            T->b324++;
        }
        T->afe0 = 4;
        /* fall through */
    case 4:
        if (T->b08c && T->cc64 == 1) T->cc64 = 5;
        break;
    }
    for (int i = 0; i < 7; i++) { T->row[i]->bright = T->afe4; T->row[i]->z = T->afd4; }
}

static void t_9b40(FrontEnd *F)     /* "LEVEL COMPLETE" zooming in */
{
    FeTally *T = F->T;
    if (T->b090 == 0) {
        T->title = obj_new(F, &F->tally, 1); T->title_s = obj_new(F, &F->tally, 1);
        T->title->depth = 10; T->title_s->depth = 0x14; T->title->bright = 0x80; T->title_s->bright = 0xff;
        T->title_s->x = T->title_s->y = 0x20000; T->title->z = T->title_s->z = 0x3e80000;
        T->title_s->flags |= 6;
        T->b090 = 1;
    }
    if (T->b090 == 1) {
        if (T->title->z < 1) { T->title->z = T->title_s->z = 0; T->b090 = 2; T->afe8 = 0x80; }
        else { T->title->z -= 0x4b0000; T->title_s->z -= 0x4b0000; }
    }
    T->title->bright = T->afe4; T->title_s->bright = T->afe4 * 2;
}

static void t_bdc0(FrontEnd *F)
{
    FeTally *T = F->T;
    if (T->afcc == 0) {
        T->sub = obj_new(F, &F->tally, 2); T->sub_s = obj_new(F, &F->tally, 2);
        T->sub->depth = 0x14; T->sub_s->depth = 0x1e; T->sub_s->x = T->sub_s->y = 0x20000; T->sub_s->flags |= 6;
        T->sub->bright = T->sub_s->bright = 0;
        T->afcc = 1;
    } else { T->sub->bright = T->afe4; T->sub_s->bright = T->afe4 * 2; }
}

static void t_beb0(FrontEnd *F)     /* Pegasus run with sparkles when every scroll was found */
{
    static const int A[4] = {821, 821, 822, 822};                       /* 0x10026618 */
    static const int B[] = {797,5,797,5,798,5,798,5,799,5,799,5,800,5,800,5,801,5,801,5,802,5,802,5,803,100,803,100,
                            804,100,804,100,805,100,805,100,806,100,806,100,807,100,807,100,808,100,808,100,809,100,
                            809,100,821,5,821,5,-1};                     /* 0x10026630 {frame, depth} */
    FeTally *T = F->T;
    int dx, dy;
    switch (T->af68) {
    case 0:
        T->runner = obj_new(F, &F->tally, 0x335);
        T->runner->depth = 5; T->runner->x = -0xc80000; T->runner->y = -0x200000; T->aff4 = 0; T->af68 = 1;
        break;
    case 2: case 4:
        if (T->af68 == 4 && T->runner->x >= 0xc80000) break;
        T->runner->frame = A[T->aff4]; if (++T->aff4 == 4) T->aff4 = 0;
        attach_point(F, T->runner, &dx, &dy);
        spark_spawn(F, 0x23f, (rand_n(F, 16) - 8 + dx) * 0x10000 + T->runner->x, (rand_n(F, 16) - 8 + dy) * 0x10000 + T->runner->y, 0, rand_n(F, 1), T->afe4);
        T->runner->x += 0x30000;
        if (T->af68 == 2 && T->runner->x > -0x800000) { T->af68 = 3; T->aff4 = 0; }
        break;
    case 3:
        attach_point(F, T->runner, &dx, &dy);
        spark_spawn(F, 0x23f, (rand_n(F, 16) - 8 + dx) * 0x10000 + T->runner->x, (rand_n(F, 16) - 8 + dy) * 0x10000 + T->runner->y, 0, rand_n(F, 1), T->afe4);
        T->runner->frame = B[T->aff4]; T->runner->depth = B[T->aff4 + 1];
        T->aff4 += 2; if (B[T->aff4] == -1) T->aff4 = 0;
        T->runner->x += 0x30000;
        if (T->runner->x > 0x400000 && T->runner->frame == 0x335) { T->af68 = 4; T->aff4 = 0; }
        break;
    }
    T->runner->bright = T->afe4;
}

static void t_a180(FrontEnd *F)     /* blinking figure on the right */
{
    FeTally *T = F->T;
    if ((T->b358 ^= 1) != 0) return;
    if (T->b010 == 0) {
        T->wink = obj_new(F, &F->tally, 0x1eb);
        T->wink->x = 0x6e0000; T->wink->y = 0x420000; T->wink->depth = 0x32; T->wink->flags |= OF_MIRROR;
        T->b010 = 1;
    } else if (T->b010 == 1) {
        if (rand_n(F, 100) < 0xb) T->b010 = 2;
    } else if (T->b010 == 2 && ++T->wink->frame == 0x1f7) {
        T->wink->frame = 0x1eb; T->b010 = 1;
        if (rand_n(F, 100) < 0x19) fe_snd(F, 0x4320);
    }
    T->wink->bright = T->afe4;
}

static int t_5cc0(FrontEnd *F, int base, const signed char *tab, int loop, int dx, int dy)   /* FUN_10005cc0 */
{
    FeTally *T = F->T;
    int r = 0;
    if (T->cc68 < 1) {
        T->phil->frame = tab[T->cc60] + base;
        T->cc68 = tab[T->cc60 + 1];
        T->cc60 += 2;
        if (tab[T->cc60] == -1) { r = 1; T->cc60 = loop; }
    } else T->cc68--;
    T->phil->x += (T->phil->flags & OF_MIRROR) ? -dx * 0x10000 : dx * 0x10000;
    T->phil->y += dy * 0x10000;
    return r;
}

static void t_a7c0(FrontEnd *F)     /* Phil: idle, reacts to the rating, runs for the bonus coin */
{
    static const signed char IDLE[] = {0,6,1,3,2,3,3,3,4,3,5,3,6,3,7,0xe,6,3,5,3,4,3,3,3,2,3,1,3,0,6,-1};   /* 0x10023270 */
    static const signed char RUN[] = {0,1,0,8,1,9,1,10,1,11,1,12,1,13,1,2,1,3,1,4,1,5,1,6,1,7,1,-1};           /* 0x10023290 */
    FeTally *T = F->T;
    T->b360 ^= 1;
    switch (T->cc64) {
    case 0:
        T->phil = obj_new(F, &F->tally, 0x229); T->phil->depth = 0x32; T->phil->x = -0x550000; T->phil->y = 0x210000;
        T->bubble = obj_new(F, &F->tally, 0x17); T->bubble->depth = 0x32; T->bubble->x = -0x550000; T->bubble->y = 0x210000;
        T->cc64 = 1; T->cc60 = T->cc68 = 0;
        break;
    case 1: case 9: t_5cc0(F, 0x229, IDLE, 0, 0, 0); break;
    case 2: T->cc64 = 3; break;
    case 3:
        if (!T->b360) break;
        T->bubble->frame++;
        T->phil->frame++;
        if (T->phil->frame == 8) fe_snd(F, 0x4321);
        if (T->phil->frame == 0xf) fe_snd(F, 0x430f);
        if (T->phil->frame == 0x16) { T->bubble->frame = 0x17; T->cc64 = 1; T->afec = 1; T->phil->frame = 0x229; }
        break;
    case 5: T->cc60 = T->cc68 = 0; T->phil->frame = 0x229; T->b00c = 0; T->cc64 = 6; break;
    case 6:
        t_5cc0(F, 0x229, IDLE, 0, 0, 0);
        if (++T->b00c > 5) { T->cc64 = 7; T->cc60 = T->cc68 = 0; T->phil->frame = 0x231; }
        break;
    case 7:
        t_5cc0(F, 0x231, RUN, 4, 6, 0);
        if (T->phil->x > 0xc80000) { T->phil->flags |= OF_MIRROR; T->cc64 = 8; }
        break;
    case 8:
        t_5cc0(F, 0x231, RUN, 4, 6, 0);
        if (T->phil->x < 0) { T->cc60 = T->cc68 = 0; T->phil->frame = 0x229; fe_snd(F, 0x4323); T->cc64 = 9; }
        break;
    }
    switch (T->afec) {
    case 1: T->bubble->frame = 0x302; T->afec = 2; break;
    case 2: if (T->b360 && ++T->bubble->frame == 0x30d) T->afec = 3; break;
    case 3:
        if (T->b360) {
            if (rand_n(F, 100) < 0x32) T->bubble->frame++;
            if (T->bubble->frame == 0x31c) T->bubble->frame = 0x30d;
        }
        break;
    }
    T->phil->bright = T->afe4; T->bubble->bright = T->afe4;
}

static void t_9c90(FrontEnd *F)     /* bonus-life coins */
{
    FeTally *T = F->T;
    T->b350 ^= 1;
    for (int i = 0; i < 20; i++) {
        FeObj *o = T->coin[i];
        switch (T->coin_state[i]) {
        case 0:
            o = T->coin[i] = obj_new(F, &F->tally, 0x24d + rand_n(F, 8));
            if (!o) { T->coin_state[i] = -1; break; }
            o->x = 0x500000; o->y = 0x3c0000; o->depth = 0x14;
            T->coin_vy[i] = -0xa0000; T->coin_vx[i] = (rand_n(F, 8) - 5) * 0x10000; T->coin_state[i] = 1;
            break;
        case 1:
            o->x += T->coin_vx[i]; T->coin_vx[i] -= T->coin_vx[i] >> 4;
            o->y += T->coin_vy[i];
            if (rand_n(F, 100) < 0x32)
                spark_spawn(F, 0x23f, (rand_n(F, 16) - 8) * 0x10000 + o->x, (rand_n(F, 16) - 8) * 0x10000 + o->y, 0, rand_n(F, 3), T->afe4);
            if (o->y < 0x460000) T->b00c = 0;
            if (o->y > 0x460000) { o->y = 0x460000; T->coin_vy[i] = -(T->coin_vy[i] >> 2); }
            if (o->x < T->phil->x) {
                fe_snd(F, 0x4322);
                T->coin_state[i] = 2;
                spark_spawn(F, 0x23f, (rand_n(F, 16) - 8) * 0x10000 + o->x, (rand_n(F, 16) - 8) * 0x10000 + o->y, 0, 9, T->afe4);
            }
            T->coin_vy[i] += 0x8000;
            if (++o->frame == 0x255) o->frame = 0x24d;
            o->bright = T->afe4;
            break;
        case 2:
            o->x += T->coin_vx[i]; T->coin_vx[i] -= T->coin_vx[i] >> 4;
            o->flags |= 5;
            if (o->bright <= 0) T->coin_state[i] = 3; else o->bright -= 4;
            if (o->y > -0xc80000) o->y -= 0x30000;
            if (++o->frame == 0x255) o->frame = 0x24d;
            break;
        }
    }
}

static void t_9fa0(FrontEnd *F)     /* spinning coin icons beside the percentage */
{
    FeTally *T = F->T;
    T->b354 ^= 1;
    if (T->adc4 == 0) {
        T->coinA = obj_new(F, &F->tally, 0x2b); T->coinB = obj_new(F, &F->tally, 0x41);
        T->coinA->x = -0x400000; T->coinA->y = -0x340000; T->coinB->x = 0; T->coinB->y = -0x340000;
        T->coinA->z = T->coinB->z = T->afd4; T->coinA->depth = T->coinB->depth = 0x1e;
        T->coinA->flags &= ~OF_VIS; T->coinB->flags &= ~OF_VIS;
        T->adc4 = 2;
    } else if (T->adc4 == 1) {
        T->coinA->flags |= OF_VIS; T->coinB->flags |= OF_VIS;
        T->coinA->z = T->coinB->z = T->afd4;
        if (T->b354) {
            if (++T->coinA->frame == 0x41) T->coinA->frame = 0x2b;
            if (++T->coinB->frame == 0x57) T->coinB->frame = 0x41;
        }
        if (T->afd4 < 1) T->afd4 = 0; else T->afd4 -= 0x4b0000;
        T->coinA->bright = T->coinB->bright = T->afe4;
    }
}

static void t_b150(FrontEnd *F)     /* the eight HERCULES scroll letters */
{
    static const int BASE[8] = {597, 621, 645, 669, 693, 717, 621, 741};   /* 0x100265d8, 24 frames each */
    FeTally *T = F->T;
    T->b308 = 0;
    for (int i = 0; i < 8; i++) {
        FeObj *o = T->let[i];
        switch (T->let_state[i]) {
        case 0:
            o = T->let[i] = obj_new(F, &F->tally, BASE[i]);
            T->let_tx[i] = i * 0x200000 - 0x700000; T->let_ty[i] = -0x2a0000; T->let_vx[i] = T->let_vy[i] = 0;
            o->depth = 0x1e; o->x = (rand_n(F, 0x140) - 0xa0) * 0x10000; o->y = (rand_n(F, 0x100) - 0x80) * 0x10000;
            o->z = T->b320; o->flags &= ~OF_VIS; T->let_t[i] = 0; T->let_state[i] = 4;
            break;
        case 1:
            if (++o->frame == BASE[i] + 24) o->frame = BASE[i];
            if (T->b320 < 1) T->b320 = 0; else T->b320 -= 0x4b0000;
            o->z = T->b320;
            if (T->b320 == 0) T->let_state[i] = 2;
            break;
        case 2:
            if (T->b1d0 != 4 && T->let_t[i] != -1) {
                T->let_t[i]++;
                if (o->frame == BASE[i] && T->let_t[i] >= i * 0x19 + 0x1e) {
                    T->let_t[i] = -1;
                    if (F->gs.scrolls == 0xff && T->af68 == 1) T->af68 = 2;
                    if (i == 7) {
                        if (F->gs.scrolls == 0xff) {
                            fe_snd(F, 0x325);
                            speech(F, rand_n(F, 2) == 0 ? 0x5f : 0x5a);
                        }
                        T->let_state[0] = 9;
                    }
                } else if (++o->frame == BASE[i] + 24) o->frame = BASE[i];
            }
            break;
        case 4:
            o->flags |= OF_VIS;
            for (int k = 0; k < 8; k++) if (!(F->gs.scrolls >> k & 1) && T->let[k]) T->let[k]->flags |= 5;
            T->let_state[i] = 1;
            break;
        case 9:
            if (F->gs.scrolls == 0xff) {
                if (T->acfc == 99) T->acfc = 1;
                if (T->af68 == 4 && T->acfc == 2) T->let_state[0] = 0xd;
            } else T->let_state[0] = 0xd;
            break;
        case 10:
            if (++T->let_t[i] > 0x3b) { T->let_vy[i] = 0; T->let_state[i] = 0xb; }
            break;
        case 0xb:
            o->y += T->let_vy[i]; T->let_vy[i] -= 0x10000;
            if (o->y < -0xa00000) T->let_state[i] = 0xc;
            break;
        case 0xc: T->b308++; break;
        case 0xd:
            for (int k = 0; k < 8; k++) { T->let_state[k] = 10; T->let_t[k] = 0x3c - 4 * k; }
            break;
        }
        if (T->let_state[i] == 0xc || !o) continue;
        if (!(o->flags & 5) && T->let_state[i] != 3 && rand_n(F, 100) < 3)
            spark_spawn(F, 0x23f, (rand_n(F, 16) - 8) * 0x10000 + o->x, o->y - rand_n(F, 0x18) * 0x10000, o->z, rand_n(F, 3), T->afe4);
        o->bright = (o->flags & 5) ? T->afe4 >> 1 : T->afe4;
        if (T->let_state[i] > 0 && T->let_state[i] < 3) {
            o->x += T->let_vx[i]; o->y += T->let_vy[i];
            T->let_vx[i] = (T->let_tx[i] - o->x) >> 3; T->let_vy[i] = (T->let_ty[i] - o->y) >> 3;
        }
    }
}

static void t_b900(FrontEnd *F)     /* rating text */
{
    FeTally *T = F->T;
    if (T->acf8 == 1) {
        int f = 0x2fd, r = T->aff0;
        if (r > 0x50) f = 0x301; else if (r > 0x3c) f = 0x300; else if (r > 0x28) f = 0x2ff; else if (r > 0x14) f = 0x2fe;
        T->rate = obj_new(F, &F->tally, f); T->rate_s = obj_new(F, &F->tally, f);
        T->rate_s->depth = 0x14; T->rate->depth = 10; T->rate->bright = T->rate_s->bright = 0;
        T->rate_s->flags |= 6; T->rate->y = -0x700000; T->rate_s->x = 0x20000; T->rate_s->y = T->rate->y + 0x20000;
        T->acf8 = 2;
    } else if (T->acf8 == 2) {
        if (T->rate->bright < 0x80) T->rate->bright += 8; else T->acf8 = 3;
    } else if (T->acf8 == 3) T->rate->bright = T->afe4;
    if (T->acf8) T->rate_s->bright = T->rate->bright * 2;
}

/* ---- password reward (process state 2) ---- */

static const int VBASE[4] = {0x18e, 0xcc, 0x12d, 0x6b}, VEND[4] = {0x1e5, 0x123, 0x184, 0xc2};
static const int VSTOP[4] = {0x18a, 200, 0x129, 0x67}, VINIT[4] = {0x14a, 0x88, 0xe9, 0x27};

static void t_bcf0(FrontEnd *F)
{
    FeTally *T = F->T;
    T->b178++;
    for (int i = 0; i < 4; i++)
        if (T->b178 > i * 10 && T->vstate[i] == 1) { fe_snd(F, 0x318); T->vstate[i] = 2; }
}

static void t_ac90(FrontEnd *F)
{
    FeTally *T = F->T;
    for (int i = 0; i < 4; i++) {
        FeObj *o = T->vase[i];
        switch (T->vstate[i]) {
        case 0:
            o = T->vase[i] = obj_new(F, &F->tally, VINIT[i] + T->pw[i]);
            if (o->frame < VBASE[i]) o->frame += 0x5c;
            o->x = i * 0x3c0000 - 0x5a0000; o->y = 0x80000; o->z = 0x3e80000; o->flags &= ~OF_VIS;
            T->vstate[i] = 1;
            break;
        case 2:
            o->flags |= OF_VIS;
            if (o->z < 1) { fe_snd(F, 0x4319); o->z = 0; T->vstate[i] = 4; }
            else o->z -= 0x640000;
            if (++o->frame > VEND[i]) o->frame = VBASE[i];
            break;
        case 4:
            if (++o->frame > VEND[i]) o->frame = VBASE[i];
            if ((i == 0 || T->vstate[i - 1] > 4) && T->pw[i] == o->frame - VSTOP[i]) {
                sfx_stop(0x319, 0);
                fe_snd(F, 0x31a);
                if (i == 3 && F->gs.vases == 0xf) fe_snd(F, 0x31e);
                T->vstate[i] = 5; o->bright = 0xff;
                if (T->pw[i] != 4)
                    for (int k = 0; k < 5; k++)
                        spark_spawn(F, 0x23f, (rand_n(F, 0x20) - 0x10) * 0x10000 + o->x, (rand_n(F, 0x20) - 0x30) * 0x10000 + o->y, 0, rand_n(F, 3), T->afe4);
            }
            break;
        case 5:
            if (o->bright < 0x81) T->vstate[i] = 6; else o->bright -= 4;
            if (T->b1d0 != 4 && rand_n(F, 0x14) == 0)
                spark_spawn(F, 0x23f, (rand_n(F, 0x20) - 0x10) * 0x10000 + o->x, (rand_n(F, 4) - 8) * 0x10000 + o->y, 0, rand_n(F, 3), T->afe4);
            break;
        }
        if (o && T->vstate[i] != 5) o->bright = T->afe4;
    }
}

static void t_bab0(FrontEnd *F)
{
    FeTally *T = F->T;
    if (!T->b100) {
        T->hdr1 = obj_new(F, &F->tally, 0x57); T->hdr1->depth = 0x14;
        T->hdr1_s = obj_new(F, &F->tally, 0x57); T->hdr1_s->depth = 0x1e; T->hdr1_s->flags |= 6; T->hdr1_s->x = T->hdr1_s->y = 0x20000;
        T->hdr2 = obj_new(F, &F->tally, 0x59); T->hdr2->depth = 0x14;
        T->hdr2_s = obj_new(F, &F->tally, 0x59); T->hdr2_s->depth = 0x1e; T->hdr2_s->flags |= 6; T->hdr2_s->x = T->hdr2_s->y = 0x20000;
        T->b100 = 1;
    }
    T->hdr1->bright = T->hdr2->bright = T->afe4;
    T->hdr1_s->bright = T->hdr2_s->bright = T->afe4 * 2;
}

/* ---- tally process ---- */

static void save_game(FrontEnd *F, int code);
static void flow_run(FrontEnd *F, int st);

static void tally_enter(FrontEnd *F)
{
    free(F->T);
    F->T = calloc(1, sizeof *F->T);
    FeTally *T = F->T;
    objs_clear(F);
    fe_title_sounds(F, 1);
    if (!F->tally_bg) F->tally_bg = tga_load(F, "T007.TGA");
    if (!F->tally.d) bank_load(&F->tally, F->fs, "M:\\LANGUAGE\\GRAFIX\\CHOP\\TITLE\\TALLYUP\\ANIMPSX.BIN");
    FeGame *g = &F->gs;
    if (g->cheats & 0x800000) g->vases = 0xf;
    if (g->vases == 0xf) {
        int e = g->difficulty * 12 + g->level;
        for (int k = 0; k < 4; k++) T->pw[k] = (int)pe_u32(&F->title, T_PSWD + 0x18u * (uint32_t)e + 4u * (uint32_t)k);
    } else for (int k = 0; k < 4; k++) T->pw[k] = 4;
    for (int i = 0; i < 20; i++) T->coin_state[i] = -1;
    for (int i = 0; i < 8; i++) T->let_state[i] = (g->level == 6 || g->level == 7 || g->level == 11) ? 0xc : 0;
    T->afd4 = 0x3e80000; T->b320 = 0x3e80000;
    T->b328 = g->difficulty == 0 ? 0x28 : g->difficulty == 1 ? 0x3c : 0x50;
    if (g->scrolls == 0xff) g->continues++;
    int tot = g->coins1a + g->coins16, got = g->coins18 + g->coins14;
    int pct = tot ? got * 100 / tot : 0;
    T->aff0 = -0x19 * g->deaths + pct / 2 + 0x32;
    T->aff0 = T->aff0 < 0 ? 0 : T->aff0 > 100 ? 100 : T->aff0;
    T->b318 = pct > 100 ? 100 : pct;
    if (T->b328 < T->aff0 && g->lives < 9) { g->lives++; T->b08c = 1; }
    T->state = 1;
    F->mode = FE_TALLY;
}

static void tally_fade(FrontEnd *F, uint16_t pr)
{
    FeTally *T = F->T;
    switch (T->b1d0) {
    case 0: if (T->afe4 < 0x80) T->afe4 += 8; else T->b1d0 = 1; break;
    case 4:
        if (T->afe4 < 1) {
            T->b1d0 = 0; T->b178 = 0; T->afe4 = 0;
            for (int i = 0; i < 48; i++) obj_free(&T->spk[i].o);
            if (T->state == 1 && F->gs.vases == 0xf) {
                objs_clear(F); memset(T->spk, 0, sizeof T->spk); T->b100 = 0; T->state = 2;
            } else T->state = 0xfffe;
            return;
        }
        T->afe4 -= 8;
        break;
    }
    (void)pr;
}

static void tally_tick(FrontEnd *F)
{
    FeTally *T = F->T;
    uint16_t pr = F->pressed;
    if (T->state == 1) {
        tally_fade(F, pr);
        if (T->state != 1) return;
        if (T->afe8 > 0) T->afe8 -= 8; else T->afe8 = 0;
        t_bc60(F); t_b710(F); spark_tick(F, 0x23f); t_a630(F); t_a3a0(F); t_9b40(F); t_bdc0(F);
        t_beb0(F); t_a180(F); t_a7c0(F); t_9c90(F); t_9fa0(F); t_b150(F); t_b900(F);
        if (T->b1d0 == 1 && (pr & PSX_CROSS)) { T->b1d0 = 4; fe_snd(F, 0x302); T->afe8 = 0x80; speech_stop(F); }
    } else if (T->state == 2) {
        tally_fade(F, pr);
        if (T->state != 2) return;
        if (T->afe8 > 0) T->afe8 -= 8; else T->afe8 = 0;
        t_bcf0(F); spark_tick(F, 0x23f); t_ac90(F); t_bab0(F);
        T->hdr2->frame = T->hdr2_s->frame = 0x59;
        if (T->b1d0 >= 1 && T->b1d0 <= 3 && (pr & PSX_START)) {
            /* Start saves the game at the next level (FUN_1000c9b0(level << 16 | difficulty << 8 | lives)) */
            for (int i = 0; i < 4; i++) T->vstate[i] = 6;
            int next = F->gs.level >= 10 ? 11 : F->gs.level == 3 ? 5 : F->gs.level + 1;
            save_game(F, next << 16 | F->gs.difficulty << 8 | F->gs.lives);
            T->b1d0 = 4; fe_snd(F, 0x302);
        } else if (T->b1d0 == 1 && (pr & PSX_CROSS)) {
            for (int i = 0; i < 4; i++) T->vstate[i] = 6;
            T->b1d0 = 4; fe_snd(F, 0x302); T->afe8 = 0x80;
        }
    } else if (T->state == 0xfffe) {
        objs_clear(F);
        free(F->T); F->T = NULL;
        flow_run(F, 0xd3);
    }
}

static void tally_draw(FrontEnd *F)
{
    memset(F->cv, 0, sizeof F->cv);
    if (!F->T) return;
    draw_bg(F, F->tally_bg, F->T->afe4, 0, 0);
    objs_draw(F);
    if (F->T->afe8 > 0) fill_rect(F, 0, 0, CW, CH, 0xffffffu, F->T->afe8, 1);
}

/* ------------------------------------------------------------------ credits (FUN_10001a70) */

static void credits_load(FrontEnd *F, int idx)
{
    char n[96];
    snprintf(n, sizeof n, "M:\\LANGUAGE\\GRAFIX\\MOVIE\\SCREENS\\%s.TGA", idx == 0 ? "endeasy" : "c%d");
    if (idx) { char m[96]; snprintf(m, sizeof m, n, idx); memcpy(n, m, sizeof n); }
    free(F->credit_bg); F->credit_bg = tga_load(F, n);
}

static void credits_enter(FrontEnd *F)
{
    fe_title_sounds(F, 0);
    F->cr_idx = F->gs.difficulty != 0;
    F->cr_count = F->gs.difficulty != 0 ? 0 : -0xf0;
    F->cr_fade = 0; F->cr_fadedir = 0; F->cr_state = 1;
    credits_load(F, F->cr_idx);
    F->mode = FE_CREDITS;
}

static void credits_tick(FrontEnd *F)
{
    uint16_t pr = F->pressed;
    if (F->cr_state == 1) {
        if (pr & PSX_START) { fe_snd(F, 0x303); F->cr_state = 4; return; }
        if (F->cr_idx < 2 && F->cr_fade < 0x80) F->cr_fade += 0x10;
        if (++F->cr_count > 0x3c) {
            if (++F->cr_idx == 0x1d) F->cr_idx = 0x1e;
            if (F->cr_idx < 0x20) { credits_load(F, F->cr_idx); F->cr_count = 0; F->cr_fade = 0x80; }
            else F->cr_state = 4;
        }
    } else if (F->cr_state == 4) {
        if (F->cr_fade > 0) F->cr_fade -= 0x10;
        else { free(F->credit_bg); F->credit_bg = NULL; flow_run(F, 0xce); }
    }
}

/* ------------------------------------------------------------------ cheats screen (FUN_100016f0) */

static FeObj *CHO[10];

static void cheats_objs(FrontEnd *F)     /* FUN_10001000 + FUN_10001300 */
{
    static const int SEL0[10][2] = {{7,1},{8,2},{9,3},{10,5},{11,6},{12,7},{13,8},{14,9},{15,10},{16,11}};   /* 0x10022030 */
    static const int SEL1[10][2] = {{20,1},{21,2},{22,3},{23,5},{24,6},{25,7},{26,8},{27,9},{28,10},{18,11}}; /* 0x10022080 */
    int m = F->gs.cheat_mode;
    if (!CHO[0]) {
        objs_clear(F);
        if (!m) { CHO[0] = obj_new(F, &F->cheatbank, 0x11); CHO[1] = obj_new(F, &F->cheatbank, 7); CHO[2] = obj_new(F, &F->cheatbank, 1);
                  CHO[3] = obj_new(F, &F->cheatbank, 2); CHO[4] = obj_new(F, &F->cheatbank, 3); }
        else { CHO[0] = obj_new(F, &F->cheatbank, 0x1d); CHO[1] = obj_new(F, &F->cheatbank, 0x14); CHO[2] = obj_new(F, &F->cheatbank, 0x13); }
        for (int i = 0; i < (m ? 3 : 5); i++) { CHO[5 + i] = obj_new(F, &F->cheatbank, CHO[i]->frame); CHO[5 + i]->flags |= 6; CHO[5 + i]->x = CHO[5 + i]->y = 0x10000; }
    }
    const int (*sel)[2] = m ? SEL1 : SEL0;
    CHO[1]->frame = sel[F->ch_idx][0];
    F->gs.level = sel[F->ch_idx][1];
    if (!m) {
        CHO[2]->frame = F->gs.cheats & 8 ? 1 : 4;
        CHO[3]->frame = F->gs.cheats & 0x10 ? 2 : 5;
        CHO[4]->frame = F->gs.cheats & 0x800000 ? 3 : 6;
    }
    int n = m ? 3 : 5;
    for (int i = 0; i < n; i++) {
        CHO[i]->depth = i < 2 ? 10 : (m ? 0x14 : 0);
        CHO[5 + i]->frame = CHO[i]->frame;
        CHO[5 + i]->depth = i < 3 ? 0x28 : 10;
        CHO[i]->bright = F->ch_fade; CHO[5 + i]->bright = F->ch_fade * 2;
    }
}

static void cheats_enter(FrontEnd *F)
{
    if (!F->cheat_bg) F->cheat_bg = tga_load(F, "CHEAT.TGA");
    if (!F->cheatbank.d) bank_load(&F->cheatbank, F->fs, "M:\\LANGUAGE\\GRAFIX\\CHOP\\TITLE\\CHEATS\\ANIMPSX.BIN");
    memset(CHO, 0, sizeof CHO);
    F->ch_idx = 0; F->ch_fade = 0; F->ch_state = 0;
    F->gs.cheats |= 0x80000000u;
    cheats_objs(F);
    F->mode = FE_CHEATS;
}

static void cheats_tick(FrontEnd *F)
{
    uint16_t pr = F->pressed;
    if (F->ch_state == 0) { if (F->ch_fade < 0x80) F->ch_fade += 0x10; else F->ch_state = 1; }
    else if (F->ch_state == 2) {
        if (F->ch_fade == 0) {
            objs_clear(F); memset(CHO, 0, sizeof CHO);
            F->gs.lives = F->gs.start_lives;
            /* back through the main menu, which decrements the level; state 0x28 -> 0x29 increments it again */
            F->gs.level--;
            F->entered = 1;
            flow_run(F, 0x29);
            return;
        }
        F->ch_fade -= 0x10;
    } else {
        if (!F->gs.cheat_mode) {
            if (pr & PSX_UP) { F->gs.cheats ^= 8; fe_snd(F, 0x302); }
            if (pr & PSX_LEFT) { F->gs.cheats ^= 0x10; fe_snd(F, 0x302); }
            if (pr & PSX_DOWN) { F->gs.cheats ^= 0x800000; fe_snd(F, 0x302); }
            if (pr & PSX_RIGHT) { F->ch_idx++; fe_snd(F, 0x301); }
        } else {
            if (pr & PSX_DOWN) { F->ch_idx++; fe_snd(F, 0x301); }
            if (pr & PSX_UP) { F->ch_idx--; fe_snd(F, 0x300); }
        }
        if (pr & PSX_START) F->ch_state = 2;
        if (F->ch_idx == -1) F->ch_idx = 9;
        if (F->ch_idx == 10) F->ch_idx = 0;
    }
    cheats_objs(F);
}

static void cheats_draw(FrontEnd *F)
{
    memset(F->cv, 0, sizeof F->cv);
    draw_bg(F, F->cheat_bg, F->ch_fade, 0, 0);
    objs_draw(F);
}

/* ------------------------------------------------------------------ saved game (PC: FUN_1000c9b0 / F3) */

static void save_path(char *out, size_t n)
{
    const char *home = getenv("HOME");
    snprintf(out, n, "%s/Library/Application Support/Hercules/savegame.dat", home ? home : ".");
}

static void save_game(FrontEnd *F, int code)
{
    char p[1024];
    save_path(p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (!f) return;
    fprintf(f, "%d\n", code);
    fclose(f);
    F->save_code = code;
}

void fe_save_load(FrontEnd *F)
{
    char p[1024];
    save_path(p, sizeof p);
    FILE *f = fopen(p, "rb");
    F->save_code = 0;
    if (!f) return;
    if (fscanf(f, "%d", &F->save_code) != 1) F->save_code = 0;
    fclose(f);
}

/* ------------------------------------------------------------------ game flow (FUN_100164b0) */

static void flow_videos(FrontEnd *F, const int *list, int n, int next, int skip_ends)
{
    F->flow = next;
    F->vskip_ends = skip_ends;
    F->vcross = 0;
    play_videos(F, list, n, FE_MENU);
}

static void new_game(FrontEnd *F)       /* menu "START GAME": gamestruct[0] = 0, state 0x28 */
{
    FeGame *g = &F->gs;
    g->level = 0; g->continues = 0; g->demo = 0;
    g->difficulty = F->difficulty;
    g->start_lives = F->cfg && F->cfg->lives > 0 ? F->cfg->lives : 3;
    g->lives = g->start_lives;
    F->acec = 0;
    flow_run(F, 0x29);
}

static void flow_run(FrontEnd *F, int st)
{
    FeGame *g = &F->gs;
    F->flow = 0;
    switch (st) {
    case 0x29: {    /* next level */
        g->demo = g->demo == 3 ? 3 : 0;
        g->vases = 0; g->scrolls = 0; g->coins14 = g->coins16 = g->coins18 = g->coins1a = 0; g->deaths = 0;
        g->level++;
        if (g->level == 4) g->level = 5;
        if (g->level < 12 && g->demo == 0) save_game(F, g->level << 16 | g->difficulty << 8 | g->lives);   /* autosave at every level start */
        if (g->level >= 10 && g->level <= 12 && g->difficulty == 0) { flow_run(F, 0xcf); return; }
        if (g->level >= 12) { flow_run(F, 0xfa); return; }
        card_enter(F, g->level);
        return;
    }
    case 0xd3: {    /* story video after a completed level */
        int v = 0;
        switch (g->level) {
        case 3: v = 12; break;
        case 4: case 5: v = 13; break;
        case 6: v = 14; break;
        case 7: v = 17; break;
        case 8: v = 19; break;
        case 9: if (g->difficulty == 0) { flow_run(F, 0xcd); return; } v = 25; break;
        case 10: v = 16; break;
        case 11: { int l[1] = {g->difficulty == 2 ? 15 : 27}; flow_videos(F, l, 1, 0xcd, 0); return; }
        }
        if (v) { int l[1] = {v}; flow_videos(F, l, 1, 0x29, 0); }
        else flow_run(F, 0x29);
        return;
    }
    case 0xcd: case 0xcf: credits_enter(F); return;     /* ending: credits slideshow */
    case 0xce: { int l[1] = {3}; flow_videos(F, l, 1, 0xf0, 0); return; }
    case 0xf0: F->entered = 1; fe_goto_menu(F); return;
    case 0xfa: {    /* level 12 (movie password): every story video, any skip returns to the menu */
        int l[8] = {18, 12, 13, 14, 11, 17, 19, 25};
        g->level = 0;
        F->vq2n = 2; F->vq2[0] = 16; F->vq2[1] = g->difficulty == 2 ? 15 : 27;
        flow_videos(F, l, 8, 0xfb, 1);
        return;
    }
    case 0xfb: flow_videos(F, F->vq2, F->vq2n, 0xf0, 1); return;
    case 200:       /* continue? */
        if (g->cheats & 0x10) g->continues = 1;
        if (g->continues == 0) { int l[2] = {9, 3}; flow_videos(F, l, 2, 0xf0, 1); return; }
        { int l[1] = {22}; flow_videos(F, l, 1, 0xc9, 0); }
        return;
    case 0xc9:
        if (F->vcross) {
            int l[1] = {21};
            g->continues--; g->lives = g->start_lives; g->level--;
            F->acec = 1;
            flow_videos(F, l, 1, 0x29, 0);
        } else { int l[2] = {9, 3}; flow_videos(F, l, 2, 0xf0, 1); }
        return;
    case 0xdc: {    /* attract mode: alternate a recorded demo and video 11 */
        if (g->demo == 1) { int l[1] = {11}; g->demo = 0; flow_videos(F, l, 1, 0xf0, 1); return; }
        static const int DT[7][2] = {{1, 1}, {1, 2}, {1, 3}, {2, 4}, {3, 5}, {5, 6}, {5, 7}};   /* 0x10026068 */
        if (g->demo_idx == 7) g->demo_idx = 0;
        int k = g->demo_idx > 6 ? rand_n(F, 7) : g->demo_idx++;
        static const char *const DF[8] = {"DEMO", "DEMO1_1", "DEMO1_2", "DEMO1_3", "DEMO2_1", "DEMO3_1", "DEMO5_1", "DEMO5_2"};
        char p[64]; snprintf(p, sizeof p, "M:\\DEMO\\%s.DAT", DF[DT[k][1]]);
        uint32_t sz = 0;
        const uint8_t *d = fs_get(F->fs, p, &sz);
        if (!d || sz < 0x6e8 || !fe_level_ported(F, DT[k][0])) { int l[1] = {11}; flow_videos(F, l, 1, 0xf0, 1); return; }
        free(F->demo_buf); F->demo_buf = malloc(sz); memcpy(F->demo_buf, d, sz);
        F->demo_pad = (const uint16_t *)F->demo_buf; F->demo_len = 0x374; F->demo_pos = 3;
        g->demo = 1; g->level = DT[k][0]; g->demo_var = DT[k][1];
        g->checkpoint = F->demo_buf[0];
        g->difficulty = F->demo_buf[4];
        g->lives = 3;
        F->start_level = g->level;
        F->mode = FE_PLAY;
        return;
    }
    default: F->entered = 1; fe_goto_menu(F); return;
    }
}

void fe_level_end(FrontEnd *F, int result, const FeStats *s)
{
    FeGame *g = &F->gs;
    sfx_speech_stop();
    F->title_sounds = 0;            /* the game freed the OPTIONS bank */
    if (g->demo == 1) { F->demo_pad = NULL; flow_run(F, 0xdc); return; }
    if (s) {
        g->lives = s->lives; g->deaths = s->deaths;
        g->coins14 = s->coins14; g->coins16 = s->coins16; g->coins18 = s->coins18; g->coins1a = s->coins1a;
        g->vases = s->vases & 0xf; g->scrolls = s->scrolls & 0xff;
    }
    if (result == FE_RES_DONE) { tally_enter(F); return; }
    if (result == FE_RES_GAMEOVER) {
        /* states 0x8c..0x96: a per-level scene, then the continue prompt (state 200) */
        static const int V[12] = {0, 4, 4, 8, 6, 6, 20, 6, 7, 8, 10, 10};
        int l[1] = {V[g->level >= 1 && g->level <= 11 ? g->level : 1]};
        flow_videos(F, l, 1, 200, 0);
        return;
    }
    F->entered = 1;
    fe_goto_menu(F);
}

int fe_wants_demo(const FrontEnd *F) { return F->mode == FE_PLAY && F->gs.demo == 1 && F->demo_pad; }

int fe_demo_pad(FrontEnd *F)    /* ENG1 FUN_10006005: words 3..0x373, any key ends the demo */
{
    if (!F->demo_pad || F->demo_pos >= F->demo_len) return -1;
    return F->demo_pad[F->demo_pos++];
}

void fe_load_game(FrontEnd *F)
{
    fe_save_load(F);
    if (!F->save_code) return;
    FeGame *g = &F->gs;
    g->level = (F->save_code >> 16 & 0xff) - 1;
    g->difficulty = F->save_code >> 8 & 0xff;
    g->lives = g->start_lives = F->save_code & 0xff;
    if (g->lives < 1) g->lives = g->start_lives = 3;
    g->continues = 0;
    F->acec = 1;
    fe_snd(F, 0x302);
    flow_run(F, 0x29);
}
