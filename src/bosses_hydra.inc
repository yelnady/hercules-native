/* HYDRA (level 6): FUN_1001ac10 (start), FUN_1001ae00 (per frame), the body (DAT_1006f3f4, FUN_1001b9a0 /
 * FUN_1001bab0), the necks (actor 0x29: FUN_1001cf50 init, FUN_1001d0b0 update, FUN_1001c720 neck shape,
 * FUN_1001dd50 curve animation, states FUN_1001da20 / 1001d780 / 1001da00 / 1001d170 / 1001d360), the heads
 * (0x2d: FUN_1001b2a0 / FUN_1001b7f0 / FUN_1001b370), regrowth (FUN_1001c3f0 / FUN_1001c4d0), falling rocks
 * (0x33: FUN_1001e500 / 1001e600, 0x2f) and the boulders that finish her (0x202e FUN_1001ea30 / 1001ea90).
 *
 * The fight: a neck lunges at Hercules; while it is stretched out (bite frames 0x26..0x38) a sword blow cuts
 * the head off. A cut neck shrivels and grows back, with an extra neck for the first few cuts. Eight cuts
 * bring the cliffs down on her.
 *
 * Simplifications (see objects_notes.md): the necks and heads are drawn as rigid map models at the positions
 * the original computes (the original shares vertices between neck segments and morphs the head and body
 * models: vertex transforms 2/3/6 of FUN_10031540); hit tests use world boxes instead of the original's
 * screen-space boxes (DAT_10070840). */

enum {
    HY_HEAD_TAB = 0x100574d8,       /* 0x54 per head: base x,y,z (16.16), rot, ..., +0x40/+0x44 player x range */
    HY_SCALE = 0x10057778,          /* neck scale per head (0x28 stride) */
    HY_LEN = 0x100578b8,            /* segment lengths [head*16 + seg] */
    HY_THR0 = 0x10057ab8, HY_THR1 = 0x10057ae8,   /* growth thresholds per segment */
    HY_CURVES = 0x10057498,         /* curve set -> rows of 11 s16 angle steps */
    HY_NECK_ANIMS = 0x10057450,     /* neck curve scripts (6-byte entries) */
    HY_HEAD_ANIMS = 0x100565f8,     /* head morph scripts */
    HY_NSEG = 10,
};

typedef struct {
    Object *neck;                   /* the 0x29 group object (base) */
    Object *seg[HY_NSEG];           /* 0xc028 neck segments (model 4) */
    Object *head;                   /* 0x2d head (models 11..17) */
    Object *stump;                  /* 0x30 bleeding stump (sprite) */
    int curveset;                   /* DAT_1006f194 */
    int lunge_base, lunge_x;        /* DAT_1006f198 / DAT_1006f19c */
    int ext_len, ext_x;             /* DAT_1006f1a0 / DAT_1006f1a4 */
    int bite;                       /* DAT_1006f1a8: 0 idle, 1 lunging, 2 snap, 3 recover */
    int dying;                      /* DAT_1006f1ac */
    int snap_t, lunge_t;            /* DAT_1006f1b0 / DAT_1006f1b4 */
    int tracking;                   /* DAT_1006f1b8 */
    int roared;                     /* DAT_1006f1bc */
    /* curve script state (+0x64, +0x30/+0x32, +0xd8/+0xdc/+0xe0/+0xf0/+0xf4/+0xf8) */
    uint32_t script; int sidx, stime, rowA, rowB, blend, bstart, btarget, bstep;
    /* head morph script */
    uint32_t hscript; int hidx, htime, hA, hB;
    int hEnd, hW, hStep, hStart;            /* FX2: morph weight +0x4c, its end (+4 of the anim struct) and per-tick step (+6) */
} HyHead;

static struct {
    Object *body, *floor;
    HyHead h[8];
    int heads_left;                 /* DAT_1006efb8 */
    int regrow;                     /* DAT_1006f44c */
    int final_state, final_t;       /* DAT_1006f3e0 / DAT_1006f458 */
    int rock_t;                     /* DAT_1006f4a4 */
    int quake;                      /* DAT_1006f180 */
    int landed;                     /* DAT_1006f454 */
    int first_cut;                  /* DAT_1006f3e4 */
    int loop;                       /* DAT_1006f3e8 */
    int frame;                      /* DAT_1006f45c */
    int snd_cut, snd_grow;          /* DAT_1006f464 / DAT_1006f460 */
} H;

