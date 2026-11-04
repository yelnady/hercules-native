/* MEDUSA (level 7): FUN_10021050 (start), FUN_10021210 (per frame), Medusa 0x2e (FUN_10021310 / 10021320,
 * anim list 0x10059398, callbacks 0x10059fa0), her gaze (rig 0x2f, FUN_10022190 / 10022280 / 10022440),
 * the eight columns with their mirror shields (0x30 pieces FUN_10023120, 0x2d shield FUN_10022dd0), the
 * rock rain (0x34 FUN_10021c00 / 10021d10, shadow 0x202f), the snakes she spits (0x33 FUN_10023460),
 * the skeleton warriors (0x32 FUN_10023a50 / 10023b30) and her death (0x31 FUN_100213c0 / 10021400).
 *
 * The fight: Medusa sits in the middle of the ring and turns with Hercules. Her gaze runs out along the
 * floor towards him; it breaks the column it meets, and the column's shield falls to the ground. A gaze
 * that meets a fallen shield is reflected back at her (one use per shield): eight reflections kill her. */

#define MX 0x1000                   /* ring length in path units */

static void medusa_draw_beams(Objects *O, Renderer *R);

static struct {
    Object *medusa, *rig, *kid[6];
    int lives;                      /* DAT_1006fe20 */
    int skel_timer, skels;          /* DAT_1006fde4 / DAT_1006fde8 */
    int gaze_pending;               /* DAT_1006fe08 */
    int gaze_delay;                 /* DAT_1006fe28 */
    int t;                          /* DAT_1006fe2c */
    int frame;                      /* DAT_1006fe30 */
    int hurt;                       /* DAT_1006fe10 */
    int shake, quake;               /* DAT_1006fde0 / DAT_1006fe1c */
    int first_reflect;              /* DAT_1006fe14 */
    uint32_t pat_base, pat;         /* DAT_1006fe0c / DAT_1006fcf0 attack pattern */
    int beam[2][3];                 /* DAT_1006fdf0.. / DAT_1006fdfc..: {state, column, shield} */
    Object *beam_shield[2];
    struct { Object *p[4]; Object *shield; int state, landed, broken; } col[8];   /* DAT_1006fd00, 7 ints each */
    int snake_h_last;               /* DAT_1006fe54 */
} M;

static Object *live(Objects *O, Object *q, int ai)             /* pointer still names the object we spawned */
{
    if (!q || q < O->obj || q >= O->obj + OBJ_MAX || !q->used || q->kind != K_BOSS || q->ai != ai) return NULL;
    return q;
}

static int ring_d(int32_t a, int32_t b)                         /* FUN_100217e0: b - a around the ring */
{
    int d = I(b) - I(a);
    d &= MX - 1;
    if (d > MX / 2 - 1) d -= MX;
    return d;
}

/* Box tests across the ring seam: shift box a next to the player. */
static int ring_hits_player(Objects *O, const Object *o, int k)
{
    int b[6], pb[6];
    if (!sbox(O, o, k, b)) return 0;
    player_box6(O->P, pb);
    int sh = I(O->P->o.x) - (b[0] + b[3]) / 2;
    if (sh > MX / 2) { b[0] += MX; b[3] += MX; }
    else if (sh < -MX / 2) { b[0] -= MX; b[3] -= MX; }
    return obj_boxes_overlap(b, pb);
}

/* Simple point box around a model object (FUN_100200e0 uses the model's collision box). */
static int model_touch_player(Objects *O, const Object *o, int r)
{
    int pb[6];
    player_box6(O->P, pb);
    int x = I(O->P->o.x) + ring_d(O->P->o.x, o->x);
    int b[6] = {x - r, I(o->y) - r, I(o->z) - r, x + r, I(o->y) + r, I(o->z) + r};
    return obj_boxes_overlap(b, pb);
}

static Object *mspawn(Objects *O, uint16_t type, int32_t x, int32_t y, int32_t z, int role)
{
    Object *o = bspawn(O, type, x & (FX(MX) - 1), y, z, role);
    return o;
}

/* 0x202a: smoke / debris puffs (FUN_10022b90 / FUN_10022c20). kind 1 = smoke, 2 = bouncing rubble. */
static Object *medusa_smoke(Objects *O, int32_t x, int32_t y, int32_t z, int kind, int vx, int vy, int vz)
{
    Object *o = mspawn(O, 0x202a, x, y, z, R_MEDUSA_GAZE);
    if (!o) return NULL;
    o->variant = 1;                                             /* sub-role: smoke */
    o->w[0] = vx; o->w[1] = vy; o->w[2] = vz; o->w[3] = kind;
    o->frame = (int16_t)(obj_rnd(O, 8) + 1);
    if (obj_rnd(O, 2) == 0) o->flags54 |= 0x10;
    o->scale = 0x100;
    return o;
}

static Object *medusa_dust(Objects *O, int32_t x, int32_t y, int32_t z)   /* FUN_10022d20: 0x202b sparkle */
{
    Object *o = mspawn(O, 0x202b, x, y, z, R_MEDUSA_GAZE);
    if (!o) return NULL;
    o->variant = 2;
    o->frame = 9; o->scale = 0x200; o->semi = 1;
    o->timer = (int16_t)(obj_rnd(O, 5) + 1);
    return o;
}

/* FUN_10021590: a burst of rubble near Medusa. */
static void medusa_rubble(Objects *O, Object *m, int kind)
{
    int dx = obj_rnd(O, 0x640) - 0x320;
    int vx = (obj_rnd(O, 10) - 5) * kind, vy = -((obj_rnd(O, 8) + 3) * kind), vz = -((obj_rnd(O, 8) + 1) * kind);
    medusa_smoke(O, m->x + FX(dx), m->y - 0x800000, m->z - 0x100000, kind, vx, vy, vz);
}

static void medusa_idle(Object *m) { M.gaze_pending = 0; m->v2a = 0; M.t = 0x1e; }   /* FUN_100218f0 */

