/* LEVELS: Battle of the Titans (level 9): Hercules flies Pegasus through an auto-scrolling sky.
 * Ported from ENG1:
 *   FUN_100374c0  player init on level 9 (Pegasus bank 48, anim list 0x10063530, children 0x4028/0x4029)
 *   FUN_100379e0  player state 0x28 (flight; sword = anim 2 -> state 0x48, back to flight when it ends)
 *   FUN_100377f0  flight physics: vx around the scroll speed, vy, accel 2, drag 1, max 7.5 (16.16)
 *   FUN_10037770  keep Pegasus 0x40 above the ground (FUN_10020ea0 probe)
 *   FUN_1003c170 / FUN_1003aef0 / FUN_1003af20  scroll anchor actor 0x36: x += DAT_100707e8 (4.0) per tick,
 *                 Hercules kept within [anchor - 0x70, anchor + 0xa0]
 *   FUN_1003adc0  Zeus (actor 0x45): the scroll slows to a stop within 0xc8 of him; touching him starts a
 *                 0x78-tick countdown that ends the level (game state 0x31). */
#include "game.h"
#include "objects_int.h"
#include "titans_ai.h"
#include "sfx.h"   /* TITANS2 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef FX
#define FX(v) ((int32_t)((v) * 65536))
#define ANIMS 0x10063530u

static void set_anim(Game *G, int n)
{
    TitansState *T = &G->T;
    T->script = pe_u32(&G->eng1, ANIMS + 4u * (uint32_t)n);
    T->idx = -1; T->timer = 0; T->anim = n; T->anim_done = 0;
}

/* FUN_100283e0 subset: {s16 frame, u16 ticks}; -1 = end of the animation (loops for the flight cycle). */
static void run_anim(Game *G)
{
    TitansState *T = &G->T;
    if (--T->timer >= 0) return;
    for (int guard = 0; guard < 32; guard++) {
        T->idx++;
        int16_t v = pe_s16(&G->eng1, T->script + 4u * (uint32_t)T->idx);
        uint16_t arg = pe_u16(&G->eng1, T->script + 4u * (uint32_t)T->idx + 2);
        if (v >= 0) { T->frame = v; T->timer = (int16_t)arg; return; }
        if (v == -1) { T->anim_done = 1; T->idx = -1; continue; }
        if (v == -40 && T->nsounds < 4) T->sounds[T->nsounds++] = arg;   /* sound op */
    }
}

/* 2-byte child scripts (FUN_10028820): word 0 = ticks per frame, then frames, -40 sound, -1 loop. */
static int child_frame(Game *G, uint32_t script, int t)
{
    int ticks = pe_s16(&G->eng1, script) + 1, n = 0;
    int16_t fr[16];
    for (int k = 1; k < 32 && n < 16; k++) {
        int16_t v = pe_s16(&G->eng1, script + 2u * (uint32_t)k);
        if (v == -1) break;
        if (v == -40) { k++; continue; }
        fr[n++] = v;
    }
    return n ? fr[(t / ticks) % n] : 0;
}

/* ENG3GAME: collision box k of Pegasus' current frame (FUN_10030e40 layout), world units {x0,y0,z0,x1,y1,z1}. */
static int peg_box(Game *G, int k, int box[6])
{
    TitansState *T = &G->T;
    const Bank *bk = &T->bank;
    int fr = T->frame;
    if (!bk->d || fr <= 0 || fr > bk->count) return 0;
    const uint8_t *d = bk->d;
    uint32_t ea = 4 + 4u * (uint32_t)fr;
    if (ea + 4 > bk->size) return 0;
    uint32_t e = d[ea] | d[ea + 1] << 8 | d[ea + 2] << 16 | (uint32_t)d[ea + 3] << 24;
    if (!e) return 0;
    uint32_t f = (e >> 8) + 4;
    if (f + 2 > bk->size || (int8_t)d[f + 1] < k) return 0;
    const int8_t *c = (const int8_t *)(d + f + 2 + 6 * (k - 1));
    if (!c[0] && !c[1] && !c[2] && !c[3] && !c[4] && !c[5]) return 0;
    int X = T->x >> 16, Y = T->y >> 16, Z = T->z >> 16;
    box[0] = X + c[0]; box[3] = X + c[3];
    box[1] = Y + c[1]; box[4] = Y + c[4];
    box[2] = Z + (c[2] >> 1); box[5] = Z + (c[5] >> 1);
    return 1;
}