static int hy_tab(Objects *O, int k, int i) { return s32(O->pe, HY_HEAD_TAB + 0x54u * (uint32_t)k + 4u * (uint32_t)i); }

/* FUN_1001dd50: neck curve script; returns 1 when it loops. */
static int hy_curve_step(Objects *O, HyHead *h)
{
    const PeImage *pe = O->pe;
    int ret = 0;
    if (--h->stime >= 0) {
        if (h->stime) h->blend += h->bstep;
        else if (h->bstart >= 0) h->blend = h->btarget << 8;
        return 0;
    }
    int v = 0;
    for (int guard = 0; guard < 32; guard++) {
        h->sidx++;
        uint32_t e = h->script + 6u * (uint32_t)h->sidx;
        v = pe_s16(pe, e);
        if (v >= 0) break;
        if (v == -0x2a) { h->curveset = pe_s16(pe, e + 2); h->script = pe_u32(pe, HY_NECK_ANIMS + 4u * (uint32_t)h->curveset); h->sidx = -1; ret = 1; }
        else if (v == -0x18) { h->rowB = pe_s16(pe, e + 4); h->btarget = pe_s16(pe, e + 2); }
        else if (v == -4 || v == -1) { h->sidx = -1; ret = 1; }
    }
    uint32_t e = h->script + 6u * (uint32_t)h->sidx;
    h->rowA = v;
    int dur = pe_s16(pe, e + 4);
    if (dur & 0x4000) dur = (dur & 0x3fff) + obj_rnd(O, 6);
    if (h->tracking && h->curveset < 8 && (dur /= 3) < 1) dur = 1;
    h->stime = dur;
    int b = pe_s16(pe, e + 2);
    if (b < 0) { h->bstart = b << 8; h->bstep = -b * 0x100; return ret; }
    h->blend = b << 8; h->bstart = b << 8;
    h->bstep = ((h->btarget - b) * 0x100) / (dur < 1 ? 1 : dur);
    return ret;
}

static void hy_set_curves(Objects *O, HyHead *h, int k, int off)     /* FUN_1001d070 */
{
    h->curveset = off + k;
    h->script = pe_u32(O->pe, HY_NECK_ANIMS + 4u * (uint32_t)(off + k));
    h->sidx = -1; h->stime = 0;
}

/* Head morph script (FUN_10028aa0, subset): {model A, blend, ticks}; -0x18 sets model B; -0x2a jumps. */
static void hy_head_anim(Objects *O, HyHead *h, int a)
{
    h->hscript = pe_u32(O->pe, HY_HEAD_ANIMS + 4u * (uint32_t)a);
    h->hidx = -1; h->htime = 0;
}

static void hy_head_step(Objects *O, HyHead *h)                 /* FUN_10028aa0 (model scripts with morphing) */
{
    const PeImage *pe = O->pe;
    if (!h->hscript) return;
    if (--h->htime >= 0) {                                      /* between keys: weight moves by the step; last tick lands on the end */
        if (h->htime == 0) { if (h->hStart >= 0) h->hW = h->hEnd; }
        else h->hW += h->hStep;
    } else {
        int v = 0;
        for (int guard = 0; guard < 32; guard++) {
            h->hidx++;
            uint32_t e = h->hscript + 6u * (uint32_t)h->hidx;
            v = pe_s16(pe, e);
            if (v >= 0) break;
            if (v == -0x18) { h->hEnd = pe_s16(pe, e + 2); h->hB = pe_s16(pe, e + 4); }   /* +4 end weight, +0x36 target model */
            else if (v == -0x20) h->hW = pe_s16(pe, e + 4);
            else if (v == -0x2a) { hy_head_anim(O, h, pe_s16(pe, e + 2)); h->hidx = -1; }
            else if (v == -1 || v == -4 || v == 0) h->hidx = -1;
        }
        uint32_t e = h->hscript + 6u * (uint32_t)h->hidx;
        h->hA = v;
        int dur = pe_s16(pe, e + 4);
        if (dur & 0x4000) dur = (dur & 0x3fff) + obj_rnd(O, 6);
        h->htime = dur;
        int start = pe_s16(pe, e + 2);
        h->hStart = start;
        if (start < 0) h->hStep = -start;                       /* negative: keep the weight, step by -start per tick */
        else { int t = dur < 1 ? 1 : dur; h->hW = start; h->hStep = (h->hEnd - start) / t; }
    }
    if (h->head) {                                              /* FX2: key model A morphing toward model B by hW/256 */
        h->head->frame = (int16_t)h->hA;
        h->head->morph_model = (int16_t)h->hB;
        h->head->morph_w = h->hW;
    }
}