static void medusa_hide_beam(int b)                            /* FUN_10022400 */
{
    for (int i = 0; i < 3; i++) if (M.kid[b * 3 + i]) M.kid[b * 3 + i]->x = M.kid[b * 3 + i]->y = M.kid[b * 3 + i]->z = 0;
}

static void medusa_after_gaze(Objects *O, Object *m)            /* FUN_100220c0 (callback 5) */
{
    if (getenv("MED_LOG")) printf("MED t=%ld after gaze hurt=%d lives=%d\n", B.tick, M.hurt, M.lives);   /* LV_MEDUSA log */
    if (M.lives < 1) {
        Object *d = mspawn(O, 0x31, 0, 0x2f00000, 0x2780000, R_MEDUSA);
        if (d) {                                                /* FUN_100213c0 */
            d->variant = 1;
            d->anim = 0; obj_set_anim(O, d, 7);
            d->scale = 0x200; d->v2a = 0;
            M.t = 0x14;
            obj_sound(O, 0x1c1, d->x);
        }
        medusa_idle(m);
        return;
    }
    if (!M.hurt) { obj_set_anim(O, m, 0); medusa_idle(m); return; }
    M.lives--;
    M.hurt = 0;
    bar_hit(O, M.lives <= 0 ? B.bar : 0x20);                   /* DAT_10072184 = 0x20 */
    boss_bar(O, M.lives, 8);
    medusa_idle(m);
}

/* FUN_10022440: one gaze beam. b = 0 (kids 0..2) or 1 (kids 3..5). */
static void medusa_beam(Objects *O, Object *m, int b)
{
    int *st = M.beam[b];
    Object *head = M.kid[b * 3 + 1];
    const Obj *p = &O->P->o;
    if (!head) { st[0] = -1; return; }
    if (st[0] == 0) {
        head->z -= 0x200000;
        for (int c = 0; c < 8 && st[0] == 0; c++) {
            Object *sh = live(O, M.col[c].shield, R_MEDUSA_PIECE);
            if (sh && head->z < p->z + 0xa40000 && iabs(ring_d(sh->x, head->x)) <= 0x20 && M.col[c].state == 6) {
                st[0] = 2; st[1] = c; M.beam_shield[b] = sh;
                M.t = 0x1e;
                obj_sound(O, 0x51ce, sh->x);
                if (M.first_reflect) sfx_speech(O->fs, 0x26);   /* AUDSHELL: first time: Hercules' line (FUN_1002c4c0(0x26)) */
                M.first_reflect = 0;
                break;
            }
            Object *top = live(O, M.col[c].p[3], R_MEDUSA_PIECE);
            if (!M.col[c].broken && top && head->z < p->z + 0x840000 && iabs(ring_d(top->x, head->x)) <= 0x2c && M.col[c].state == 0) {
                st[0] = 1; st[1] = c;
                obj_sound(O, 0x51c9, top->x);
                break;
            }
        }
        if (st[0] == 0)
            for (int i = 0; i < OBJ_MAX; i++) {
                Object *s = &O->obj[i];
                if (s->used && s->kind == K_BOSS && s->ai == R_MEDUSA_SKEL && head->z < p->z + 0x80000 &&
                    iabs(ring_d(s->x, head->x)) <= 0xf) s->hp = 0;     /* turned to stone */
            }
        if (st[0] == 0 && p->y > 0x2cfffff && head->z < p->z + 0x80000 && iabs(ring_d(p->x, head->x)) <= 0x10) {
            obj_sound_stop(O, 0x1c7);                           /* AUDSHELL: FUN_10022440 */
            player_damage(O->P, 3, head->x, head->y, head->z, 0);
            if (obj_rnd(O, 4) == 0) obj_sound(O, 0x1c5, p->x);
            st[0] = -1;
        }
        if (head->z < p->z - 0x200000) { st[0] = -1; obj_sound_stop(O, 0x1c7); }
    } else if (st[0] == 1) {                                    /* the gaze climbs the column and breaks it */
        head->y -= 0x40000;
        if (!(M.frame & 1)) medusa_smoke(O, head->x + FX(obj_rnd(O, 0x20) - 0x10), head->y + FX(obj_rnd(O, 0x20) - 0x10), head->z, 1, 0, 0, 0);
        if (head->y < 0x2400000) {
            st[0] = -1;
            obj_sound_stop(O, 0x1c7);
            int c = st[1];
            if (!M.col[c].broken) {
                M.col[c].broken = 1;
                Object *old = live(O, M.col[c].p[3], R_MEDUSA_PIECE);
                int32_t x = old ? old->x : head->x;
                if (old) bkill(O, old);
                static const int FR[4] = {4, 3, 2, 0xb};
                static const int32_t Y[4] = {0x2000000, 0x2400000, 0x2800000, 0x2c00000};
                for (int k = 0; k < 4; k++) {                    /* FUN_10022fe0: the four pieces */
                    Object *q = mspawn(O, 0x30, x, Y[k], 0xb20000, R_MEDUSA_PIECE);
                    if (!q) continue;
                    q->flags54 = 0x90000040; q->frame = (int16_t)FR[k]; q->variant = c;
                    M.col[c].p[k] = q;
                }
                if (!(c & 1) && live(O, M.col[c].p[0], R_MEDUSA_PIECE)) boss_drop(O, M.col[c].p[0]->x, M.col[c].p[0]->y, p->z, 0x289);
                obj_sound(O, 0xd1c0, x);
            }
        }
    } else if (st[0] == 2) {                                    /* reflected back at Medusa */
        Object *sh = live(O, M.beam_shield[b], R_MEDUSA_PIECE);
        int32_t tx = sh ? sh->x + (b ? 0x80000 : -0x80000) : head->x, ty = sh ? sh->y : head->y;
        head->x += (I(head->x) < I(tx)) ? FX(2) : (I(head->x) > I(tx)) ? -FX(2) : 0;
        head->y += (I(head->y) < I(ty)) ? FX(2) : (I(head->y) > I(ty)) ? -FX(2) : 0;
        int32_t tz = p->z + 0xa40000;
        head->z += (I(head->z) < I(tz)) ? FX(2) : (I(head->z) > I(tz)) ? -FX(2) : 0;
        Object *a = M.kid[b * 3 + 2];
        if (a) { a->x = p->x + (b ? 0xcc0000 : -0x1f0000); a->y = 0x29f0000; a->z = 0x2200000; }
        if (!(M.frame & 1) && a) medusa_dust(O, a->x, 0x29f0000, 0x2200000);
        if (M.t < 1) {
            st[0] = -1;
            obj_set_anim(O, m, 1);                              /* hit by her own gaze */
            obj_sound(O, obj_rnd(O, 2) ? 0x1c4 : 0x1c3, m->x);
            M.hurt = 1;
        }
    }
    if (!(M.frame & 1)) medusa_dust(O, head->x, head->y, head->z + 0x60000);
}