/* TITANS3: FUN_10020ea0 - the map's height-limit tables (DAT_10070d4c, count = map header u16 +0xa). Table t is a
 * self-relative offset to {u16 n, u16 pad, n x 16-byte keys {u16 x, u16 centre, u16 ?, u16 height, ...}}; the
 * key pair around x is interpolated (extrapolated before the first key) and gives top = centre - height/2,
 * bottom = centre + height/2. Returns 0 when the table does not exist. */
static int limit_table(const Level *L, int t, int x, int *top, int *bot)
{
    const uint8_t *d = L->d;
    #define LU16(o) ((int)(d[o] | d[(o) + 1] << 8))
    #define LU32(o) ((uint32_t)(d[o] | d[(o) + 1] << 8 | d[(o) + 2] << 16 | (uint32_t)d[(o) + 3] << 24))
    if (!d || L->size < 0x14 || t >= LU16(0xa)) return 0;
    uint32_t tab = L->blocks + 4u * (uint32_t)(LU16(0x12) + LU16(0x10) + LU16(0xe) + LU16(0xc)) + 4u * (uint32_t)t;
    if (tab + 4 > L->size) return 0;
    uint32_t base = tab + (LU32(tab) & ~3u);
    if (base + 4 > L->size) return 0;
    int n = LU16(base); if (n == 0) n = 1;
    if (x < 0) x = 0;
    uint32_t cur = base + 4, nxt = cur;
    int nx = 8;                                       /* param_5 when there is a single key */
    for (int i = 1; i < n; i++) {
        nxt = cur + 16;
        if (nxt + 16 > L->size) break;
        nx = LU16(nxt);
        if (x < nx) break;
        cur = nxt;
    }
    if (cur + 16 > L->size) return 0;
    int cx = LU16(cur), span = cx - nx, f = 0;
    if (span) f = (int)(((int64_t)(x - cx) * 0x10000) / span);    /* negative fraction, as in ENG1 */
    int a = LU16(cur + 2), c = LU16(cur + 6) >> 1, A = LU16(nxt + 2), C = LU16(nxt + 6) >> 1;
    *top = (int16_t)(((((C - A) - c + a) * f) >> 16) - c + a);
    *bot = (int16_t)(((((c - A) - C + a) * f) >> 16) + c + a);
    #undef LU16
    #undef LU32
    return 1;
}

static void set_children(Game *G, uint32_t wing, uint32_t rider)
{
    TitansState *T = &G->T;
    T->wing_script = wing; T->rider_script = rider; T->wing_t0 = T->ticks;
}

/* FUN_10037080: Pegasus is killed */
static void peg_die(Game *G)
{
    TitansState *T = &G->T;
    if (T->state == 0x23) return;
    G->P.health = 0;
    if (T->nsounds < 4) T->sounds[T->nsounds++] = 0x601c;
    T->state = 0x23;
    if (G->P.lives > 0) G->P.lives--;
    if (G->P.lives < 1) G->P.game_over = 1;
    set_anim(G, 8);
    set_children(G, 0x100634b0, 0x100634d8);
    T->death_timer = 0;
}

/* FUN_10037e30: Pegasus is hit (level 0..7 indexes the damage table at 0x10055348 by difficulty) */
static void peg_hurt(Game *G, int level)
{
    TitansState *T = &G->T;
    if (T->invuln > 0 || G->P.cheats & 2 || getenv("TITANS_GOD")) return;   /* TITANS2: TITANS_GOD test hook */
    if (T->state == 0x24 || T->state == 0x23) return;
    if (level < 8) G->P.health -= pe_s16(&G->eng1, 0x10055348 + 4u * (uint32_t)(level + (G->P.difficulty & 3) * 8));
    else G->P.health -= 1;
    G->P.flame.active = 0;                                /* TITANS2: FUN_1001a410 */
    if (getenv("TITANS_LOG")) printf("[%ld] pegasus hit level %d health %d\n", T->ticks, level, G->P.health);
    if (G->P.health < 1 && !(G->P.cheats & 1)) { peg_die(G); return; }
    if (T->nsounds < 4) T->sounds[T->nsounds++] = (T->ticks & 1) ? 0x6279 : 0x6278;
    T->state = 0x24;
    set_anim(G, 1);
    set_children(G, 0x10063350, 0x10063378);
}