static int hy_player_in_range(Objects *O, int k)                /* FUN_1001cf10 */
{
    int px = I(O->P->o.x);
    return hy_tab(O, k, 16) <= px && px <= hy_tab(O, k, 17);
}

/* FUN_1001c720: lay the neck out from its base and put the head on the end. */
static void hy_shape(Objects *O, int k)
{
    HyHead *h = &H.h[k];
    Object *n = h->neck;
    const PeImage *pe = O->pe;
    if (!n) return;
    uint32_t curves = pe_u32(pe, HY_CURVES + 4u * (uint32_t)h->curveset);
    int ang = n->rot[0], a24 = -0x200, a1c = -0x200, a18 = ang;
    int32_t x = n->x, y = n->y, z = n->z;
    int cnt = n->count126, st = n->v2a, len = 0, falling = 0;
    for (int i = 1; i < HY_NSEG; i++) {
        int sway = sin_t(O, (n->w[0] + i * 0x66) & 0x3ff);
        int L = s32(pe, HY_LEN + 4u * (uint32_t)(k * 16 + i));
        int t0 = s32(pe, HY_THR0 + 4u * (uint32_t)i), t1 = s32(pe, HY_THR1 + 4u * (uint32_t)i);
        x = n->x;
        switch (st) {
        case 0: case 1:
            len = cnt < t0 ? 0 : cnt > t1 ? L : (t1 > t0 ? L * (cnt - t0) / (t1 - t0) : L);
            if (st == 1 && cnt >= t0 && cnt <= t1) falling = 1;
            if (st == 0 && i == 1 && cnt < 0x18) len += (0x18 - cnt) * (k == 1 ? 0x6e : k == 3 ? 0x28 : k == 5 ? 0x72 : 0) / 0x18;
            x += sway * i * (st == 1 ? 0x40 : 6);
            break;
        case 2: len = 0; break;
        case 3: len = L; x += sway * i * 6; break;
        case 4:
            len = L + h->ext_len;
            x += h->ext_x * i / 10;
            if (cnt < 0x11) x += (sway * (0x10 - cnt) * i * 6) >> 4;
            else if (cnt >= 0x38) x += (sway * (cnt - 0x38) * i * 6) >> 3;
            break;
        }
        y += sin_t(O, a18 & 0x3ff) * 2 * len;
        z -= sin_t(O, (a18 + 0x100) & 0x3ff) * 2 * len;
        Object *s = h->seg[i];
        if (s) { s->x = x; s->y = y; s->z = z; }
        if (curves) {
            a24 += pe_s16(pe, curves + 2u * (uint32_t)(i + h->rowA * 11));
            a1c += pe_s16(pe, curves + 2u * (uint32_t)(i + h->rowB * 11));
        }
        a18 = ang - (int)(((long)(a1c - a24) * h->blend) >> 16) - a24;
        if (s) s->rot[0] = (int16_t)(a18 + 0x200);
    }
    (void)falling;
    h->tracking = 0;
    Object *hd = h->head;
    if (hd && st != 1 && st != 2 && !h->dying) {
        hd->x = x; hd->y = y; hd->z = z;
        if (H.heads_left < 1) {
            int d = angle_diff(0x180, hd->rot[0]);
            if (d > 8) d = 8;
            if (d < -8) d = -8;
            hd->rot[0] = (int16_t)(hd->rot[0] - d);
            if (!h->roared) {       /* FUN_1001c720: the head has turned up; the first one roars (0x19e, once for the fight) */
                h->roared = 1;
                if (H.snd_grow) { H.snd_grow = 0; obj_sound(O, 0x19e, 0); }
            }
            if (obj_rnd(O, 0x78) == 0) obj_sound(O, 0x184, 0);      /* AUDSHELL: FUN_1001c720, 1 in 0x78 per tick */
        } else hd->rot[0] = (int16_t)(pe_s16(pe, HY_HEAD_TAB + 0x54u * (uint32_t)k + 0x34) + a18);
        int turn;
        if (hy_player_in_range(O, k)) {
            int a = angle_to(O, hd->z, hd->x, O->P->o.z, O->P->o.x);
            turn = angle_diff(a + 0x200, hd->rot[1]);
            int lim = (st == 4 && cnt > 0x25) ? 0 : 0xc;
            if (turn > lim) turn = lim;
            if (turn < -lim) turn = -lim;
            h->tracking = 1;
        } else {
            turn = angle_diff((sin_t(O, (n->w[0] * 2 + 0x100) & 0x3ff) << 7) >> 16, hd->rot[1]);
            if (turn > 4) turn = 4;
            if (turn < -4) turn = -4;
        }
        hd->rot[1] = (int16_t)(hd->rot[1] - turn);
    }
}