static void medusa_gaze(Objects *O, Object *m)                  /* FUN_10022280 */
{
    const Obj *p = &O->P->o;
    for (int b = 0; b < 2; b++) {
        if (M.beam[b][0] < 0) continue;
        int32_t ax = p->x + (b ? 0x8c0000 : 0x210000);
        for (int k = 0; k < 3; k += 2)
            if (M.kid[b * 3 + k]) { M.kid[b * 3 + k]->x = ax; M.kid[b * 3 + k]->y = 0x26f0000; M.kid[b * 3 + k]->z = 0x2200000; M.kid[b * 3 + k]->frame = 6; }
        medusa_beam(O, m, b);
        if (M.beam[b][0] < 0) {
            int c = b == 0 ? M.beam[0][1] : (M.beam[0][1] < 0 ? M.beam[1][1] : -1);
            if (c >= 0) M.col[c].state++;                       /* the reflecting shield is spent */
        }
    }
    if (M.beam[0][0] != M.beam[1][0]) {
        if (M.beam[0][0] < 1) { if (M.beam[1][0] > 0) M.beam[0][0] = -1; }
        else M.beam[1][0] = -1;
    }
    if (M.beam[0][0] < 0) medusa_hide_beam(0);
    if (M.beam[1][0] < 0) medusa_hide_beam(1);
    if (M.beam[0][0] < 0 && M.beam[1][0] < 0) medusa_after_gaze(O, m);
}

static void medusa_rocks(Objects *O, Object *m)                 /* FUN_10021aa0: rock rain while she roars */
{
    const Obj *p = &O->P->o;
    if ((M.frame & 3) == 0) {
        Object *r;
        if (obj_rnd(O, 8) == 0)
            r = mspawn(O, 0x34, FX(obj_rnd(O, 0x1000)), m->y - 0x1680000, m->z - FX(obj_rnd(O, 0x15c)) - 0xd80000, R_MEDUSA_ROCK);
        else {
            int dx = obj_rnd(O, 0x40) - 0x20;
            if (O->P->o.vx) { dx = obj_rnd(O, 0x20) + 0x78; if (O->P->o.facing) dx = -dx; }
            r = mspawn(O, 0x34, p->x + FX(dx), p->y - 0x1180000, p->z, R_MEDUSA_ROCK);
        }
        if (r) {                                                /* FUN_10021c00 */
            static const int FR[4] = {2, 3, 1, 0xc};
            int k = obj_rnd(O, 4);
            r->flags54 = 0x90000000; r->frame = (int16_t)FR[k];
            r->w[0] = obj_rnd(O, 10) - 5; r->w[1] = 0; r->w[2] = obj_rnd(O, 10) - 5;
            for (int i = 0; i < 3; i++) { r->rot[i] = (int16_t)obj_rnd(O, 0x1000); r->w[3 + i] = obj_rnd(O, 0x40) - 0x20; }
            Object *s = mspawn(O, 0x202f, r->x, 0x2e00000, r->z, R_MEDUSA_SHADOW);   /* FUN_10021ed0 */
            if (s) { s->frame = 0x19; s->bright = 0; s->w[0] = k == 3 ? 0xc : 0x1c; s->w[1] = (int32_t)(r - O->obj); r->w[6] = (int32_t)(s - O->obj); }
        }
    }
    medusa_smoke(O, FX(obj_rnd(O, 0x1000)), m->y - 0x1680000, m->z - FX(obj_rnd(O, 0x15c)) - 0xd80000, 1, 0, 0, 0);
    M.quake = 0x40000;
    if (M.t < 0) { M.quake = 0; medusa_idle(m); }
}