void titans_start(Game *G, int x, int y, int z)
{
    TitansState *T = &G->T;
    sfx_stop(0x246, 0); sfx_stop(0x256, 0);   /* LV_TITANS: a restart (death) removes the Titans; their lava / tornado loops must not keep playing */
    memset(T, 0, sizeof *T);
    if (!T->bank.d) bank_load(&T->bank, G->fs, "P:\\SRC1\\ALIEN2\\PEGASUS\\ANIMPSX.BIN");
    T->active = 1;
    T->x = FX(x); T->y = FX(y); T->z = FX(z);
    T->ax = T->x; T->ay = T->y;
    T->speed = 0x40000;                         /* DAT_100707e8 */
    T->vx = T->speed;
    set_anim(G, 0);
    T->state = 2; T->invuln = 0; T->zeus_obj = -1;
    set_children(G, 0x10063318, 0x10063338);
    T->az = T->z;                                       /* TITANS2 */
    {   /* TITANS2: FUN_1003c170 level start for the actors (titans_ai.c) */
        TitansCtx *c = titans_ai_ctx();
        c->speed = &T->speed; c->zpush = &T->zpush;
        titans_ai_level_start(G->have_objects ? G->O.checkpoint : 0);
        c->speed = &T->speed; c->zpush = &T->zpush;
        /* TITANS3: the actors spawned on (re)start read these straight away (the spy takes z = Pegasus z + 8 and
         * is dropped when it is behind the anchor) - before the first tick they were still 0 */
        c->ax = T->ax; c->ay = T->ay; c->az = T->az; c->px = T->x; c->py = T->y; c->pz = T->z; c->peg_bank = &T->bank;
    }
    /* Zeus' spawn record (type 0x45) marks the end of the flight */
    if (getenv("TITANS_DIFF")) G->P.difficulty = atoi(getenv("TITANS_DIFF"));   /* TITANS3 test hook: 0 easy, 1 medium, 2 hard */
    T->zeus_x = -1;
    if (G->have_objects)
        for (int i = 0; i < G->O.nsp; i++)
            if (G->O.sp[i].type == 0x45) { T->zeus_x = G->O.sp[i].x; T->zeus_y = G->O.sp[i].y; }
}

/* TITANS3: TITANS_AUTO test pilot (not game logic). It fights harpies and the spy from sword range (the sword box
 * reaches x +16..+60 of Pegasus, the body box only to +23), steps out of the way of anything about to touch the
 * body box, ducks the screen-space tornado arms / lava blobs, and dips to collect checkpoints. */