static void hy_kill_head(Objects *O, int k)                     /* FUN_1001d5f0 (head drops) */
{
    HyHead *h = &H.h[k];
    if (!h->head) return;
    h->dying = 1;
    hy_head_anim(O, h, 4);
    h->head->w[0] = obj_rnd(O, 10) - 5; h->head->w[1] = -4; h->head->w[2] = obj_rnd(O, 6) - 3;
    if (H.snd_cut) {
        H.snd_cut = 0;
        obj_sound(O, obj_rnd(O, 2) ? 0x5995 : 0x5994, h->head->x);
        obj_sound(O, 0x5993, h->head->x);
        obj_sound(O, 0x1a2, h->head->x);
    }
}

static void hy_spawn_head(Objects *O, int k)                    /* FUN_1001c4d0 */
{
    HyHead *h = &H.h[k];
    memset(h, 0, sizeof *h);
    h->snap_t = obj_rnd(O, 0x1e) + 0x1e;
    Object *n = bspawn(O, 0x29, hy_tab(O, k, 0), hy_tab(O, k, 1), hy_tab(O, k, 2), R_HYDRA_NECK);
    if (!n) return;
    h->neck = n;
    n->variant = k;
    n->flags54 = 0xb0001000; n->frame = 4;
    n->rot[0] = (int16_t)(hy_tab(O, k, 3) + 0x200); n->rot[1] = (int16_t)hy_tab(O, k, 4); n->rot[2] = (int16_t)hy_tab(O, k, 5);
    int sc = pe_s16(O->pe, HY_SCALE + 0x28u * (uint32_t)k);
    n->scale = (uint16_t)sc;
    n->w[0] = obj_rnd(O, 0x400);                                /* +0x98 sway phase */
    for (int i = 1; i < HY_NSEG; i++) {                         /* 0xc028 children, model 4 */
        Object *s = bspawn(O, 0x4028, n->x, n->y, n->z, R_HYDRA_FX);
        if (!s) break;
        s->flags54 = 0x90000000; s->frame = 4; s->scale = (uint16_t)sc; s->variant = 9;
        s->rot[1] = n->rot[1];
        h->seg[i] = s;
    }
    Object *hd = bspawn(O, 0x2d, n->x, n->y, n->z, R_HYDRA_HEAD);  /* FUN_1001b2a0 */
    if (hd) {
        hd->variant = k;
        hd->flags54 = 0x90001040; hd->scale = H.regrow == 0 ? 0xd4 : 0x90; hd->w[5] = hd->scale;
        hd->frame = 11;
        h->head = hd;
        hy_head_anim(O, h, 0);
    }
    if (H.regrow == 0) { hy_set_curves(O, h, k, 0); n->v2a = 3; n->count126 = 0x3c; }   /* FUN_1001cf50 */
    else {
        hy_set_curves(O, h, k, 0); n->v2a = 0; n->count126 = 0;
    }
    hy_shape(O, k);
}

static void hy_regrow(Objects *O, int k)                        /* FUN_1001c3f0 */
{
    int extra = -1;
    switch (H.regrow) {
    case 0: hy_spawn_head(O, 7); H.regrow++; return;
    case 1:
        hy_spawn_head(O, 3); hy_spawn_head(O, 1); hy_spawn_head(O, 5);
        H.regrow += 3;
        return;
    case 4: extra = 2; break;
    case 5: extra = 4; break;
    case 6: extra = 0; break;
    case 7: extra = 6; break;
    default: break;
    }
    hy_spawn_head(O, k);
    if (extra >= 0) { hy_spawn_head(O, extra); H.regrow++; }
}

static void hy_free_neck(Objects *O, int k)
{
    HyHead *h = &H.h[k];
    for (int i = 0; i < HY_NSEG; i++) if (h->seg[i]) { bkill(O, h->seg[i]); h->seg[i] = NULL; }
    if (h->neck) { bkill(O, h->neck); h->neck = NULL; }
    if (h->stump) { bkill(O, h->stump); h->stump = NULL; }
}