static void medusa_tick(Objects *O, Object *m)                  /* FUN_10021320 */
{
    if (m->variant != 1) { M.t--; M.frame--; }                  /* LV_MEDUSA: FUN_10021400 (dying Medusa 0x31) does not count the timers; only FUN_10021320 does (they ran twice per tick) */
    if (m->variant == 1) {                                      /* 0x31: the dying Medusa, FUN_10021400 */
        obj_run_script(O, m);
        m->x = O->P->o.x;
        switch (m->v2a) {
        case 0: if (M.t < 1) { M.t = 0x14; m->v2a = 1; } break;
        case 1:
            if ((M.frame & 3) == 0) medusa_rubble(O, m, 1);
            if (M.t < 1) { M.t = 0x14; m->v2a = 2; sfx_speech_stop(); sfx_speech(O->fs, 0x27); }   /* AUDSHELL: her last words (FUN_1002c4c0(0x27)) */
            break;
        case 2:
            if ((M.frame & 1) == 0) medusa_rubble(O, m, 1);
            if (M.t < 1) {
                M.t = 0x5a; m->v2a = 3;
                static const int P[4][3] = {{0, -0x48, 0}, {-0x800, -0x80, 5}, {0x800, -0x38, 8}, {-0x400, -0x40, 10}};
                for (int k = 0; k < 4; k++) {                   /* FUN_10021650: 0x35 pieces of her head */
                    Object *q = mspawn(O, 0x35, m->x + P[k][0] * 0x10000, m->y + P[k][1] * 0x10000, m->z, R_MEDUSA_GAZE);
                    if (q) { q->variant = 3; q->scale = 0x300; q->frame = 0; q->timer = (int16_t)P[k][2]; }
                }
                for (int k = 0; k < 100; k++) medusa_rubble(O, m, 2);
                m->w[11] = 1;                                   /* hidden */
                if (M.medusa) M.medusa->w[11] = 1;
                obj_sound_stop(O, 0x1c6);       /* AUDSHELL: FUN_10021400 ends the fight's loop */
                obj_sound(O, 0x1ea, m->x);
            }
            break;
        case 3: if (M.t < 1) { boss_defeated(O); m->v2a = 4; } break;
        }
        return;
    }
    obj_run_script(O, m);
    if (!m->used) return;
    m->x = O->P->o.x;
    if (m->v2a == 1) medusa_gaze(O, m);
    else if (m->v2a == 2) medusa_rocks(O, m);
    int sh = M.quake;                                           /* LV_MEDUSA: FUN_10021320: DAT_1006fde0 (set to 3 by rock/shield impacts) is a */
    if (M.shake > 0) { M.shake--; sh = 0xa0000; }               /* tick counter giving a 10 px shake; it was overwritten by the quake value each tick */
    if (sh && (M.frame & 1) == 0) sh = -sh;                     /* DAT_10070cbc camera shake */
    O->cam_dy = sh;                                             /* LEFTOVERS: wired to game.c's camera */
}

void boss_callback(Objects *O, Object *o, int id)
{
    if (B.level != 7 || o->ai != R_MEDUSA) return;
    const PeImage *pe = O->pe;
    switch (id) {
    case 1:                                                     /* FUN_10021810: time for a gaze? */
        if (M.gaze_delay == 0) {
            if (!M.gaze_pending && obj_rnd(O, 4) < 3 && !O->P->dying) {
                M.gaze_pending = 1;
                obj_set_anim(O, o, 3);
                if (obj_rnd(O, 4) == 0) obj_sound(O, 0x1c2, o->x);
            }
        } else M.gaze_delay--;
        break;
    case 2: {                                                   /* FUN_10021880: next attack from the pattern */
        int32_t v = s32(pe, M.pat);
        while (v < 0) { M.pat = M.pat_base; v = s32(pe, M.pat); }
        M.pat += 4;
        if (getenv("MED_LOG")) printf("MED t=%ld pattern %d\n", B.tick, v);   /* LV_MEDUSA log */
        obj_set_anim(O, o, v == 0 ? 2 : v == 1 ? 6 : 5);
        break;
    }
    case 3: {                                                   /* FUN_10021a40: roar, rocks fall */
        Object *e = mspawn(O, 0x2033, 0x4000000, 0x2e00000, 0x2900000, R_MEDUSA_GAZE);
        if (e) { e->variant = 4; e->scale = 0x300; e->rot[0] = 0x80; e->w[0] = 0; e->timer = 0; obj_sound(O, 0x1d3, e->x); }
        o->v2a = 2; M.t = 0xb4;
        obj_sound(O, 0x1cd, o->x);
        break;
    }
    case 4: {                                                   /* FUN_10021f60: the gaze starts */
        if (getenv("MED_LOG")) printf("MED t=%ld gaze start\n", B.tick);   /* LV_MEDUSA log */
        obj_sound(O, 0x1c7, o->x);
        o->v2a = 1;
        obj_sound(O, 0x1c8, o->x);
        const Obj *p = &O->P->o;
        if (M.kid[1]) { M.kid[1]->x = p->x - 0x100000; M.kid[1]->y = o->y - 0x80000; M.kid[1]->z = o->z; M.kid[1]->frame = 5; }
        if (M.kid[4]) { M.kid[4]->x = p->x + 0x100000; M.kid[4]->y = o->y - 0x80000; M.kid[4]->z = o->z; M.kid[4]->frame = 5; }
        M.beam[0][0] = M.beam[1][0] = 0;
        M.beam[0][1] = M.beam[1][1] = -1;
        medusa_gaze(O, o);
        break;
    }
    case 5: medusa_after_gaze(O, o); break;
    case 6:                                                     /* FUN_10023340: two snakes */
        for (int k = 0; k < 2; k++) {
            int32_t dx = k ? 0x3ff0000 : -0x4000000;
            Object *s = mspawn(O, 0x33, o->x + dx, o->y - 0x600000, o->z - 0x640000, R_MEDUSA_GAZE);
            if (!s) continue;
            s->variant = 5; s->frame = 0x82; s->v2a = 0;
            s->w[0] = 0; s->w[1] = dx < 0 ? 1 : -1; s->w[2] = 0x800; s->w[3] = s->y;
            if (dx >= 0) s->flags54 |= 0x10;
            obj_sound(O, 0x59e5, s->x);
        }
        obj_sound(O, 0x1e6, o->x);
        medusa_idle(o);
        break;
    }
}

/* Skeleton warrior scripts (int pairs {frame, ticks}, -1 sound, -2 overlay frame, 0 end). */
enum {
    SK_RISE = 0x10058d98, SK_TURN = 0x10058e68, SK_WALK = 0x10058f18, SK_WALK0 = 0x10058ef8, SK_STOP = 0x10058f80,
    SK_ATK1 = 0x10058f98, SK_ATK2 = 0x10059008, SK_ATK3 = 0x10059078, SK_BACK = 0x10059140, SK_TAUNT = 0x10058eb8,
    SK_HURT = 0x100590d0, SK_AFTER = 0x10058d88,
};

static void skel_set(Object *s, uint32_t sc, int next) { s->script = sc; s->w[0] = (int32_t)sc; s->timer = 0; s->w[1] = next; s->v2a = 1; }