static int box_of(Game *G, const Object *e, int b[6])
{
    if (obj_box(&G->O, e, 1, b)) return 1;
    int X = e->x >> 16, Y = e->y >> 16, Z = e->z >> 16;
    b[0] = X - 16; b[3] = X + 16; b[1] = Y - 32; b[4] = Y; b[2] = Z - 64; b[5] = Z + 64;
    return 0;
}
static uint16_t titans_pilot(Game *G, uint16_t pad)
{
    TitansState *T = &G->T;
    int PX = T->x >> 16, PY = T->y >> 16, spd = T->speed >> 16;
    pad &= ~(PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT | PAD_SQUARE);
    pad |= PAD_SQUARE;                                       /* keep swinging: the sword box is live 2 ticks per swing */
    int ty = T->zeus_x >= 0 ? (T->zeus_y >> 16) - 90 : PY, tx = PX, aim = 0;
    int threat = 0, push_x = 0, push_y = 0;
    int64_t bd = (int64_t)1 << 62;
    for (int i = 0; i < OBJ_MAX; i++) {
        const Object *e = &G->O.obj[i];
        if (!e->used || (e->flags54 & 0x40000000u)) continue;
        int hz = e->type == 0x5c || e->type == 0x5d || e->type == 0x32 || e->type == 0x41 || e->type == 0x52 || e->type == 0x53
              || e->type == 0x57 || e->type == 0x5b || e->type == 0x3d || e->type == 0x3f || e->type == 0x39 || e->type == 0x3a;
        if (!hz || ((e->type == 0x5c || e->type == 0x5d) && e->v2a == 0x23)) continue;
        int b[6]; box_of(G, e, b);
        int w = (b[3] - b[0]) / 2, h = (b[4] - b[1]) / 2, cx = (b[0] + b[3]) / 2, cy = (b[1] + b[4]) / 2;
        int vx = (e->v[0] >> 16) + ((e->type == 0x5c || e->type == 0x5d) ? spd / 2 : 0) - (T->vx >> 16), vy = (e->v[4] - T->vy) >> 16;   /* relative to Pegasus */
        const int *pb = titans_ai_ctx()->pbody;
        int bx0 = pb[0] - PX, bx1 = pb[3] - PX, by0 = pb[1] - PY, by1 = pb[4] - PY;
        if (!titans_ai_ctx()->have_pbody || T->state == 0x24 || bx1 - bx0 > 80) { bx0 = -12; bx1 = 23; by0 = -30; by1 = 28; }
        for (int k = 0; k <= 16; k += 2) {                   /* will it touch Pegasus' body box soon? */
            int x = cx + vx * k - PX, y = cy + vy * k - PY;
            if (x + w > bx0 - 16 && x - w < bx1 + 16 && y + h > by0 - 16 && y - h < by1 + 16) {
                threat = 1; push_y += y < 0 ? 3 : -3; push_x += x > 0 ? -1 : 1; break;
            }
        }
        if ((e->type == 0x5c || e->type == 0x5d) && cx > PX && cx < PX + 360) {
            int64_t d = (int64_t)(cx - PX) * (cx - PX) + (int64_t)(cy - PY) * (cy - PY) * 4;
            if (d < bd) { bd = d; aim = 1; tx = cx - w - 40; ty = cy + 7; }
        }
    }
    for (int i = 0; i < OBJ_MAX; i++) {                      /* screen-space attacks */
        const Object *e = &G->O.obj[i];
        if (!e->used || !(e->flags54 & 0x40000000u) || (e->type != 0x34 && e->type != 0x56)) continue;
        int sx = e->x >> 16, sy = e->y >> 16;
        if (abs(sx - (int)T->sx) < 100 && abs(sy - (int)T->sy) < 80) { threat = 1; push_y += sy < (int)T->sy ? 2 : -2; }
    }
    if (!aim) for (int i = 0; i < OBJ_MAX; i++) {            /* dip to an unclaimed checkpoint ahead */
        const Object *e = &G->O.obj[i];
        if (e->used && (e->type == 0x400b || e->type == 0x400c) && e->script == 0x100533c8 && (e->x >> 16) > PX - 20) { ty = (e->y >> 16) - 8; break; }
    }
    if (T->zeus_x >= 0 && (T->zeus_x >> 16) - PX < 400 && T->speed < 0x10000) {   /* the scroll has stopped at Zeus: fly into him */
        tx = T->zeus_x >> 16; ty = (T->zeus_y >> 16) - 60; threat = 0;
    }
    if (threat) {
        if (push_y > 0) pad |= PAD_DOWN; else if (push_y < 0) pad |= PAD_UP;
        if (push_x < 0) pad |= PAD_LEFT; else if (push_x > 0) pad |= PAD_RIGHT;
    } else {
        if (tx > PX + 4) pad |= PAD_RIGHT; else if (tx < PX - 4) pad |= PAD_LEFT;
        if (ty > PY + 4) pad |= PAD_DOWN; else if (ty < PY - 4) pad |= PAD_UP;
    }
    return pad;
}