/* FUN_1001b370: a sword blow on a stretched-out head cuts it off. */
static void hy_head_hits(Objects *O, int k)
{
    HyHead *h = &H.h[k];
    Object *n = h->neck, *hd = h->head;
    if (!n || !hd || n->v2a != 4 || n->count126 < 0x26 || n->count126 > 0x38) return;
    int s = (hd->scale ? hd->scale : 0x100) * 2;
    int r = 0x18 * s / 0x200 + 8;
    int X = I(hd->x), Y = I(hd->y);
    /* The original compares screen-space boxes (DAT_10070840): depth is ignored, and the head, which sits further
     * back than Hercules, projects lower than its height. Emulate that: no z test, the player's boxes reach 64 up. */
    int hb[6] = {X - r, Y - r, -0x10000, X + r, Y + r, 0x10000}, pb[6];
    player_box6(O->P, pb);
    pb[1] -= 0x40; pb[2] = -0x10000; pb[5] = 0x10000;
    if (obj_boxes_overlap(hb, pb)) player_damage(O->P, 0x22, hd->x, hd->y, hd->z, 0);
    Box ab;
    if (!player_attack_box(O->P, &ab)) return;
    int a[6] = {ab.x0, ab.y0 - 0x40, -0x10000, ab.x1, ab.y1, 0x10000};
    if (!obj_boxes_overlap(a, hb)) return;
    if (!player_hit_damage(O->P, (uint16_t)(0x18 & ~O->P->consumed)) && !getenv("BOSS_WEAK")) return;   /* sword only */
    int id = drop_list_next(O);
    if (id) boss_drop(O, hd->x, hd->y, O->P->o.z, (uint32_t)id | (k < 2 ? 0x220u : k > 4 ? 0x240u : 0x280u));
    obj_sound(O, (uint16_t)(0x5985 + obj_rnd(O, 3)), hd->x);
    /* FUN_1001d6c0: cut */
    if (O->P->dying) return;
    n->v2a = 1; n->count126 = 0x3f;
    Object *st = bspawn(O, 0x30, hd->x, hd->y, hd->z, R_HYDRA_FX);   /* FUN_1001df50 */
    if (st) { st->variant = 3; st->frame = 0x15; st->timer = 3; h->stump = st; }
    H.heads_left--;
    if (H.heads_left < 1) B.dead = 1;
    bar_hit(O, H.heads_left < 1 ? B.bar : 0x20);
    boss_bar(O, H.heads_left, 8);
    hy_kill_head(O, k);
}