static void medusa_skel_tick(Objects *O, Object *s)             /* FUN_10023b30 */
{
    const PeImage *pe = O->pe;
    const Obj *p = &O->P->o;
    int d = ring_d(s->x, p->x);
    int face = s->facing;
    if (d > 0x400 || d < -0x400) face ^= 1;
    s->flags54 = (s->flags54 & ~0x10u) | (face ? 0x10 : 0);
    int32_t move = 0;
    int near = s->w[2], done = 0, vis = 0;
    for (int guard = 0; guard < 32 && !done; guard++) {
        switch (s->v2a) {
        case 0:
            if ((s->facing != 0) != (d >= 0)) {
                if ((uint32_t)s->w[0] == SK_WALK) skel_set(s, SK_STOP, 0);
                else { skel_set(s, SK_TURN, 2); obj_sound(O, 0x51d5, s->x); }
            } else if (iabs(d) > near) {
                skel_set(s, ((uint32_t)s->w[0] == SK_WALK || (uint32_t)s->w[0] == SK_WALK0) ? SK_WALK : SK_WALK0, 0);
            } else {
                int r = O->P->dying ? 10 : obj_rnd(O, 0xb);
                if (r < 3) { obj_sound(O, 0x51d9, s->x); skel_set(s, SK_ATK1, 0); }
                else if (r < 6) skel_set(s, SK_ATK2, 0);
                else if (r < 9) skel_set(s, SK_ATK3, 0);
                else if (r == 9) skel_set(s, SK_BACK, 3);
                else skel_set(s, SK_TAUNT, 0);
            }
            break;
        case 1:
            done = 1;
            if (--s->timer < 1) {
                int32_t v = s32(pe, s->script); s->script += 4;
                s->frame = (int16_t)v;
                if (v == -2) { vis = 1; s->child = (int16_t)s32(pe, s->script); s->script += 4; done = 0; }
                else if (v == -1) { obj_sound(O, (uint16_t)s32(pe, s->script), s->x); s->script += 4; done = 0; }
                else if (v == 0) { s->v2a = (uint16_t)s->w[1]; done = 0; }
                else { s->timer = (int16_t)s32(pe, s->script); s->script += 4; s->w[3] = vis; }
            }
            if (((uint32_t)s->w[0] == SK_WALK || (uint32_t)s->w[0] == SK_WALK0) && iabs(d) <= near && obj_rnd(O, 4)) {
                s->v2a = 0; done = 0;
            }
            break;
        case 2:
            s->facing ^= 1; s->flags54 ^= 0x10;
            skel_set(s, SK_AFTER, 0);
            s->w[2] = O->P->o.vx ? 100 : 0x5a;
            break;
        case 3:
            move = -0x240000; skel_set(s, SK_AFTER, 0);
            break;
        default: done = 1; break;
        }
    }
    if ((uint32_t)s->w[0] == SK_WALK) move = 0x40000;
    if (!s->facing) move = -move;
    s->x = (s->x + move) & (FX(MX) - 1);
    if (ring_hits_player(O, s, 2)) {
        player_damage(O->P, 1, s->x, s->y, s->z, 0);
        if (O->P->hit_req) obj_sound(O, 0xd1dd, s->x);
    } else if (s->w[2] > 0x46) s->w[2]--;
    if (s->inv == 0) {
        int k = took_hit(O, s, 1);
        if (k == 1) {
            int dmg = player_hit_damage(O->P, (uint16_t)(0x7f & ~O->P->consumed));
            if (dmg) { s->hp = (int16_t)(s->hp - dmg); skel_set(s, SK_HURT, 0); obj_sound(O, 0xd1de, s->x); }
            hit_sound(O, 0x27a);
            s->inv = 5;
        } else if (k == 2) { s->hp = (int16_t)(s->hp - 0x80); obj_sound(O, 0xd1de, s->x); s->inv = 5; }
        if (s->inv == 5 && s->hp < 1 && obj_rnd(O, 8) == 0) {  /* LV_MEDUSA: FUN_10023b30: the killing blow sometimes draws a Hercules line (speech 0x27..0x29) */
            static int last_line;
            int n = obj_rnd(O, 3);
            if (n == last_line && --n < 0) n = 2;
            last_line = n;
            sfx_speech(O->fs, 0x27 + n);
        }
    }
    if (s->hp < 1 || M.lives <= 0) {
        obj_sound(O, 0xd1df, s->x);
        if (!B.dead) { int id = drop_list_next(O); if (id) boss_drop(O, s->x, s->y, O->P->o.z, (uint32_t)id | 0x280); }
        static const int PCS[12][3] = {{-0xf,-0xf,0xc},{0xf,-0xf,0xc},{-0xf,-0xf,9},{0xf,-0xf,9},{0,-0x23,3},{0,-0x2d,5},
                                      {-0x14,-0x32,10},{0x14,-0x32,10},{-0x1e,-0x32,7},{0x1e,-0x32,7},{0x32,-0x3c,4},{0,-0x41,2}};
        for (int k = 0; k < 12; k++) {                          /* FUN_10024120 / FUN_100241f0: bones */
            int dx = PCS[k][0];
            if (k == 10 && s->facing) dx = -dx;
            Object *b = mspawn(O, 0x2030, s->x - dx * 0x10000, s->y + PCS[k][1] * 0x10000, s->z, R_MEDUSA_GAZE);
            if (!b) continue;
            b->variant = 6; b->frame = (int16_t)(PCS[k][2] + 0x56);
            if (PCS[k][2] == 5 ? dx > 0 : dx < 0) b->flags54 |= 0x10;
            b->w[0] = obj_rnd(O, 0x14) - 10; b->w[1] = -5 - obj_rnd(O, 8); b->w[2] = obj_rnd(O, 0x32) + 5;   /* FUN_10023680 */
        }
        bkill(O, s);
        M.skels--;
    }
}