void titans_tick(Game *G, uint16_t pad, uint16_t pressed)
{
    TitansState *T = &G->T;
    const Level *L = &G->L;
    T->nsounds = 0;
    T->ticks++;
    G->P.ext_box_on = 0;                                  /* TITANS2: no stale sword box while dying / at Zeus */
    titans_ai_ctx()->sword_on = 0; titans_ai_ctx()->pstate = T->state;
    if (T->invuln > 0) T->invuln--;                      /* DAT_1006fff0 */
    if (G->P.hit_req) {                                   /* ENG3GAME: hits requested by enemies (player_damage) */
        peg_hurt(G, (G->P.hit_req - 1) & 0xf);
        G->P.hit_req = 0;
    }
    if (T->state == 0x23) {                               /* dying: fall, then respawn */
        T->y += 0x40000;
        run_anim(G);
        if (++T->death_timer > 60) G->P.dead = 1;
        return;
    }
    if (T->countdown > 0) {                     /* Zeus reached: hold, then the level is complete */
        T->x += T->speed; T->ax += T->speed;
        if (--T->countdown <= 0) G->titans_done = 1;
        run_anim(G);
        return;
    }
    if (getenv("TITANS_SEEK") && T->zeus_x >= 0) {      /* test hook: steer towards Zeus' height */
        pad &= ~(PAD_UP | PAD_DOWN);
        if (T->y > T->zeus_y - 60 * 65536) pad |= PAD_UP; else if (T->y < T->zeus_y - 120 * 65536) pad |= PAD_DOWN;
    }
    if (getenv("TITANS_AUTO") && G->have_objects) pad = titans_pilot(G, pad);   /* TITANS3 test pilot */
    if (getenv("LV_TP") && T->ticks == 20) {   /* LV_TITANS test hook: LV_TP=x,y teleports Pegasus (the anchor follows) */
        int tx = 0, ty = 0; sscanf(getenv("LV_TP"), "%d,%d", &tx, &ty);
        T->ax += FX(tx) - T->x; T->x = FX(tx); T->y = FX(ty); if (T->ay > T->y) T->ay = T->y;
    }
    /* FUN_100377f0 */
    if ((pad & PAD_DOWN) && T->vy < 0x78000) T->vy += 0x20000;
    if ((pad & PAD_UP) && T->vy > -0x78000) T->vy -= 0x20000;
    if (T->vy < 1) { if (!(pad & PAD_DOWN) && (T->vy += 0x10000) > 0) T->vy = 0; }
    else if (!(pad & PAD_UP) && (T->vy -= 0x10000) < 0) T->vy = 0;
    if ((pad & PAD_RIGHT) && T->vx < T->speed + 0x78000) T->vx += 0x20000;
    if ((pad & PAD_LEFT) && T->vx > T->speed - 0x78000) T->vx -= 0x20000;
    if (T->vx > T->speed) { if (!(pad & PAD_LEFT) && (T->vx -= 0x10000) < T->speed) T->vx = T->speed; }
    else if (!(pad & PAD_RIGHT) && (T->vx += 0x10000) > T->speed) T->vx = T->speed;
    T->x += T->vx;
    T->y += T->vy;
    /* FUN_10037770: stay 0x40 above the ground and below the top of the map */
    {   /* TITANS3: FUN_10037770 - bottom of limit table 1 minus 0x40, top of limit table 0 */
        int top, bot, top0, bot0, X = T->x >> 16;
        if (limit_table(L, 1, X, &top, &bot) && T->y > FX(bot - 0x40)) T->y = FX(bot - 0x40);
        if (limit_table(L, 0, X, &top0, &bot0) && T->y < FX(top0)) T->y = FX(top0);
        if (getenv("TITANS_LIMITS") && !(T->ticks % 60)) printf("[%ld] x %d y %d limits t0 %d..%d t1 %d..%d\n", T->ticks, X, T->y >> 16, top0, bot0, top, bot);
    }
    /* FUN_1003af20: scroll anchor */
    T->ax += T->speed;
    if (T->ax - T->x > 0x700000) T->x = T->ax - 0x700000;
    if (T->ax - T->x < -0xa00000) T->x = T->ax + 0xa00000;
    if (T->ay - T->y > 0xa00000) T->ay = T->y + 0xa00000;
    if (T->ay < T->y) T->ay = T->y;
    {   /* TITANS3: FUN_1003af20 also keeps the anchor 0x10 above the bottom of limit table 1 */
        int top, bot;
        if (limit_table(L, 1, T->ax >> 16, &top, &bot) && T->ay > FX(bot - 0x10)) T->ay = FX(bot - 0x10);
    }
    /* sword (anim 2, state 0x48); hurt (anim 1, state 0x24) */
    /* TITANS2: FUN_100379e0 attacks: sword (anim 2, state 0x48), fireball (3 -> 4 hold -> 5, states 0x49 / 0x4a / 0x4b),
     * lightning (6 -> 7 bolt 0x4005 -> 5, states 0x4c / 0x4d). The held sword button attacks with the selected Gift
     * (DAT_1006efb4 = P.weapon: 0 sword, 1 fireball, 2 lightning); L1 / R1 select. */
    Player *P = &G->P;
    P->nspawns = 0;
    for (int w = 0; w < 5; w++)                                  /* weapon charge timers (DAT_10072140) */
        if (--P->weapon_timer[w] < 0) P->weapon_timer[w] = pe_s16(&G->eng1, 0x10055320u + 2u * (uint32_t)w);
    if (T->state == 2 && (pressed & (PAD_L1 | PAD_R1))) {
        int old = P->weapon, step = (pressed & PAD_L1) ? -1 : 1;
        do { P->weapon = (P->weapon + step + 3) % 3; } while (P->weapon && P->ammo[P->weapon] <= 0);
        if (P->weapon != old && T->nsounds < 4) T->sounds[T->nsounds++] = 0x27;
    }
    if (P->weapon > 2 || (P->weapon && P->ammo[P->weapon] <= 0)) P->weapon = 0;
    if (T->state == 2 && (pad & PAD_SQUARE)) {
        if (P->weapon == 0) { set_anim(G, 2); T->state = 0x48; }
        else if (P->weapon == 1) { set_anim(G, 3); T->state = 0x49; }
        else { set_anim(G, 6); T->state = 0x4c; }
    }
    run_anim(G);
    switch (T->state) {
    case 0x48: case 0x4b: if (T->anim_done) { set_anim(G, 0); T->state = 2; } break;   /* FUN_100372b0 */
    case 0x49: if (T->anim_done) { set_anim(G, 4); T->state = 0x4a; P->flame.active = 1; } break;
    case 0x4a:
        if (!(pad & PAD_SQUARE) || P->weapon != 1) { set_anim(G, 5); T->state = 0x4b; P->flame.active = 0; }
        else if (P->weapon_timer[1] == 0 && --P->ammo[1] < 1) {          /* FUN_100371a0 */
            P->ammo[1] = 0; P->weapon = 0; P->flame.active = 0;
        }
        break;
    case 0x4c:
        if (T->anim_done) {
            set_anim(G, 7); T->state = 0x4d;
            Spawn *sp = &P->spawns[P->nspawns++];
            sp->id = 0x4005; sp->x = T->x + 0x200000; sp->y = T->y + 0x140000; sp->z = T->z; sp->facing = 0;
            if (--P->ammo[2] < 1) { P->ammo[2] = 0; P->weapon = 0; }
        }
        break;
    case 0x4d: if (T->anim_done) { set_anim(G, 5); T->state = 0x4b; } break;
    }
    if (P->flame.active) { P->flame.x = T->x + 0x340000; P->flame.y = T->y - 0x140000; P->flame.z = T->z; P->flame.angle = 0x300; }
    /* TITANS2: FUN_1003af20 anchor z drift requested by the 0x46..0x4b markers */
    if (T->zpush > 0) { T->az += 0x20000; T->zpush -= 2; }
    else if (T->zpush < 0) { T->az -= 0x20000; T->zpush += 2; }
    if (T->state == 0x24 && T->anim_done) {               /* FUN_10037230 */
        T->state = 2; set_anim(G, 0); set_children(G, 0x10063318, 0x10063338); T->invuln = 0xf;
    }
    /* sword blow box for enemies (player_attack_box) */
    G->P.ext_box_on = 0;
    G->P.hitflags = 0;
    if (T->state == 0x48) {
        int b[6];
        if (peg_box(G, 2, b)) {
            G->P.ext_box_on = 1;
            G->P.ext_box = (Box){b[0], b[1], b[2], b[3], b[4], b[5]};
            G->P.hitflags = HIT_SWORD;
        }
    }
    G->P.consumed = 0;
    {   /* TITANS2: what the level-9 actors see (titans_ai.c) */
        TitansCtx *c = titans_ai_ctx();
        c->frame++;
        c->ax = T->ax; c->ay = T->ay; c->az = T->az;
        c->px = T->x; c->py = T->y; c->pz = T->z; c->pstate = T->state;
        c->have_pbody = peg_box(G, 1, c->pbody);
        c->sword_on = G->P.ext_box_on;
        if (c->sword_on) { Box *e = &G->P.ext_box; c->sword[0] = e->x0; c->sword[1] = e->y0; c->sword[2] = e->z0; c->sword[3] = e->x1; c->sword[4] = e->y1; c->sword[5] = e->z1; }
        c->camx = (int)G->camx; c->camy = (int)G->camy;
        c->peg_bank = &T->bank;                           /* TITANS3 */
        if (T->have_sxy) { c->psx = T->sx; c->psy = T->sy; } else { c->psx = 112; c->psy = 120; }
        G->P.shake_y = c->shake >> 16;                    /* DAT_10070cbc rock-impact shake */
    }
    /* FUN_1003adc0: Zeus */
    if (T->zeus_x >= 0) {
        if (T->zeus_x - T->ax < 0xc80000 && (T->speed -= 0x1999) < 0) T->speed = 0;
        /* FUN_1003adc0: Pegasus' body box (+0x88) touching Zeus' frame box -> sound 0x627b, 0x78 ticks */
        int pb[6], zb[6], hit = 0, tested = 0;
        if (G->have_objects && peg_box(G, 1, pb)) {
            for (int i = 0; i < OBJ_MAX && !hit; i++) {
                const Object *z = &G->O.obj[i];
                if (!z->used || z->type != 0x45) continue;
                if (obj_box(&G->O, z, 1, zb)) { tested = 1; hit = obj_boxes_overlap(pb, zb); }
                if (getenv("TITANS_DEBUG")) printf("zeus box %d %d %d-%d %d %d peg %d %d %d-%d %d %d\n", zb[0], zb[1], zb[2], zb[3], zb[4], zb[5], pb[0], pb[1], pb[2], pb[3], pb[4], pb[5]);
            }
        }
        if (!tested) {
            int dx = (T->x - T->zeus_x) >> 16, dy = (T->y - T->zeus_y) >> 16;
            hit = dx > -80 && dx < 80 && dy > -200 && dy < 64;
        }
        if (hit) { T->countdown = 0x78; if (T->nsounds < 4) T->sounds[T->nsounds++] = 0x627b; }
    } else if ((T->x >> 16) > L->ncols * 64 - 400) T->countdown = 0x78;
}