static void hy_neck_tick(Objects *O, Object *n)                 /* FUN_1001d0b0 */
{
    int k = n->variant;
    HyHead *h = &H.h[k];
    if (H.landed > 0) { hy_kill_head(O, k); hy_free_neck(O, k); return; }   /* the boulders landed on her */
    H.loop = hy_curve_step(O, h);
    hy_shape(O, k);
    switch (n->v2a) {
    case 0:                                                     /* FUN_1001da20: growing */
        n->count126++; n->w[0] += 0x14;
        if (n->count126 < 0x40) {
            if (h->head) { int s = (h->head->w[5] - 2) * n->count126 / 64 + 2; h->head->scale = (uint16_t)(s < 2 ? 2 : s); }
        } else {
            if (h->head) h->head->scale = 0xd4;
            n->v2a = 3; n->count126 = (int16_t)(obj_rnd(O, 0x32) + 0x32);
        }
        break;
    case 1:                                                     /* FUN_1001d780: cut, shrivelling */
        if (n->count126 < 1) { n->v2a = 2; if (h->stump) { bkill(O, h->stump); h->stump = NULL; } }
        else {
            n->w[0] += 0x50;
            n->count126--;
            if (n->count126 == 1 && h->head && h->head->used && h->dying) n->count126 = 2;   /* wait for the head to land */
        }
        if (h->stump && h->seg[HY_NSEG - 1]) { h->stump->x = h->seg[HY_NSEG - 1]->x; h->stump->y = h->seg[HY_NSEG - 1]->y; h->stump->z = h->seg[HY_NSEG - 1]->z; }
        break;
    case 2:                                                     /* FUN_1001da00: regrow */
        if (H.heads_left > 0) { hy_free_neck(O, k); hy_regrow(O, k); }
        return;
    case 3: {                                                   /* FUN_1001d170: idle, picks a bite */
        n->w[0] += 6;
        if (H.loop) {
            if (--h->lunge_t < 1 && !O->P->dying && hy_player_in_range(O, k) && h->bite == 0 && H.heads_left > 0) {
                int tx = I(O->P->o.x) + obj_rnd(O, 0x20) - 0x10 + ((hy_tab(O, k, 6) * 0x80) >> 16);
                if (tx < 0x480) tx = 0x480;
                if (tx > 0xcf0) tx = 0xcf0;
                int ok = 1;
                for (int j = 0; j < 8; j++)
                    if (j != k && H.h[j].neck && H.h[j].neck->v2a == 4 && iabs(H.h[j].lunge_x - tx) < 0x7c) ok = 0;
                if (ok) {
                    h->lunge_base = I(n->x); h->lunge_x = tx;
                    n->v2a = 4; n->count126 = 0;
                    hy_set_curves(O, h, k, 8);
                    h->bite = 1;
                }
            }
        }
        if (--h->snap_t < 1 && hy_player_in_range(O, k) && h->lunge_t >= 0 && h->bite == 0 && H.heads_left > 0) h->bite = 2;
        break;
    }
    case 4: {                                                   /* FUN_1001d360: the bite */
        n->w[0] += 6;
        int c = n->count126++;
        if (c == 0x1c) { hy_head_anim(O, h, 2); h->bite = 0; obj_sound(O, (uint16_t)(0x598c + obj_rnd(O, 4)), n->x); }
        if (c == 0x28) obj_sound(O, (uint16_t)(0x5196 + obj_rnd(O, 4)), n->x);
        if (c == 0x2d && obj_rnd(O, 2) == 0) obj_sound(O, obj_rnd(O, 2) ? 0x51a1 : 0x51a0, n->x);
        int reach = hy_tab(O, k, 9);
        if (c > 0x1b && c < 0x27) { h->ext_x = FX(h->lunge_x - h->lunge_base) * (c - 0x1c) / 10; h->ext_len = reach * (c - 0x1c) / 10; }
        if (c > 0x34 && c < 0x40) { h->ext_x = FX(h->lunge_x - h->lunge_base) * (0x3f - c) / 10; h->ext_len = reach * (0x3f - c) / 10; }
        if (H.loop || c > 0x50) {
            n->v2a = 3; hy_set_curves(O, h, k, 0);
            h->ext_len = 0; h->ext_x = 0; h->lunge_t = 0;
        }
        break;
    }
    }
    hy_head_hits(O, k);
}

static void hy_head_tick(Objects *O, Object *hd)                /* FUN_1001b7f0 */
{
    HyHead *h = &H.h[hd->variant];
    if (h->head != hd) { bkill(O, hd); return; }
    hy_head_step(O, h);
    if (h->bite == 1 && h->neck && h->neck->v2a == 4) { /* jaws open while lunging */ }
    else if (h->bite == 2) { hy_head_anim(O, h, 1); obj_sound(O, 0x5984, hd->x); h->bite = 3; }
    else if (h->bite == 3 && h->htime <= 0) { h->bite = 0; h->snap_t = obj_rnd(O, 0x1e) + 0x1e; }
    if (!h->dying) return;
    hd->x += FX(hd->w[0]); hd->y += FX(hd->w[1]); hd->z += FX(hd->w[2]);
    if (hd->w[1] < 0x20) hd->w[1]++;
    hd->rot[0] = (int16_t)(hd->rot[0] + 0x10);
    if (hd->y > 0x3d7ffff) {
        obj_sound(O, 0x188, hd->x);
        hd->w[0] /= 2; hd->w[1] = (-hd->w[1] * 2) / 3; hd->y = FX(hd->w[1] + 0x3d8);
        if (++hd->w[3] > 1) { h->head = NULL; h->dying = 0; bkill(O, hd); }
    }
}