static void medusa_skel_spawn(Objects *O)                       /* FUN_10021210 */
{
    if (--M.skel_timer >= 1 || M.lives <= 0) return;
    M.skel_timer = obj_rnd(O, 0x78) + 0x3c;
    if (M.skels >= 3) return;
    const Obj *p = &O->P->o;
    int dx;
    if (!p->vx) { dx = (obj_rnd(O, 2) ? 0x60 : -0x60) + obj_rnd(O, 0x60) - 0x30; }
    else { dx = obj_rnd(O, 0x40) + 0x1f0; if (p->facing) dx = -dx; }
    Object *s = mspawn(O, 0x32, p->x + FX(dx), 0x2e00000, p->z, R_MEDUSA_SKEL);
    if (!s) return;                                             /* FUN_10023a50 */
    s->facing = ring_d(s->x, p->x) > 0;
    s->z = p->z;
    skel_set(s, SK_RISE, 0);
    s->inv = 0x32; s->hp = 4;
    M.skels++;
    s->child = 0x9f; s->child_semi = 1; s->w[3] = 0;
    s->w[2] = p->vx ? 100 : 0x5a;
}

static void medusa_misc_tick(Objects *O, Object *o)            /* 0x202a / 0x202b / 0x202c / 0x2033 / 0x33 / 0x35 / 0x2030 */
{
    switch (o->variant) {
    case 1:                                                     /* FUN_10022c20 */
        o->scale = (uint16_t)(o->scale - 4);
        if (o->scale <= 5 || o->scale > 0x8000) { bkill(O, o); return; }
        o->x += FX(o->w[0]); o->y += FX(o->w[1]); o->w[1]++; o->z += FX(o->w[2]);
        if (o->w[3] == 2) {
            { static unsigned cnt; if ((cnt++ & 0xf) == 0) { int k = obj_rnd(O, 3); if (k < 3) obj_sound(O, (uint16_t)(0xd1e7 + k), o->x); } }   /* LV_MEDUSA: FUN_10022c20 rubble clatter, every 16th puff (DAT_1006fe50) */
            if (I(o->y) < 0x2d8) return;
            o->w[0] >>= 1; o->w[2] >>= 1; o->w[1] = -(o->w[1] >> 1); o->y = FX(o->w[1] + 0x2d8);
            if (++o->w[4] >= 4) bkill(O, o);
            return;
        }
        if (I(o->y) >= 0x2d8) bkill(O, o);
        break;
    case 2:                                                     /* FUN_10022d60 */
        o->y -= o->timer * 0x8000;
        if ((M.frame & 1) == 1 && ++o->frame > 0x18) bkill(O, o);
        break;
    case 3:                                                     /* FUN_10021940: head pieces */
        if (--o->timer < 1) {
            o->timer = 2;
            o->frame = o->frame == 0 ? 0x86 : o->frame + 1;
            if (o->frame > 0x93) bkill(O, o);
        }
        break;
    case 4: {                                                   /* FUN_100219e0: the roar ring */
        if (--o->timer < 1) {
            int idx = o->w[0];
            int t = pe_s16(O->pe, 0x10058d50 + 4u * (uint32_t)idx);
            o->w[0] = idx + 1;
            o->timer = (int16_t)t;
            if (t < 0) { bkill(O, o); return; }
            o->frame = (int16_t)(pe_s16(O->pe, 0x10058d50 + 4u * (uint32_t)(idx + 1)) + 0x76);
            o->w[0] = idx + 2;
        }
        break;
    }
    case 5: {                                                   /* FUN_10023460: a snake */
        if ((M.frame & 3) == 0 && ++o->frame > 0x85) o->frame = 0x82;
        if (getenv("MED_LOG") && (B.tick % 10) == 0) printf("MED t=%ld snake x %d y %d z %d st %d w0 %d life %d\n", B.tick, I(o->x), I(o->y), I(o->z), o->v2a, o->w[0], o->w[2]);   /* LV_MEDUSA log */
        const Obj *p = &O->P->o;
        if (!ring_hits_player(O, o, 1)) {
            Object *t = mspawn(O, 0x202c, o->x, o->y, o->z, R_MEDUSA_GAZE);
            if (t) { t->variant = 7; t->frame = 0x7a; }
            if (o->v2a == 0) {
                int a = ((0x21c - I(o->z)) * 0x800) / 0x1d0;
                /* DAT_10070f34 is a 4096-step sine (4096 = 1); the original's ((sin*15) & 0x1fffe00) * -0x80 wraps to
                 * (-sin*1920) & 0xffff0000: an arc of 120 px */
                int sn = (int)((long)sin_t(O, ((a - 0x400) & 0xfff) >> 2) * 4096 / 32767);
                o->y = o->w[3] + (int32_t)((uint32_t)(-sn * 1920) & 0xffff0000u);
                o->z -= 0x100000;
                if (o->z <= p->z) {
                    o->z = p->z; o->v2a = 1; o->flags54 ^= 0x10;
                    int h = obj_rnd(O, 3);
                    if (h == M.snake_h_last && --h < 0) h = 2;
                    M.snake_h_last = h;
                    o->w[3] = h == 0 ? 0x2a80000 : h == 1 ? 0x2c80000 : 0x2d80000;
                }
            } else {
                o->y += (I(o->y) < I(o->w[3])) ? FX(2) : (I(o->y) > I(o->w[3])) ? -FX(2) : 0;
                o->x = (o->x + o->w[0] * 0x4000) & (FX(MX) - 1);
                o->w[2] -= iabs(o->w[0] >> 2);
                o->w[0] += o->w[1] * 2;
            }
        } else { player_damage(O->P, 2, o->x, o->y, o->z, 0); o->w[2] = 0; }
        if (o->w[2] < 1) bkill(O, o);
        break;
    }
    case 6:                                                     /* FUN_100236e0: bones */
        o->rot[2] = (int16_t)((o->rot[2] + o->w[2]) & 0xfff);
        o->x += FX(o->w[0]); o->y += FX(o->w[1]); o->w[1]++;
        if (I(o->y) < 0x2d8) break;
        if (obj_rnd(O, 10) < 5) obj_sound(O, (uint16_t)(0xd9e0 + obj_rnd(O, 5)), o->x);
        o->w[0] /= 2; o->w[1] = -(o->w[1] / 2); o->w[2] >>= 1;
        o->y = FX(o->w[1] + 0x2d8);
        if (iabs(o->w[1]) < 3) bkill(O, o);
        break;
    case 7:                                                     /* FUN_10023660: snake trail */
        if (++o->frame > 0x81) bkill(O, o);
        break;
    }
}