void titans_sync_player(Game *G)
{
    /* the rest of the game (objects, camera, HUD) sees Pegasus as the player object */
    TitansState *T = &G->T;
    Obj *o = &G->P.o;
    o->px = o->x; o->py = o->y; o->pz = o->z;
    o->x = T->x; o->y = T->y; o->z = T->z;
    o->bank = 0; o->facing = 0;
    o->timer = (int16_t)(T->ticks & 1);                 /* TITANS2: fireball stream cadence (enemies.c) */
}

void titans_draw(Game *G, Renderer *R)
{
    TitansState *T = &G->T;
    float wx, wz;
    level_path_to_world(&G->L, T->x / 65536.0f, T->z / 65536.0f, &wx, &wz, NULL);
    float y = T->y / 65536.0f;
    { float sx, sy, zc; if ((T->have_sxy = render_project(R, wx, y, wz, &sx, &sy, &zc))) { T->sx = sx; T->sy = sy; } }   /* TITANS2 */
    render_sprite(R, bank_frame(&T->bank, T->frame), wx, y, wz, 0, -8);
    if (T->invuln > 0 && (T->ticks & 2)) return;         /* hit flicker */
    {   /* TITANS2: the wing / horse children (DAT_100707dc / DAT_100707fc) are updated and drawn every frame
         * (FUN_10037390 / FUN_10037400), whatever Pegasus' own animation is */
        long t = T->ticks - T->wing_t0;
        render_sprite(R, bank_frame(&T->bank, child_frame(G, T->wing_script ? T->wing_script : 0x10063318, (int)t)), wx, y, wz, 0, -9);
        render_sprite(R, bank_frame(&T->bank, child_frame(G, T->rider_script ? T->rider_script : 0x10063338, (int)t)), wx, y, wz, 0, -7);
    }
}