static void hy_fx_tick(Objects *O, Object *o)
{
    switch (o->variant) {
    case 3:                                                     /* FUN_1001df80: stump spurting */
        if (--o->timer < 1) { o->timer = 3; if (++o->frame > 0x1c) o->frame = 0x15; }
        break;
    case 4:                                                     /* 0x2f / 0x33 rocks, FUN_1001e3f0 / FUN_1001e600 */
    case 5: {
        o->y += FX(o->w[0]); o->w[0] += o->variant == 4 ? 2 : 1;
        for (int i = 0; i < 3; i++) o->rot[i] = (int16_t)((o->rot[i] + o->w[1 + i]) & 0xfff);
        int hit = 0;
        if (o->variant == 5) {
            int pb[6]; player_box6(O->P, pb);
            int X = I(o->x), Y = I(o->y), Z = I(o->z), r = o->w[4] + 8;
            int b[6] = {X - r, Y - r, Z - r * 2, X + r, Y + r, Z + r * 2};
            hit = obj_boxes_overlap(b, pb);
        }
        if (o->y < 0x3d80000 && !hit) break;
        if (hit) player_damage(O->P, 0, o->x, o->y, o->z, 0);
        else o->y = 0x3d80000;
        H.quake = H.quake > 0 ? H.quake : 0;
        int r9 = obj_rnd(O, 9);
        if (o->variant == 5 || obj_rnd(O, 2) == 0) obj_sound(O, (uint16_t)(r9 < 3 ? 0x188 : r9 < 6 ? 0x189 : 0x18a), o->x);
        if (o->variant == 5) {
            Object *d = fx_spawn(O, 0x202d, o->x, o->y, o->z, 0, 0, 1, 1);
            (void)d;
            if (!hit) { int id = drop_list_next(O); if (id) boss_drop(O, o->x, o->y, O->P->o.z, (uint32_t)id | (I(O->P->o.x) < 0x4e0 ? 0x220u : I(O->P->o.x) >= 0xca0 ? 0x240u : 0x280u)); }
        }
        bkill(O, o);
        break;
    }
    case 6:                                                     /* 0x202e: a boulder, FUN_1001ea90 */
        if (o->w[1]) break;
        o->y += FX(o->w[0]); o->w[0] += 2;
        if (I(o->y) >= o->w[2]) {
            o->y = FX(o->w[2]); o->w[1] = 1;
            obj_sound(O, 0x188, o->x);
            if (++H.landed == 1) { obj_sound(O, 0x19f, o->x); obj_sound(O, 0x193, o->x); }
        }
        break;
    default: break;
    }
}

static void hydra_start(Objects *O)                             /* FUN_1001ac10 */
{
    memset(&H, 0, sizeof H);
    H.snd_cut = H.snd_grow = 1;
    Object *b = bspawn(O, 0x2b, 0x8800000, 0x34d0000, 0x3000000, R_HYDRA_BODY);   /* FUN_1001b9a0: the body */
    if (b) { b->flags54 = 0x90000040; b->frame = 0x22; b->scale = 0x200; b->rot[0] = (int16_t)0xfff0; H.body = b; B.boss = b; }
    Object *f = bspawn(O, 0x202a, 0x4000000, 0x3e00000, 0x2900000, R_HYDRA_FX);    /* FUN_1001dc80: the floor */
    if (f) { f->variant = 0; f->flags54 = 0x90000000; f->rot[1] = 0x200; f->scale = 0x3ff; f->frame = 3; H.floor = f; }
    H.heads_left = getenv("BOSS_WEAK") ? 1 : 8;
    H.regrow = 0;
    hy_regrow(O, 7);
    obj_sound(O, 0x180, 0); obj_sound(O, 0x193, 0);
    static const uint32_t DROPS[3] = {0x10056048, 0x100560e0, 0x10056168};
    int d = O->P->difficulty; if (d < 0 || d > 2) d = 1;
    drop_list_set(DROPS[d]);
    boss_bar(O, H.heads_left, 8);
}