static void medusa_piece_tick(Objects *O, Object *o)           /* 0x30 FUN_10023120 / 0x2d FUN_10022dd0 */
{
    int c = o->variant;
    int *st = &M.col[c].state;
    if (o->w[11] == 2) {                                        /* the shield */
        switch (*st) {
        case 1: o->w[1] = 0; break;
        case 5:
            o->y += FX(o->w[1]);
            if (o->w[1] < 0x20) o->w[1]++;
            if (o->y > 0x2cfffff) {
                o->w[1] = -(o->w[1] / 4); o->y = FX(o->w[1] + 0x2d0);
                if (!M.col[c].landed) { M.col[c].landed = 1; obj_sound(O, 0x59cb, o->x); }
                if (o->w[1] > -3) (*st)++;
            }
            break;
        case 7:
            o->w[0] = obj_rnd(O, 4) - 2; o->w[1] = -8 - obj_rnd(O, 8); o->w[2] = -10 - obj_rnd(O, 8);
            (*st)++;
            obj_sound(O, 0x59ca, o->x);
            break;
        case 8:
            o->x += FX(o->w[0]); o->y += FX(o->w[1]); o->z += FX(o->w[2]);
            if (o->w[1] < 0x20) o->w[1]++;
            o->rot[1] = (int16_t)((o->rot[1] + 0x30) & 0xfff);
            if (o->y > 0x2cfffff) {
                o->w[0] /= 2; o->w[2] /= 2; o->w[1] = -(o->w[1] / 2); o->y = FX(o->w[1] + 0x2d0);
                if (o->w[1] > -3) { M.col[c].shield = NULL; bkill(O, o); }
            }
            break;
        }
        return;
    }
    switch (*st) {
    case 1: case 2: case 3: case 4: {
        int v = obj_rnd(O, 5) + 7;
        o->w[0] = ring_d(O->P->o.x, o->x) < 0 ? -v : v;
        o->w[1] = -5 - obj_rnd(O, 5); o->w[2] = -8 - obj_rnd(O, 8); o->w[3] = 0;
        (*st)++;
        break;
    }
    case 5: case 6:
        if ((M.frame & 7) == 0) medusa_dust(O, o->x, o->y, o->z);
        o->x += FX(o->w[0]); o->y += FX(o->w[1]); o->z += FX(o->w[2]);
        if (o->w[1] < 0x20) o->w[1]++;
        o->rot[0] = (int16_t)((o->w[1] + o->rot[0] + 4) & 0xfff);
        if (o->y > 0x2cfffff) {
            for (int k = 0; k < 4 - o->w[3]; k++) medusa_smoke(O, o->x, o->y, o->z, 1, obj_rnd(O, 6) - 3, -obj_rnd(O, 6), obj_rnd(O, 4) - 2);
            if (o->w[3] == 0) M.shake = 3;
            o->w[3]++;
            o->w[0] /= 2; o->w[2] /= 2; o->w[1] = -(o->w[1] / 2); o->y = FX(o->w[1] + 0x2d0);
            if (o->w[1] > -3) {
                for (int k = 0; k < 4; k++) if (M.col[c].p[k] == o) M.col[c].p[k] = NULL;
                bkill(O, o);
            }
        }
        break;
    }
}

static void medusa_rock_tick(Objects *O, Object *r)             /* FUN_10021d10 */
{
    r->x += FX(r->w[0]); r->y += FX(r->w[1]); r->w[1]++; r->z += FX(r->w[2]);
    for (int i = 0; i < 3; i++) r->rot[i] = (int16_t)((r->rot[i] + r->w[3 + i]) & 0xfff);
    int hit = model_touch_player(O, r, 0x18);
    if (r->y < 0x2d80000 && !hit) return;
    if (hit) player_damage(O->P, 0, r->x, r->y, r->z, 0);
    int n = 0;
    do medusa_smoke(O, r->x, r->y, r->z, 1, obj_rnd(O, 6) - 3, -obj_rnd(O, 6), obj_rnd(O, 4) - 2); while (++n <= obj_rnd(O, 4));
    medusa_dust(O, r->x, r->y + 0x100000, r->z);
    if (obj_rnd(O, 2) == 0) {
        int k = obj_rnd(O, 10);
        obj_sound(O, k < 3 ? 0xd1cf : k < 6 ? 0xd1d0 : k < 9 ? 0xd1d2 : 0xd1d1, r->x);
    }
    if (r->w[6] >= 0 && r->w[6] < OBJ_MAX && O->obj[r->w[6]].used && O->obj[r->w[6]].ai == R_MEDUSA_SHADOW) bkill(O, &O->obj[r->w[6]]);
    bkill(O, r);
    M.shake = 3;
}

static void medusa_shadow_tick(Objects *O, Object *s)           /* FUN_10021f00 */
{
    Object *r = (s->w[1] >= 0 && s->w[1] < OBJ_MAX) ? &O->obj[s->w[1]] : NULL;
    if (!r || !r->used || r->ai != R_MEDUSA_ROCK) { bkill(O, s); return; }
    s->x = r->x; s->y = 0x2e00000; s->z = r->z;
    int v = (s->w[0] << 8) / 0x14 + ((0x2e00000 - r->y) >> 0x13);
    if (v > 0x3ff) v = 0x3ff;
    if (v < 1) v = 1;
    s->scale = (uint16_t)v;
}

static void medusa_start(Objects *O)                            /* FUN_10021050 */
{
    memset(&M, 0, sizeof M);
    Player *P = O->P;
    P->o.x = P->o.px = 0x3b80000; P->o.z = P->o.pz = 0x4c0000;
    Object *m = mspawn(O, 0x2e, 0, 0x2f00000, 0x2800000, R_MEDUSA);
    if (!m) return;
    M.medusa = m; B.boss = m;
    m->scale = 0x200; m->frame = 0x3d;
    /* the gaze rig (0x2f) and its six model parts (0x2032) */
    for (int k = 0; k < 6; k++) {
        Object *q = mspawn(O, 0x2032, 0, 0, 0, R_MEDUSA_GAZE);
        if (!q) break;
        q->variant = 0; q->flags54 = 0x10000045; q->bright = 0xff; q->frame = 6;
        if (k == 1 || k == 4) q->scale = 0x200;
        M.kid[k] = q;
    }
    medusa_hide_beam(0); medusa_hide_beam(1);
    /* FUN_10023040: eight columns, each with a mirror shield, and the brazier flames round the ring */
    for (int c = 0; c < 8; c++) {
        int32_t x = 0xa00000 + 0x2000000 * c;
        Object *col = mspawn(O, 0x30, x, 0x2c00000, 0xb20000, R_MEDUSA_PIECE);
        if (col) { col->flags54 = 0x90000040; col->frame = 0x10; col->scale = 0x200; col->variant = c; }
        M.col[c].p[3] = col;
        Object *sh = mspawn(O, 0x2d, x, 0x2200000, 0xe20000, R_MEDUSA_PIECE);
        if (sh) { sh->flags54 = 0x90000000; sh->rot[1] = 0x200; sh->frame = 0xf; sh->variant = c; sh->w[11] = 2; }
        M.col[c].shield = sh;
    }
    for (int32_t x = 0x200000; x < 0x10200000; x += 0x1000000) {
        Object *f = mspawn(O, 0x2029, x, 0x2a00000, 0x80000, R_MEDUSA_GAZE);
        if (f) { f->variant = 8; f->frame = 0x5d; f->w[0] = obj_rnd(O, 2); }
    }
    Object *pool = mspawn(O, 0x202d, 0x4000000, 0x2e00000, 0x2900000, R_MEDUSA_GAZE);   /* FUN_100216b0 */
    if (pool) { pool->variant = 0; pool->flags54 = 0x90000000; pool->rot[1] = 0x200; pool->scale = 0x3ff; pool->frame = 0xe; }
    medusa_idle(m);
    m->anim = 0; obj_set_anim(O, m, 0);
    M.lives = getenv("BOSS_WEAK") ? 1 : 8; M.skel_timer = 500;   /* BOSS_WEAK: test hook */ M.gaze_delay = 3; M.first_reflect = 1;
    obj_sound(O, 0x1c6, m->x);
    static const uint32_t DROPS[3] = {0x10058b08, 0x10058ba0, 0x10058c40}, PATS[3] = {0x10058ce0, 0x10058d10, 0x10058d38};
    int d = P->difficulty; if (d < 0 || d > 2) d = 1;
    drop_list_set(DROPS[d]);
    M.pat_base = M.pat = PATS[d];
    M.beam[0][0] = M.beam[1][0] = -1;
    boss_bar(O, 8, 8);
}

static void medusa_level_tick(Objects *O) { medusa_skel_spawn(O); }

static int medusa_role_tick(Objects *O, Object *o)
{
    switch (o->ai) {
    case R_MEDUSA: medusa_tick(O, o); return 1;
    case R_MEDUSA_SKEL: medusa_skel_tick(O, o); return 1;
    case R_MEDUSA_ROCK: medusa_rock_tick(O, o); return 1;
    case R_MEDUSA_SHADOW: medusa_shadow_tick(O, o); return 1;
    case R_MEDUSA_PIECE: medusa_piece_tick(O, o); return 1;
    case R_MEDUSA_GAZE:
        if (o->variant == 8) {                                  /* FUN_10021780: brazier flames */
            int d = ring_d(o->x, O->P->o.x);
            o->w[11] = (d < 0x400 && d > -0x401);                /* hidden next to the camera */
            if ((M.frame & 1) == o->w[0] && ++o->frame > 100) o->frame = 0x5d;
            return 1;
        }
        medusa_misc_tick(O, o);
        return 1;
    }
    return 0;
}

/* The gaze rays: the original draws them as one mesh whose vertices are shared between the rig's parts
 * (vertex transform 2, FUN_10031990). Drawn here as a line of additive sparkles from eye to head. */
static void medusa_draw_beams(Objects *O, Renderer *R)
{
    /* FX2: the gaze is geometry, as in the original: rig 0x2f with six 0x2032 parts (model 6, FUN_10022190). Kids 0
     * and 3 start a chain (transform 0); kids 1/2 and 4/5 use transform 2 with list 0x10058d78, so their vertices
     * 4..7 take the previous part's vertices 0..3. Each beam is a tube from Medusa's eyes (kid 0/3) out to the gaze
     * head (kid 1/4, double size) and on to kid 2/5 (back at the eyes, or the reflection point). Additive, bright. */
    for (int k = 0; k < 2; k++) {
        if (M.beam[k][0] < 0) continue;
        if (getenv("FX2_LOG")) for (int i = 0; i < 3; i++) if (M.kid[k*3+i]) printf("beam%d st=%d kid%d (%d,%d,%d)\n", k, M.beam[k][0], i, I(M.kid[k*3+i]->x), I(M.kid[k*3+i]->y), I(M.kid[k*3+i]->z));
        Vec3 W[64]; int wv = 0;
        for (int i = 0; i < 3; i++) {
            Object *q = M.kid[k * 3 + i];
            if (!q || !q->used || !(q->x || q->y || q->z)) { wv = 0; continue; }
            boss_model_chain(O, R, q, i == 0 ? 0 : 2, i == 0 ? 0 : 0x10058d78u, W, &wv);
        }
    }
}