static void hydra_level_tick(Objects *O)                        /* FUN_1001ae00 + FUN_1001e140 */
{
    H.final_t--; H.frame--;
    if (H.heads_left > 0 && --H.rock_t < 1) {                   /* rocks shaken loose from the cliffs */
        H.rock_t = obj_rnd(O, 0x4b) + 0x1e;
        int dx = O->P->o.vx == 0 ? obj_rnd(O, 0x60) - 0x30 : (O->P->o.facing ? -(obj_rnd(O, 0x40) + 0xea) : obj_rnd(O, 0x40) + 0xea);
        int32_t x = FX(dx) + O->P->o.x;
        if (x < 0x4200000) x = 0x4200000;
        if (x > 0xd600000) x = 0xd600000;
        Object *r = bspawn(O, 0x33, x, 0xc00000, O->P->o.z, R_HYDRA_FX);     /* FUN_1001e500 */
        if (r) {
            static const int FR[4] = {0x42, 0x44, 0x40, 0x45}, SZ[4] = {0x14, 0x20, 0xf, 0x18};
            int q = obj_rnd(O, 4);
            r->variant = 5; r->flags54 = 0x90000000; r->frame = (int16_t)FR[q]; r->w[4] = SZ[q];
            for (int i = 0; i < 3; i++) { r->rot[i] = (int16_t)obj_rnd(O, 0x1000); r->w[1 + i] = obj_rnd(O, 0x40) - 0x20; }
        }
    }
    if (H.quake > 0) {                                          /* the cliffs come down */
        H.quake--;
        if ((H.frame & 3) == 0) {
            Object *r = bspawn(O, 0x2f, FX(obj_rnd(O, 0x1000)), 0xc00000, (0x100 - obj_rnd(O, 0x80)) * 0x10000, R_HYDRA_FX);
            if (r) {
                static const int FR[4] = {0x42, 0x44, 0x40, 0x45};
                r->variant = 4; r->flags54 = 0x90000000; r->frame = (int16_t)FR[obj_rnd(O, 4)];
                for (int i = 0; i < 3; i++) { r->rot[i] = (int16_t)obj_rnd(O, 0x1000); r->w[1 + i] = obj_rnd(O, 0x40) - 0x20; }
            }
        }
    }
    if (H.heads_left >= 1) return;
    switch (H.final_state) {                                    /* FUN_1001ae00: boulders finish her */
    case 0: H.final_t = 0x3c; H.quake = 900; H.final_state = 1; break;
    case 1: case 2: case 3: case 4: case 5:
        if (H.final_t < 1) {
            static const int32_t BX[5] = {0xf000000, 0x8a0000, 0x8a00000, 0xc180000, 0x4ca0000};
            static const int32_t BZ[5] = {0x26f0000, 0x1340000, 0xda0000, 0xda0000, 0xda0000};
            static const int SC[5] = {0x3ff, 0x27b, 0x180, 0x1dd, 0x1dd}, FRM[5] = {0x48, 0x4b, 0x48, 0x4c, 0x4d}, YT[5] = {0x3ae, 0x3ae, 0x3bb, 0x3c1, 0x3bf};
            int i = H.final_state - 1;
            Object *bo = bspawn(O, 0x202e, BX[i], 0x500000, BZ[i], R_HYDRA_FX);     /* FUN_1001ea30 */
            if (bo) { bo->variant = 6; bo->flags54 = 0x90000000; bo->rot[1] = 0x3c0; bo->scale = (uint16_t)SC[i]; bo->frame = (int16_t)FRM[i]; bo->w[2] = YT[i]; }
            H.final_t = H.final_state == 5 ? 0x3c : 0x1e;
            if (H.final_state == 1) sfx_speech(O->fs, 0x2a);        /* AUDSHELL: FUN_1002c4c0(0x2a), her last scream */
            H.final_state++;
        }
        break;
    case 6:
        if (H.final_t < 1) { boss_defeated(O); H.final_state++; }
        break;
    }
    if (H.landed > 1 && H.body) { bkill(O, H.body); H.body = NULL; }
}

static int hydra_role_tick(Objects *O, Object *o)
{
    switch (o->ai) {
    case R_HYDRA_NECK: hy_neck_tick(O, o); return 1;
    case R_HYDRA_HEAD: hy_head_tick(O, o); return 1;
    case R_HYDRA_BODY: return 1;
    case R_HYDRA_FX: hy_fx_tick(O, o); return 1;
    }
    return 0;
}

static int hydra_draw(Objects *O, Renderer *R, Object *o)
{
    if (B.level != 6) return 0;
    if (o->ai == R_HYDRA_FX && o->variant == 3) return 0;      /* stump: a sprite */
    if (o->ai == R_HYDRA_FX && o->variant == 9) return 1;      /* FX2: neck segments are drawn by their rig */
    if (o->ai == R_HYDRA_NECK && o->variant >= 0 && o->variant < 8) {
        /* FX2: FUN_10031470 - the rig (0x29, model 4) only transforms its vertices (transform 5 on the odd necks,
         * which glue them to the body; 0 on the regrown even ones); each 0xc028 segment then draws with transform 2
         * and list 0x10056240, so its vertices b take the previous member's vertices a: a seamless neck. */
        HyHead *h = &H.h[o->variant];
        Vec3 W[64]; int wv = 0;
        boss_model_chain(O, NULL, o, 5, 0, W, &wv);
        for (int i = 1; i < HY_NSEG; i++) {
            Object *sg = h->seg[i];
            if (!sg || !sg->used) continue;
            if (sg->x || sg->y || sg->z) boss_model_chain(O, R, sg, 2, 0x10056240u, W, &wv);
        }
        return 1;
    }
    return 0;
}
