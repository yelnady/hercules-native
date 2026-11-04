/* ENG3GAME: ENG3.DLL gameplay (Training Gauntlet, Cyclops chase, Passageways of Torment).
 *
 * Hercules runs into the screen on his own; left/right steer, up runs faster, down slows down (or turns
 * back on the spot), jump jumps. There is no attack in these levels: obstacles and creatures hurt on contact.
 * Ported from ENG3.DLL:
 *   FUN_100178a0 / FUN_1001c720  player and level start          FUN_100174e0  per-tick player update
 *   state table DAT_10041c80:   0 FUN_10018600 stand   1 FUN_10018950 run   2 FUN_10018eb0 jump
 *                               3 FUN_10019030 fall    5 FUN_100195d0 hit    6 FUN_10019b70 die
 *                               8 FUN_10018880 power-up run   9 FUN_10019cd0 level won   10 FUN_10018c90 end run
 *   FUN_10018370 script interpreter, FUN_10019f30 / FUN_1001a070 / FUN_1001a180 speed / steering / vertical,
 *   FUN_1001e8e0 / FUN_1001eaf0 / FUN_1001ea60 floor follow / landing, FUN_1001b2e0 floor, FUN_1001b4a0
 *   attributes, FUN_10017ae0 attribute effects, FUN_10022b30 / FUN_10022620 path changes,
 *   FUN_10019200 / FUN_10019410 hits and damage, FUN_10019a30 death, FUN_10019c20 victory.
 * Objects: FUN_1001c010 (spawn tables), FUN_1001c290 (activation), FUN_1001c9f0 / FUN_1001d050 /
 *   FUN_1001d570 (spawn per class), FUN_1001dbc0 (object scripts), FUN_10023d90 / FUN_10023ef0 (3D models). */
#include "game.h"
#include "e3world.h"
#include "eng3.h"
#include "sfx.h"
#include "scrolls.h"       /* AUDSHELL */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static E3World *Wg;     /* one world (the engine runs one level at a time) */

#define FX(v) ((int32_t)((v) * 65536))
#define HI(v) ((int16_t)((uint32_t)(v) >> 16))

/* ENG3 player scripts */
enum {
    S_RUN = 0x10037c38, S_RUN_FAST = 0x10037cd8, S_RUN_SLOW = 0x10037ca0, S_STAND = 0x10037df0,
    S_JUMP = 0x10037e08, S_JUMP_BOOST = 0x10037fe8, S_FALL = 0x10037e48, S_DIE = 0x10037e58,
    S_DIE_SHIELD = 0x10037e98, S_WIN = 0x10037ea0, S_HIT = 0x10037f50, S_HIT_AIR = 0x10037f88,
    S_HIT_SIDE = 0x10037fb8,
};

static uint32_t d32(E3World *W, uint32_t va) { return pe_u32(W->dll, va); }
static uint16_t d16(E3World *W, uint32_t va) { return pe_u16(W->dll, va); }
static int16_t ds16(E3World *W, uint32_t va) { return pe_s16(W->dll, va); }
static uint8_t d8(E3World *W, uint32_t va) { const uint8_t *p = pe_ptr(W->dll, va, 1); return p ? *p : 0; }

static uint32_t rng_a = 0x12345678, rng_b = 0x9abcdef1;
uint32_t e3_rand(int n)     /* FUN_10001010 */
{
    rng_b += rng_a; rng_a += rng_b;
    if (rng_b & 0x80000000u) rng_a++;
    return n > 0 ? rng_a % (uint32_t)n : 0;
}

static inline uint32_t m32(const Level *L, uint32_t a) { return a + 4 <= L->size ? (uint32_t)(L->d[a] | L->d[a+1] << 8 | L->d[a+2] << 16 | (uint32_t)L->d[a+3] << 24) : 0; }
static inline uint16_t m16(const Level *L, uint32_t a) { return a + 2 <= L->size ? (uint16_t)(L->d[a] | L->d[a+1] << 8) : 0; }
static inline int16_t ms16(const Level *L, uint32_t a) { return (int16_t)m16(L, a); }
static inline uint32_t mrel(const Level *L, uint32_t a) { return a + (m32(L, a) & ~3u); }

/* ---------------------------------------------------------------- map queries */

/* FUN_1001b2e0: distance down to the floor (bits 0-15) and up to the floor above (16-31) for an object at
 * (depth, y, lateral) on a path; the surface is 0x40 above the object's y. */
int e3_floor(E3World *W, int d, int y, int l, int path, int *up)
{
    int a;
    int dn = eng3_floor(W->L, path, (float)d, (float)(l - 64), (float)(y - 0x40), &a);
    if (up) *up = a;
    return dn;
}

/* FUN_1001b4a0: attribute value at a point (cell attribute shape, interpolated like the ENG1 shapes). */
static int e3_attr(E3World *W, int d, int y, int l, int path)
{
    const Level *L = W->L;
    const Eng3Map *M = L->e3;
    if (path < 0 || path >= M->npaths) return 0;
    const E3Path *P = &M->path[path];
    static const int CT[16][4] = {{4,63,4,1},{8,31,8,2},{16,15,16,4},{32,7,32,8},{64,3,64,16}};
    uint32_t col = ((uint32_t)(l + 0x300) & 0xffff) >> 7;
    uint32_t row = ((uint32_t)(d - 0x40) & 0xffff) >> 7;
    for (int k = 0; k < P->ngrids; k++) {
        int g = P->g[k].grid;
        if ((int)col >= M->grid[g].w || (int)row >= M->grid[g].h) continue;
        uint32_t cell = eng3_cell(L, g, (int)row, (int)col);
        uint32_t ai = (cell >> 11) & 0x1fff;
        uint16_t shape = m16(L, M->attr + ai * 4);
        if (!shape) continue;
        uint32_t se = W->shapes - 4 + shape * 4u;
        uint32_t sp = se + (uint32_t)(int32_t)m32(L, se);
        uint16_t h = m16(L, sp);
        sp += 2;
        if (!h) return (int8_t)L->d[sp];
        int a = (h & 0xff) >> 4, b = h >> 8, c = h & 15;
        uint32_t off = 0;
        /* TORMENT: FUN_1001b4a0 indexes the stride table 0x10039a68 directly by a / b / c (as ENG1 FUN_10020d30);
         * the previous port used index - 1, which shifted every attribute shape (wall markers fired 13+ units early) */
        if (a) off += (uint32_t)(((0x7f - ((d - 0x40) & 0x7f)) >> 1) * CT[a][0] >> 8);
        if (b) off += (uint32_t)((CT[b][0] * ((((P->g[k].yoff * 0x20 + ((int)(int8_t)(cell >> 24) & ~1) * -2) - y) + 0x40) >> 1) >> 8) * CT[a][3]);
        if (c) off += (uint32_t)(((CT[c][0] * ((int)(((uint32_t)(l + 0x300) & 0xffff) >> 1) & 0x3f)) >> 8) * CT[b][3] * CT[a][3]);
        return (int8_t)L->d[sp + off];
    }
    return 0;
}

/* ---------------------------------------------------------------- sound */

static E3World *e3_sfx_world;
/* AUDSHELL: the emitter's current screen x (lateral offset from Hercules / 2, as e3_sound) for tracked (0x800) sounds */
static int e3_obj_sfx_pos(void *ctx, SfxPos *p)
{
    E3World *W = e3_sfx_world;
    int slot = (int)(intptr_t)ctx - 2;
    if (!W || slot < 1 || slot >= E3_MAXOBJ || !W->obj[slot].used) return 0;
    p->sx = 160 + (HI(OLAT(&W->obj[slot])) - HI(OLAT(&W->obj[0]))) / 2;
    p->depth = 160; p->hidden = 0;      /* lateral offset only: no visibility culling in the 3-D levels */
    return 1;
}

void e3_sound(E3World *W, int id, E3Obj *o)
{
    if (W->sfx_n >= 48) return;
    int sx = 160;
    if (o && o != &W->obj[0]) sx = 160 + (HI(OLAT(o)) - HI(OLAT(&W->obj[0]))) / 2;
    /* AUDSHELL: own = 0 no object, 1 the player, >= 2 world object (slot + 2: tracked, its loops die with it) */
    W->sfx[W->sfx_n].id = id; W->sfx[W->sfx_n].sx = sx;
    W->sfx[W->sfx_n].own = !o ? 0 : o == &W->obj[0] ? 1 : (int)(o - W->obj) + 2;
    W->sfx_n++;
}

/* ---------------------------------------------------------------- AUDSHELL: letter display (FUN_10016f30) */

static ScrollCfg e3_scr_cfg(E3World *W)
{
    ScrollCfg c = {W->dll, 0x10038100u, 0x10038120u, 0x10038080u, 0x320000, 1, 8, 8};
    return c;
}
static void e3_scr_sound(void *ctx, int x)       /* FUN_1000bc47(0x600d, icon): positional at the icon */
{
    E3World *W = ctx;
    if (W->sfx_n >= 48) return;
    W->sfx[W->sfx_n].id = 0x600d; W->sfx[W->sfx_n].sx = x; W->sfx[W->sfx_n].own = 1; W->sfx_n++;
}
static Bank *e3_scr_bank(void *ctx, int b) { E3World *W = ctx; return b > 0 && b < 10 && W->banks[b].d ? &W->banks[b] : NULL; }
void e3_scroll_trigger(E3World *W, int id) { if (W->scr) scrolls_trigger(W->scr, id); }

/* ---------------------------------------------------------------- objects */

E3Obj *e3_spawn(E3World *W, int type, int32_t d, int32_t y, int32_t l, int path)
{
    for (int i = 1; i < E3_MAXOBJ; i++) {
        E3Obj *o = &W->obj[i];
        if (o->used) continue;
        memset(o, 0, sizeof *o);
        o->used = 1; o->spath = -1; o->sidx = -1;
        ODEP(o) = d; OY(o) = y; OLAT(o) = l;
        OB(o, 0x1c) = 0xff;   /* FX2: FUN_1001d8a0 - no palette override (crowds set +0x1c to recolour) */
        OW(o, 0x2e) = (uint16_t)type;
        OW(o, 0x38) = (uint16_t)path;
        OW(o, 0x3a) = 0x100; OW(o, 0x3c) = 0x100; OW(o, 0x3e) = 0x100;
        OW(o, 0x4a) = 0x80;
        OS(o, 0x32) = -1;
        OB(o, 0x24) = 0x80;
        int cls = type & 0x7000, ix = type & 0x1ff;
        if (cls == 0) {
            uint32_t ti = W->tinfo + 16u * ix;
            OU(o, 0x64) = d32(W, ti);
            OU(o, 0x110) = d32(W, ti + 4);
            OB(o, 0x22) = d8(W, ti + 12);
        } else if (cls == 0x2000) {
            uint32_t ti = W->tinfo2 + 8u * ix;
            OU(o, 0x64) = d32(W, ti);
            OB(o, 0x22) = d8(W, ti + 6);
        } else if (cls == 0x4000) {
            uint32_t ti = W->tinfo4 + 8u * ix;
            OU(o, 0x64) = d32(W, ti);
            OB(o, 0x22) = d8(W, ti + 6);
        }
        if (OU(o, 0x110) && OU(o, 0x64) == 0) OU(o, 0x64) = d32(W, OU(o, 0x110));
        return o;
    }
    return NULL;
}

void e3_remove(E3World *W, E3Obj *o)    /* FUN_1001cb90 */
{
    if (!o->used || o == &W->obj[0]) return;
    if (o->spath >= 0 && o->sidx >= 0) {
        E3Spawn *s = &W->sp[o->spath][o->sidx];
        s->state &= 0x7fff;
        if (OW(o, 0x2a) & 0x8000) s->state |= 0x4000;  /* collected / destroyed for good */
    }
    for (int k = 0; k < o->nchild; k++) {     /* children (FUN_1001f410) go with their parent */
        E3Obj *c = &W->obj[o->child[k]];
        if (c->used && c->parent == (int)(o - W->obj) + 1) c->used = 0;
    }
    o->nchild = 0;
    if (W->sfx_n < 48) { W->sfx[W->sfx_n].id = 0xffff; W->sfx[W->sfx_n].sx = 0; W->sfx[W->sfx_n].own = (int)(o - W->obj) + 2; W->sfx_n++; }   /* AUDSHELL: loops end with the owner */
    o->used = 0;
}

E3Obj *e3_child(E3World *W, E3Obj *parent, int32_t d, int32_t y, int32_t l, int type)
{
    if (parent->nchild >= 8) return NULL;
    E3Obj *c = e3_spawn(W, type, d, y, l, OW(parent, 0x38));
    if (!c) return NULL;
    c->parent = (int)(parent - W->obj) + 1;
    parent->child[parent->nchild++] = (int)(c - W->obj);
    e3_type_init(W, c);
    return c;
}

void e3_move(E3Obj *o)      /* FUN_1001f550 */
{
    int32_t v = OI(o, 0xa8) + (OI(o, 0xb0) - OI(o, 0xb4));
    if (v < 0) v = 0;
    if (v > OI(o, 0xac)) v = OI(o, 0xac);
    OI(o, 0xa8) = v;
    int32_t w = OI(o, 0xb8) + (OI(o, 0xc0) - OI(o, 0xc4));
    if (w < 0) w = 0;
    if (w > OI(o, 0xbc)) w = OI(o, 0xbc);
    OI(o, 0xb8) = w;
    ODEP(o) += OB(o, 0x94) ? -v : v;
    if (!OB(o, 0x95)) OY(o) += w; else OY(o) -= w;
}

void e3_move_lat(E3Obj *o)  /* FUN_1001f620 */
{
    int32_t v = OI(o, 0xc8) + (OI(o, 0xd0) - OI(o, 0xd4));
    if (v < 0) v = 0;
    if (v > OI(o, 0xcc)) v = OI(o, 0xcc);
    OI(o, 0xc8) = v;
    if (OB(o, 0x96)) OLAT(o) -= v; else OLAT(o) += v;
}

int e3_cos(int a) { return (int)lrint(4096.0 * cos((a & 0xfff) * (2 * M_PI / 4096.0))); }


void e3_set_anim(E3World *W, E3Obj *o, int n)   /* FUN_1001e2e0 */
{
    OS(o, 0x114) = (int16_t)n;
    OU(o, 0x64) = d32(W, OU(o, 0x110) + 4u * (uint32_t)n);
    OS(o, 0x32) = -1;
    OS(o, 0x30) = 0;
}

/* FUN_1001dbc0: object script, 4-byte {op, arg}. Returns 1 when the animation looped / changed. */
int e3_anim(E3World *W, E3Obj *o)
{
    int ret = 0;
    if (!OU(o, 0x64)) return 0;
    OS(o, 0x30)--;
    if (OS(o, 0x30) >= 0) return 0;
    for (int guard = 0; guard < 64; guard++) {
        OS(o, 0x32)++;
        uint32_t a = OU(o, 0x64) + 4u * (uint32_t)OS(o, 0x32);
        int16_t v = ds16(W, a), arg = ds16(W, a + 2);
        if (v >= 0) {
            OS(o, 0x34) = v;
            OS(o, 0x30) = arg;
            return ret;
        }
        switch (v) {
        case -0x30: OU(o, 0x54) &= ~0x800000u; break;
        case -0x2d: OI(o, 0xc8) = arg << 8; break;
        case -0x2c: OI(o, 0xb8) = arg << 8; break;
        case -0x2b: OI(o, 0xa8) = arg << 8; break;
        case -0x29: OW(o, 0x2a) = (uint16_t)arg; break;
        case -0x28: e3_sound(W, (uint16_t)arg, o); break;
        case -0x27: e3_sound(W, (uint16_t)arg, NULL); break;
        case -0x20: OI(o, 0x4c) = arg; break;
        case -0x1e: OU(o, 0x50) &= ~(uint32_t)(int32_t)arg; break;
        case -0x1d: OU(o, 0x50) |= (uint32_t)(int32_t)arg; break;
        case -0x1c: OLAT(o) += arg * 0x10000; break;
        case -0x1b: OY(o) += arg * 0x10000; break;
        case -0x1a: ODEP(o) += (OB(o, 0x94) ? -arg : arg) * 0x10000; break;
        case -0x19:
            if (o->spath >= 0) { E3Spawn *s = &W->sp[o->spath][o->sidx]; ODEP(o) = s->d; OY(o) = s->y; OLAT(o) = s->l; }
            break;
        case -0x18: OS(o, 0x36) = arg; break;
        case -0x17: OU(o, 0x54) &= ~((uint32_t)(int32_t)arg << 16); break;
        case -0x16: OU(o, 0x54) &= ~(uint32_t)(int32_t)arg; break;
        case -0x15: OU(o, 0x54) |= (uint32_t)(int32_t)arg << 16; break;
        case -0x14: OU(o, 0x54) |= (uint32_t)(int32_t)arg; break;
        case -0x12: OB(o, 0x22) = (uint8_t)arg; break;
        case -0x11: OW(o, 0x98) = (uint8_t)arg; break;
        case -0x10: OI(o, 0xf4) = arg; break;
        case -0x0f: OS(o, 0x11e) = (int16_t)-OS(o, 0x11e); OI(o, 0xf8) = 0; break;
        case -0x0e: if (OS(o, 0x11e) < 0) OS(o, 0x11e) = (int16_t)-OS(o, 0x11e); OI(o, 0xf8) = 0; break;
        case -0x0d: if (OS(o, 0x11e) > 0) OS(o, 0x11e) = (int16_t)-OS(o, 0x11e); OI(o, 0xf8) = 0; break;
        case -0x0c: e3_set_anim(W, o, arg); ret = 1; break;
        case -0x07: case -0x06: e3_remove(W, o); return ret;
        case -0x04:     /* next animation in the list (FUN_1001e280) */
            if (OU(o, 0x110)) {
                int n = OS(o, 0x114) + 1;
                uint32_t s = d32(W, OU(o, 0x110) + 4u * (uint32_t)n);
                if (!s) { n = (int)d32(W, OU(o, 0x110) + 4u * (uint32_t)(n + 1)); s = d32(W, OU(o, 0x110) + 4u * (uint32_t)n); }
                if (s) { OS(o, 0x114) = (int16_t)n; OU(o, 0x64) = s; }
            }
            /* fall through */
        case -0x01: OS(o, 0x32) = -1; ret = 1; break;
        case -0x03: OU(o, 0x54) ^= 0x10; break;
        case -0x02: OB(o, 0x94) = (uint8_t)~OB(o, 0x94); break;
        default: break;     /* -5 / -8 / -10 / -11 / -0x1f / -0x2e / -0x2f: effects and callbacks per type */
        }
    }
    return ret;
}

/* FUN_10023860 / FUN_10024870: collision box k (1 = body) of the current frame, in object units
 * {lateral, y, depth, w, h, d}. Returns 0 without one. */
static int frame_box(E3World *W, E3Obj *o, int k, int16_t b[6])
{
    int fr = OW(o, 0x34);
    if (!fr) return 0;
    int bank = OB(o, 0x22);
    int x, y, z, w, h, dz;
    const Level *L = W->L;
    if (bank == 0) {        /* model: boxes in the map's model record */
        if (fr >= W->nmodels) return 0;
        uint32_t e = W->mdl_tab + 4u * (uint32_t)fr, r = e + (uint32_t)(int32_t)m32(L, e);
        int nb = L->d[r + 2];
        if (nb < k) return 0;
        int32_t off = (int32_t)m32(L, r + 4);
        uint32_t bx = r + 8 + 8u * (uint32_t)(k - 1);
        uint32_t v0 = m32(L, bx), v1 = m32(L, bx + 4);
        /* TORMENT: FUN_10024870 field order: lateral = bits 8-23, y = bits 24-31, depth = bits 0-7 (as the model
         * vertices); sizes likewise. The previous decode swapped the axes (a raft stood 96 units tall). */
        x = (int16_t)(v0 >> 8); y = (int8_t)(v0 >> 24); z = (int8_t)v0;
        if (off) { x += (int8_t)off; y += (int16_t)((uint32_t)off >> 8); z += off >> 24; }
        w = (int)((v1 >> 8) & 0xffff); h = (int)(v1 >> 24); dz = (int)(v1 & 0xff);
        int s = OW(o, 0x3a);
        if (s != 0x100) { x = x * s >> 8; y = y * s >> 8; z = z * s >> 8; w = w * s >> 8; h = h * s >> 8; dz = dz * s >> 8; }
        b[0] = (int16_t)x; b[1] = (int16_t)y; b[2] = (int16_t)z; b[3] = (int16_t)w; b[4] = (int16_t)h; b[5] = (int16_t)dz;
        return w || h;
    }
    Bank *bk = &W->banks[bank < 10 ? bank : 0];
    if (bank >= 10 || !bk->d || fr > bk->count) return 0;
    const uint8_t *d = bk->d;
    uint32_t ea = 4 + 4u * (uint32_t)fr;
    if (ea + 4 > bk->size) return 0;
    uint32_t e = d[ea] | d[ea + 1] << 8 | d[ea + 2] << 16 | (uint32_t)d[ea + 3] << 24;
    if (!e) return 0;
    uint32_t f = (e >> 8) + 4;
    if (f + 2 > bk->size || (int8_t)d[f + 1] < k) return 0;
    const int8_t *c = (const int8_t *)(d + f + 2 + 6 * (k - 1));
    if (!c[0] && !c[1] && !c[2] && !c[3] && !c[4] && !c[5]) return 0;
    x = c[0]; y = c[1]; z = c[2] >> 1; w = c[3] - c[0]; h = c[4] - c[1]; dz = (c[5] >> 1) - z;
    int s = OW(o, 0x3a);
    if (s != 0x100) { x = x * s >> 8; y = y * s >> 8; z = z * s >> 8; w = w * s >> 8; h = h * s >> 8; dz = dz * s >> 8; }
    b[0] = (int16_t)x; b[1] = (int16_t)y; b[2] = (int16_t)z; b[3] = (int16_t)w; b[4] = (int16_t)h; b[5] = (int16_t)dz;
    return w || h;
}

/* FUN_1001f1a0: world box {l0, y0, d0, l1, y1, d1}. */
static int world_box(E3World *W, E3Obj *o, int16_t r[6])
{
    int16_t b[6];
    if (!frame_box(W, o, 1, b)) return 0;
    int16_t dep = HI(ODEP(o)), y = HI(OY(o)), lat = HI(OLAT(o));
    if (!(OU(o, 0x54) & 0x10)) { r[2] = dep + b[2]; r[5] = r[2] + b[5]; }
    else { r[5] = dep - b[2]; r[2] = r[5] - b[5]; }
    if (!(OU(o, 0x54) & 0x20)) { r[1] = y + b[1]; r[4] = r[1] + b[4]; }
    else { r[4] = y - b[1]; r[1] = r[4] - b[4]; }
    r[0] = lat + b[0]; r[3] = r[0] + b[3];
    return 1;
}

int e3_model_box(E3World *W, E3Obj *o, int16_t b[6]) { return frame_box(W, o, 1, b); }

int e3_overlap(E3World *W, E3Obj *a, E3Obj *b)     /* FUN_1001eff0 */
{
    int16_t p[6], q[6];
    if (!world_box(W, b, p) || !world_box(W, a, q)) return 0;
    if (OW(a, 0x38) != OW(b, 0x38)) {       /* other path: compare in the player's frame */
        return 0;
    }
    return !(q[3] < p[0] || p[3] < q[0] || q[4] < p[1] || p[4] < q[1] || q[5] < p[2] || p[5] < q[2]);
}

/* FUN_1001b920: outside the active window of its path (behind the player). */
int e3_offscreen(E3World *W, E3Obj *o, int margin)
{
    E3Obj *P = &W->obj[0];
    int d = HI(ODEP(o));
    if (OW(o, 0x38) != OW(P, 0x38)) return 0;
    int start = HI(ODEP(P)) - 256, end = HI(ODEP(P)) + 0x80 * 26;
    return d < start - margin || d > end + 0x100 + margin;
}

void e3_drop(E3World *W, E3Obj *o)     /* FUN_1001eeb0: put an object on the floor below it */
{
    int up, dn = e3_floor(W, HI(ODEP(o)), HI(OY(o)), HI(OLAT(o)), OW(o, 0x38), &up);
    OB(o, 0x25) |= 1;
    if (dn > 0x1ff) {
        if (up < 0x100) { OY(o) -= up << 16; OB(o, 0x25) &= 0xfe; }
        return;
    }
    if (OU(o, 0x54) & 0x10000000) OY(o) += (dn - 0x20) << 16;
    else OY(o) += dn << 16;
    OB(o, 0x25) &= 0xfe;
}

/* ---------------------------------------------------------------- player */

#define PL (&W->obj[0])

static int run_script(E3World *W)    /* FUN_10018370 */
{
    E3Obj *o = PL;
    OS(o, 0x30)--;
    if (OS(o, 0x30) >= 0) return 0;
    for (int guard = 0; guard < 64; guard++) {
        OS(o, 0x32)++;
        uint32_t a = OU(o, 0x64) + 4u * (uint32_t)OS(o, 0x32);
        int16_t v = ds16(W, a);
        uint16_t arg = d16(W, a + 2);
        if (v >= 0) {
            OW(o, 0x34) = v > 0x143 ? 0x32 : (uint16_t)v;
            OS(o, 0x30) = (int16_t)arg;
            return 0;
        }
        switch (v) {
        case -0x28: e3_sound(W, arg, o); break;
        case -9: if (!(W->pad & 0x10)) { OU(o, 0x64) = a + 4; } OS(o, 0x32) = -1; break;
        case -5: if (!(W->pad & 0x40)) { OU(o, 0x64) = a + 4; } OS(o, 0x32) = -1; break;
        case -8: W->gameover = 1; break;
        case -6: OU(o, 0x54) ^= 0x10; break;
        case -4: OB(o, 0x94) = (uint8_t)~OB(o, 0x94); OU(o, 0x54) = (OB(o, 0x94) & 0x10) | (OU(o, 0x54) & ~0x10u); break;
        case -3: return 1;
        case -2: OU(o, 0x64) = a + 4; OS(o, 0x32) = -1; break;
        case -1: OS(o, 0x32) = -1; break;
        default: break;     /* -0xb footstep callbacks, -7 fade */
        }
    }
    return 0;
}

static void set_script(E3World *W, uint32_t s)
{
    OU(PL, 0x64) = s; OS(PL, 0x30) = -1; OS(PL, 0x32) = -1;
    run_script(W);
}

/* ENG3B: FUN_1001e540: standable objects (+0x50 & 0x200, the DAT_10040d20 list) under / around o.
 * *dn = distance down to the nearest box top (0x7fff none), *up = how far o is inside a box (0x7fff none). */
static int floor_obj;   /* GAUNTLET: DAT_1003fbc0, the object obj_floor found (index, 0 none) */
static void obj_floor(E3World *W, E3Obj *o, int *dn, int *up)
{
    *dn = *up = 0x7fff;
    floor_obj = 0;
    int d = HI(ODEP(o)), y = HI(OY(o)), l = HI(OLAT(o));
    for (int i = 1; i < E3_MAXOBJ; i++) {
        E3Obj *b = &W->obj[i];
        /* GAUNTLET: the standable list DAT_10040d20 holds actors with +0x50 & 0x240000 and child-class objects with
         * +0x50 & 0x40000 (FUN_1001b9f0), not every 0x200 object */
        uint32_t sf = (OW(b, 0x2e) & 0x4000) ? 0x40000u : 0x240000u;
        if (!b->used || b == o || !(OU(b, 0x50) & sf) || OW(b, 0x38) != OW(o, 0x38)) continue;
        int16_t r[6];
        if (!world_box(W, b, r)) continue;
        if (getenv("E3_PLATDBG")) printf("plat %x box l %d..%d y %d..%d d %d..%d  hero d %d y %d l %d\n", OW(b, 0x2e), r[0], r[3], r[1], r[4], r[2], r[5], d, y, l);   /* TORMENT: debug */
        if (d < r[2] || d > r[5] || l < r[0] || l > r[3]) continue;
        int dist = r[1] - y;
        if (dist >= 0) { if (dist < *dn) { *dn = dist; floor_obj = i; } }
        else if (-dist < *up && -dist < r[4] - r[1] + 8) { *up = -dist; floor_obj = i; }
    }
}

/* TORMENT: test-pilot floor probe: map floor near the hero's height, or the top of a standable object */
static int pilot_floor(E3World *W, int d, int y, int l, int path)
{
    int u, dn = e3_floor(W, d, y, l, path, &u);
    if ((dn != 0x7fff && dn < 0x60) || (u != 0x7fff && u < 0x40)) return 1;
    for (int i = 1; i < E3_MAXOBJ; i++) {
        E3Obj *b = &W->obj[i];
        int16_t r[6];
        if (!b->used || !(OU(b, 0x50) & 0x200) || OW(b, 0x38) != path || !world_box(W, b, r)) continue;
        if (d >= r[2] && d <= r[5] && l >= r[0] && l <= r[3] && r[1] - y < 0x90 && y - r[1] < 0x90) return 1;
    }
    return 0;
}

/* FUN_1001e8e0: keep to the floor; 0 = nothing under the feet */
static int ground(E3World *W, E3Obj *o)
{
    int dd = abs(HI(ODEP(o) - OI(o, 0x9c))), dl = abs(HI(OLAT(o) - OI(o, 0xa4)));
    int thr = (dd < 4 ? dd + 4 : dd + 2) + dl;
    {   /* GAUNTLET: platform carry link +0xe4 / +0xe8 (FUN_1001e8e0 head, FUN_1001e830, FUN_1001e4c0): keep riding the
         * object Hercules landed on while he is over its depth span; otherwise unlink */
        int k = OI(o, 0xe4);
        if (k > 0 && k < E3_MAXOBJ) {
            E3Obj *b = &W->obj[k]; int16_t r[6];
            int d = HI(ODEP(o));
            if (b->used && world_box(W, b, r) && r[2] <= d && d <= r[5]) { OY(o) = r[1] << 16; return 1; }
            if (b->used && OI(b, 0xe8) == (int32_t)(o - W->obj)) OI(b, 0xe8) = 0;
            OI(o, 0xe4) = 0;
        }
    }
    {   /* ENG3B: objects first (FUN_1001e540 / FUN_1001e440) */
        int pdn, pup;
        obj_floor(W, o, &pdn, &pup);
        if (pdn != 0x7fff || pup != 0x7fff) {
            if (thr < pup) {
                if (pdn <= thr) {
                    OY(o) = (OY(o) & (int32_t)0xffff0000) + (pdn << 16);
                    if (floor_obj) { OI(o, 0xe4) = floor_obj; OI(&W->obj[floor_obj], 0xe8) = (int32_t)(o - W->obj); }   /* GAUNTLET: FUN_1001e440 */
                    return 1;
                }
            }
            else { OY(o) -= pup << 16; return 1; }
        }
    }
    int up, dn = e3_floor(W, HI(ODEP(o)), HI(OY(o)), HI(OLAT(o)), OW(o, 0x38), &up);
    if (dn < 0x7fff || up < 0x7fff) {
        if (up <= thr) { OY(o) = (OY(o) & (int32_t)0xffff0000) - (up << 16); return 1; }
        if (dn <= thr) { OY(o) += dn << 16; return 1; }
    }
    return 0;
}

static int landed(E3World *W, E3Obj *o)     /* FUN_1001eaf0 */
{
    int dy = abs(HI(OY(o) - OI(o, 0xa0))), dd = abs(HI(ODEP(o) - OI(o, 0x9c))), dl = abs(HI(OLAT(o) - OI(o, 0xa4)));
    int up, dn = e3_floor(W, HI(ODEP(o)), HI(OY(o)), HI(OLAT(o)), OW(o, 0x38), &up);
    {   /* ENG3B: landing on standable objects */
        int pdn, pup; obj_floor(W, o, &pdn, &pup);
        if (pdn == 0) return 1;
        if (pup != 0x7fff && pup <= (dl + dd + dy) * 2 + 8) { OY(o) -= pup << 16; return 1; }
    }
    if (dn == 0x7fff && up == 0x7fff) return 0;
    if (dn == 0) return 1;
    int k = W->level == 10 ? 2 : W->level == 8 ? 0x10 : 8;     /* DAT_100431e0 */
    if (up <= (dl + dd + k + dy) * 2) { OY(o) -= up << 16; return 1; }
    return 0;
}

int e3_landed(E3World *W, E3Obj *o) { return landed(W, o); }      /* GAUNTLET: export */
int e3_attr_at(E3World *W, int d, int y, int l, int path) { return e3_attr(W, d, y, l, path); }   /* GAUNTLET: export */

static void rise_snap(E3World *W, E3Obj *o)  /* FUN_1001ea60 */
{
    int dd = abs(HI(ODEP(o) - OI(o, 0x9c)));
    if (dd < 4) dd = 4;
    dd += W->level == 10 ? 2 : W->level == 8 ? 0x10 : 8;
    int up;
    e3_floor(W, HI(ODEP(o)), HI(OY(o)), HI(OLAT(o)), OW(o, 0x38), &up);
    if (up <= dd) OY(o) = (OY(o) & (int32_t)0xffff0000) - (up << 16);
}

static void speed_x(E3World *W)      /* FUN_10019f30 */
{
    E3Obj *o = PL;
    int32_t c = OI(o, 0xa8), t = OI(o, 0xac);
    if (c != t) {
        if (t < c) { c -= OI(o, 0xb4); if (c < t) c = t; }
        else { c += OI(o, 0xb0); if (c > t) c = t; }
        OI(o, 0xa8) = c;
    }
    ODEP(o) += OB(o, 0x94) ? OI(o, 0xf0) - c : c + OI(o, 0xf0);
}

static void steer_in(E3World *W)     /* FUN_10018d00 */
{
    E3Obj *o = PL;
    if (!(W->pad & 0xa0)) { OI(o, 0xc8) = 0; return; }
    if (W->pad & 0x80) {
        OI(o, 0xc8) = OI(o, 0xcc) = 0x40000;
        if (W->lock_l) { OI(o, 0xc8) = OI(o, 0xcc) = 0; W->lock_l = 0; }
    }
    if (W->pad & 0x20) {
        OI(o, 0xc8) = OI(o, 0xcc) = -0x40000;
        if (W->lock_r) { OI(o, 0xc8) = OI(o, 0xcc) = 0; W->lock_r = 0; }
    }
}

static void steer(E3World *W)        /* FUN_1001a070 */
{
    E3Obj *o = PL;
    int32_t c = OI(o, 0xc8), t = OI(o, 0xcc);
    if (c != t) {
        if (t < c) { c -= OI(o, 0xd4); if (c < t) c = t; }
        else { c += OI(o, 0xd0); if (c > t) c = t; }
        OI(o, 0xc8) = c;
    }
    if (!OB(o, 0x96)) OLAT(o) -= c; else OLAT(o) += c;
}

static void clamp_y(E3World *W)      /* FUN_1001a110 */
{
    E3Obj *o = PL;
    if (W->level == 8 && OY(o) > 0x1c00000) { /* Cyclops: only with cheats (flag 7) */ }
    if (W->level == 2 && OY(o) > 0x2000000) OY(o) = 0x2000000;
}

static void die(E3World *W);

static void vertical(E3World *W)     /* FUN_1001a180 */
{
    E3Obj *o = PL;
    if (!OB(o, 0x95)) {
        OI(o, 0xb8) += OI(o, 0xc0);
        if (OI(o, 0xb8) > OI(o, 0xbc)) OI(o, 0xb8) = OI(o, 0xbc);
        OY(o) += OI(o, 0xb8);
        clamp_y(W);
    } else {
        OI(o, 0xb8) -= OI(o, 0xc4);
        if (OI(o, 0xb8) >= 0) { OY(o) -= OI(o, 0xb8); clamp_y(W); return; }
        OI(o, 0xb8) = 0; OB(o, 0x95) = 0;
        OI(o, 0xbc) = 0x100000; OI(o, 0xc0) = 0x18000;
        if (W->state != 6) W->state = 3;
    }
}

static void go_run(E3World *W)       /* FUN_10018690 */
{
    E3Obj *o = PL;
    W->state = 1;
    set_script(W, S_RUN);
    OI(o, 0xf0) = OI(o, 0xf4) = OI(o, 0xfc) = OI(o, 0x100) = 0;
    OI(o, 0xb4) = 0x6000; OI(o, 0xa8) = 0xc0000;
    OI(o, 0xb8) = OI(o, 0xbc) = 0;
    OI(o, 0xc8) = OI(o, 0xcc) = OI(o, 0xd0) = 0;
    W->airborne = 0;
    OI(o, 0xac) = 0xa0000;          /* DAT_10038038 */
    OI(o, 0xb0) = 0x60000;
}

static void go_fall(E3World *W)      /* FUN_10018f70 */
{
    E3Obj *o = PL;
    W->state = 3;
    set_script(W, S_FALL);
    OI(o, 0xfc) = OI(o, 0x100) = 0;
    OI(o, 0xb8) = 0x40000;
    OB(o, 0x95) = 0;
    OI(o, 0xc0) = 0x6000; OI(o, 0xc4) = 0xa000; OI(o, 0xbc) = 0x100000;
    W->airborne = 1;
}

static void go_jump(E3World *W)      /* FUN_10018de0 */
{
    E3Obj *o = PL;
    W->state = 2;
    set_script(W, W->boost ? S_JUMP_BOOST : S_JUMP);
    OB(o, 0x95) = 0xff;
    OI(o, 0xb8) = 0xba000; OI(o, 0xc0) = 0x6000; OI(o, 0xc4) = 0xa000; OI(o, 0xbc) = 0x100000;
    W->airborne = 1;
}

static void go_end_run(E3World *W)   /* FUN_10018bc0 */
{
    E3Obj *o = PL;
    W->state = 10;
    set_script(W, S_RUN_SLOW);
    OI(o, 0xf0) = OI(o, 0xf4) = OI(o, 0xfc) = OI(o, 0x100) = 0;
    OI(o, 0xac) = OI(o, 0xa8) = 0x10000;
    OI(o, 0xb8) = OI(o, 0xbc) = 0;
    OI(o, 0xc8) = OI(o, 0xcc) = OI(o, 0xd0) = 0;
    W->airborne = 0;
}

void e3_win(E3World *W)              /* FUN_10019c20 */
{
    E3Obj *o = PL;
    W->state = 9;
    set_script(W, S_WIN);
    OI(o, 0xac) = OI(o, 0xbc) = OI(o, 0xcc) = 0;
}

static int32_t safe[4], safe_hp;         /* TORMENT test hook E3_RESCUE: last position standing on the map floor */
void e3t_safe(E3World *W)
{
    int up, dn = e3_floor(W, HI(ODEP(PL)), HI(OY(PL)), HI(OLAT(PL)), OW(PL, 0x38), &up);
    int ok = dn == 0;
    for (int k = 32; k <= 96 && ok; k += 32) { int u2, d2 = e3_floor(W, HI(ODEP(PL)) - k, HI(OY(PL)), HI(OLAT(PL)), OW(PL, 0x38), &u2); ok = d2 < 8 || u2 < 8; }
    if (ok) { safe[0] = ODEP(PL) - 0x200000; safe[1] = OY(PL); safe[2] = OLAT(PL); safe[3] = OW(PL, 0x38) + 1; safe_hp = W->health; }
}
static void die(E3World *W)          /* FUN_10019a30 */
{
    if (getenv("E3_RESCUE") && OY(PL) > 0x900000 && safe[3]) {      /* TORMENT test hook: a fall puts Hercules back where he last stood */
        ODEP(PL) = safe[0]; OY(PL) = safe[1]; OLAT(PL) = safe[2]; OW(PL, 0x38) = (uint16_t)safe[3] - 1;
        if (W->health < safe_hp) W->health = safe_hp > 0 ? safe_hp : 1;
        W->state = 1; OI(PL, 0xe0) = 0; OI(PL, 0xb8) = 0; OB(PL, 0x95) = 0;
        printf("RESCUE t=%ld\n", W->ticks);
        return;
    }
    if (getenv("E3_HURTLOG")) printf("DIE t=%ld path %d d=%d l=%d y=%d hp=%d state=%d\n", W->ticks, OW(PL, 0x38), HI(ODEP(PL)), HI(OLAT(PL)), HI(OY(PL)), W->health, W->state);   /* TORMENT: debug */
    E3Obj *o = PL;
    W->boost = 0; W->fb68 = 0; W->shield_state = 2;
    OB(o, 0xec) = 0; OW(o, 0xee) = 0;
    if (W->lives > 0) W->lives--;
    if (W->lives == 0) W->gameover = 1;
    OI(o, 0xac) = OI(o, 0xbc) = OI(o, 0xcc) = 0;
    W->state = 6;
    set_script(W, S_DIE);
}

static int try_jump(E3World *W)      /* FUN_10018dc0 */
{
    if (W->pressed & 0x4000) { go_jump(W); return 1; }
    return 0;
}

/* FUN_10019410: damage per hit kind (difficulty table DAT_100380a0) */
static void damage(E3World *W, int kind)
{
    if (OB(PL, 0xec)) return;           /* shield */
    if (kind != 0x10 && kind != 0x20) e3_sound(W, W->airborne ? 0x602d : 0x602e, PL);
    uint32_t t = 0x100380a0 + (uint32_t)(W->difficulty & 3) * 0x20;
    int A = ds16(W, t), B = ds16(W, t + 4), C = ds16(W, t + 8), D = ds16(W, t + 0xc), E = ds16(W, t + 0x10);
    int dmg = 0;
    switch (kind) {
    case 2: case 4: case 0x22: case 0x23: case 0x24: case 0x25: case 0x27: case 0x34: case 0x35: dmg = A; break;
    case 0x21: case 0x26: case 0x29: case 0x2b: case 0x2c: case 0x2d: case 0x80: dmg = B; break;
    case 0x2a: dmg = B; break;
    case 0x40: dmg = A + B; break;
    case 0x28: case 0x2e: case 0x2f: case 0x30: case 0x31: case 0x32: case 0x33: case 0x36: dmg = C; break;
    case 0x100: dmg = E; break;
    case 0x200: case 0x400: case 0x800: dmg = D; break;
    default: break;
    }
    if (getenv("E3_HURTLOG") && dmg) printf("DAMAGE t=%ld kind %x dmg %d by %x d=%d\n", W->ticks, kind, dmg, OI(PL, 0x110) > 0 ? OW(&W->obj[OI(PL, 0x110) - 1], 0x2e) : 0, HI(ODEP(PL)));   /* TORMENT: debug */
    W->health -= dmg;
}

void e3_hurt(E3World *W, int kind, E3Obj *by)     /* FUN_10019200 */
{
    if (getenv("CYC_HITLOG") && !OC(PL, 0xec) && W->state != 5) printf("hurt t=%ld kind=%#x by=%#x d=%d l=%d hp=%d\n", W->ticks, kind, by ? OW(by, 0x2e) : 0, HI(ODEP(PL)), HI(OLAT(PL)), W->health);   /* CYCLOPS test log */
    E3Obj *o = PL;
    OS(o, 0x116) = (int16_t)kind;
    if (by) OI(o, 0x110) = (int32_t)(by - W->obj) + 1;
    if (getenv("E3_HURTLOG") && W->state != 5) printf("hurt t=%ld kind %x by type %x d=%d hp=%d\n", W->ticks, kind, by ? OW(by, 0x2e) : 0, HI(ODEP(o)), W->health);   /* TORMENT: debug */
    if (W->state == 5) return;
    if (W->boost && (kind == 0x22 || kind == 0x26 || kind == 0x36 || kind == 0x32)) return;
    W->boost = 0; OW(o, 0xee) = 0;
    if (W->state == 6 || W->state == 7) return;
    if (!((ODEP(o) & 0x7f0000) > 0x3fffff || kind != 1)) return;
    if (!OC(o, 0xec)) {
        if (getenv("E3_HITLOG")) printf("hit t=%ld kind=%#x by=%#x path=%d depth=%d lat=%d hp=%d\n", W->ticks, kind, by ? OW(by, 0x2e) : 0,
                                       OW(o, 0x38), HI(ODEP(o)), HI(OLAT(o)), W->health);   /* GAUNTLET: test log */
        if (!getenv("E3_GOD")) {   /* CYCFIX: the god test hook skips only the damage; the knock-back (e.g. off the 0x56 side walls) still happens */
            damage(W, kind);
            if (W->health < 1) { die(W); return; }
        }
        OC(o, 0xec) = 0x40;
    } else if (kind == 4 || kind == 0x30) return;
    if (kind == 0x80) { W->state = 5; set_script(W, S_HIT); return; }
    W->state = 5;
    set_script(W, W->airborne ? S_HIT_AIR : (kind == 0x10 || kind == 0x20) ? S_HIT_SIDE : S_HIT);
}

/* FUN_10017ae0: what the floor attributes under and ahead of Hercules do */
static void attr_effects(E3World *W, int a)
{
    E3Obj *o = PL;
    if (W->level == 8) e3_cyclops_attr(W, a);
    if (W->level == 8) { void cyc_attr_effect(E3World *, int); cyc_attr_effect(W, a); }   /* CYCLOPS: 0x75 pit */
    e3t_attr(W, a);     /* TORMENT: river under the rafts (cases 99 / 0x77) */
    switch (a) {
    case 100: W->slowzone = 1; break;
    case 0x65: W->slowzone = 0; break;
    case 0x76:      /* end of the level: the end object takes over (type 7, FUN_10016240) */
        OI(o, 0xac) = OI(o, 0xa8) = 0x70000;
        if (!OI(o, 0xdc)) {
            E3Obj *e = e3_spawn(W, 7, ODEP(o), OY(o), OLAT(o), OW(o, 0x38));
            if (e) { e3_type_init(W, e); W->endobj = (int)(e - W->obj); }
            OI(o, 0xdc) = 1;
        }
        break;
    default: break;
    }
    int ahead = W->level == 2 ? 0x40 : 0x80, side = W->level == 2 ? 4 : 0x10;
    int b = e3_attr(W, HI(ODEP(o)) + ahead, HI(OY(o)), HI(OLAT(o)) + (OLAT(o) < 0 ? -side : side), OW(o, 0x38));
    if (getenv("E3_ATTRDBG")) printf("side b=%x at d=%d l=%d\n", b, HI(ODEP(o)) + ahead, HI(OLAT(o)) + (OLAT(o) < 0 ? -side : side));   /* TORMENT: debug */
    switch (b) {
    case 0x52: W->lock_r = 1; W->lock_l = 0; OS(o, 0x116) = 0x20; if (W->state != 5) e3_hurt(W, 0x20, NULL); break;
    case 0x55: case 0x5a: if (OLAT(o) > 0) W->lock_r = 1; else W->lock_l = 1; break;
    case 0x56: W->lock_r = 1; W->lock_l = 0; OS(o, 0x116) = 0x10; if (W->state != 5) e3_hurt(W, 0x10, NULL); break;
    case 0x50: W->track_sel = 1; break;     /* ENG3B: camera track switch (FUN_10017ae0) */
    case 0x51: W->track_sel = 0; break;
    case 0x5d: W->lock_r = 1; W->lock_l = 0; break;
    case 0x5e: W->lock_l = 1; W->lock_r = 0; break;
    case 0x7a: case 0x7b: W->lock_r = 1; W->lock_l = 1; break;
    default: break;
    }
    int c = e3_attr(W, HI(ODEP(o)) + 0x60, HI(OY(o)), HI(OLAT(o)), OW(o, 0x38));
    if (c == 0x73 && OY(o) > 0x1c00000 && W->state != 5) e3_hurt(W, 0x80, NULL);
}

/* FUN_10022b30 / FUN_10022620: attribute flag 0x100 = continue on another path */
void e3_kill(E3World *W) { if (W->state != 6) { W->health = 0; die(W); } }                    /* TORMENT */

static void path_change(E3World *W, int a)
{
    E3Obj *o = PL;
    if (a <= 0 || a >= 0x80) return;
    uint16_t f = d16(W, W->attrflags + 2u * (uint32_t)a);
    if (f & 0x8000) return;
    if (!(f & 0x100)) return;
    int np = f & 0xff;
    if (np == OW(o, 0x38) || np >= W->L->e3->npaths) return;
    int pr = W->L->e3->path[np].parent_row;
    /* ENG3B: FUN_10022620 subtracts parent_row*128 for every value, 0xff included (Cyclops paths 1-3) */
    OW(o, 0x38) = (uint16_t)np;
    ODEP(o) -= pr * 0x800000;
    OI(o, 0x9c) -= pr * 0x800000;
    W->cd -= pr * 0x80;                     /* ENG3B: FUN_10022620 moves the camera with the hero */
}

static void st_stand(E3World *W)     /* FUN_10018600 */
{
    if (!ground(W, PL)) { go_fall(W); return; }
    if (W->ending) { go_end_run(W); return; }
    if (W->pad & 0x10) { go_run(W); OB(PL, 0x94) = 0; return; }
    if (W->pad & 0x40) { go_run(W); OB(PL, 0x94) = 0xff; return; }
    run_script(W);
}

static void st_run(E3World *W)       /* FUN_10018950 */
{
    E3Obj *o = PL;
    if (W->ending) { if (W->state != 10) go_end_run(W); return; }
    clamp_y(W);
    if (W->health < 1) { die(W); return; }
    if (OY(o) > 0x2000000) OY(o) = 0x2000000;
    int path = OW(o, 0x38), skipjump = 0;
    if (W->level == 2 && ODEP(o) >= 0x39800000 && (path >= 7 && path <= 10)) skipjump = 1;
    if (W->level == 8 && ODEP(o) >= 0x74800000 && path == 2) skipjump = 1;
    if (W->level == 10 && ODEP(o) >= 0x71000000 && (path == 3 || path == 4)) skipjump = 1;
    if (!skipjump && try_jump(W)) return;
    if (W->cam < 0x4e9) OU(o, 0x64) = OU(o, 0x64) ? OU(o, 0x64) : S_RUN;
    OI(o, 0xac) = 0xa0000;
    if (!W->boost) {
        steer_in(W);
        W->camtarget = 0x140;
        uint32_t want = S_RUN;
        if (W->pad & 0x10) { OI(o, 0xac) = 0x100000; want = S_RUN_FAST; W->camtarget = 0x168; }
        if (W->pad & 0x40) {
            OI(o, 0xac) = -0x40000; want = S_RUN_SLOW; W->camtarget = 0xf0;
            if (W->cam < 0xf1) OI(o, 0xac) = 0x50000;
        }
        if (OU(o, 0x64) != want && (OU(o, 0x64) == S_RUN || OU(o, 0x64) == S_RUN_FAST || OU(o, 0x64) == S_RUN_SLOW)) {
            OU(o, 0x64) = want;     /* keep the frame phase: the three run cycles share frames 50-60 */
        }
        if (W->level == 2 && OY(o) > 0x1e10000) OI(o, 0xac) >>= 1;
        if (W->slowzone) OI(o, 0xac) = 0xa0000 >> 1;
        speed_x(W);
        int l0 = HI(OLAT(o));
        steer(W);
        int l1 = HI(OLAT(o));
        if (!ground(W, o)) go_fall(W);
        if (getenv("E3_ATTRDBG")) printf("run l0=%d l1=%d l2=%d c8=%x cc=%x pad=%x\n", l0, l1, HI(OLAT(o)), OI(o, 0xc8), OI(o, 0xcc), W->pad);   /* TORMENT: debug */
        run_script(W);
        return;
    }
    /* speed power-up (state 8, FUN_10018880) */
    W->state = 8;
    set_script(W, S_JUMP_BOOST);
    OI(o, 0xac) = 0x280000; OI(o, 0xb0) = OI(o, 0xb4) = 0x6000; OI(o, 0xa8) = 0xc0000;
    OI(o, 0xb8) = OI(o, 0xbc) = OI(o, 0xc8) = OI(o, 0xcc) = OI(o, 0xd0) = 0;
}

static void st_boost(E3World *W)     /* FUN_10018880 */
{
    E3Obj *o = PL;
    OI(o, 0xac) = 0x180000; OI(o, 0xb0) = 0x60000;
    clamp_y(W);
    if (OS(o, 0xee) > 0) {
        if (try_jump(W)) return;
        speed_x(W);
        steer(W);
        if (!ground(W, o)) go_fall(W);
        steer_in(W);
        run_script(W);
        if (OU(o, 0x64) == S_JUMP_BOOST && OS(o, 0x32) < 0) set_script(W, S_JUMP_BOOST);
        return;
    }
    W->boost = 0;
    go_run(W);
}

static void st_jump(E3World *W)      /* FUN_10018eb0 */
{
    E3Obj *o = PL;
    OI(o, 0xac) -= 0x100;
    run_script(W);
    steer_in(W);
    if (!W->ending && (W->pad & 0x40)) {
        OI(o, 0xac) = -0x60000; W->camtarget = 0xf0;
        if (W->cam < 0xf1) OI(o, 0xac) = 0x50000;
    }
    speed_x(W);
    steer(W);
    if (!(W->pad & 0x4000)) { OI(o, 0xb8) -= 0x10000; if (OI(o, 0xb8) < 0) OI(o, 0xb8) = 0; }
    vertical(W);
    rise_snap(W, o);
}

static void st_fall(E3World *W)      /* FUN_10019030 */
{
    E3Obj *o = PL;
    if (!W->ending && (W->pad & 0x40)) {
        OI(o, 0xac) = -0x60000; W->camtarget = 0xf0;
        if (W->cam < 0xf1) OI(o, 0xac) = 0x50000;
    }
    steer_in(W);
    speed_x(W);
    steer(W);
    vertical(W);
    if (landed(W, o)) {
        if (W->fb68) e3_sound(W, 0x6037, o);
        W->fb68 = 1;
        go_run(W);
        return;
    }
    run_script(W);
    /* fell out of the level: no floor anywhere below (the original's camera loses him and he dies) */
    int up, dn = e3_floor(W, HI(ODEP(o)), HI(OY(o)), HI(OLAT(o)), OW(o, 0x38), &up);
    if (dn == 0x7fff && up == 0x7fff && OY(o) > OI(o, 0xd8) + 0x2000000) { W->health = 0; die(W); }
}

static void st_hit(E3World *W)       /* FUN_100195d0 */
{
    E3Obj *o = PL;
    ground(W, o);
    int k = OS(o, 0x116);
    E3Obj *by = OI(o, 0x110) > 0 && OI(o, 0x110) <= E3_MAXOBJ ? &W->obj[OI(o, 0x110) - 1] : NULL;
    int bylat = by && by->used ? OI(by, 8) : 0;
    switch (k) {
    case 4: case 0x23: case 0x2e: case 0x33: case 0x40: case 0x24: case 0x30: case 0x32: case 0x28: case 0x2f: case 0x34:
        OI(o, 0xac) = 0x80000; speed_x(W); break;
    case 0x10: OU(o, 0x54) &= ~0x10u; OLAT(o) -= 0x20000; ODEP(o) -= 0x10000; break;
    case 0x20: OU(o, 0x54) |= 0x10; OLAT(o) += 0x20000; ODEP(o) -= 0x10000; break;
    case 0x80:
        if (OLAT(o) >= W->fb78 * 0x10000) { OU(o, 0x54) |= 0x10; OLAT(o) += 0x20000; }
        else { OU(o, 0x54) &= ~0x10u; OLAT(o) -= 0x20000; }
        ODEP(o) -= 0x10000; break;
    case 0x21: case 0x22: case 0x26: case 0x27: case 0x2a: case 0x2c: case 0x400: ODEP(o) -= 0x10000; break;
    case 0x800: ODEP(o) -= 0x10000; break;
    case 0x29: case 0x2b:
        ODEP(o) -= 0x10000;
        OLAT(o) += bylat < 0 ? 0x10000 : -0x10000;
        break;
    case 2: case 8: case 0x25: case 0x31: case 0x36: case 0x100: case 0x200: default:
        ODEP(o) -= 0x10000;
        OLAT(o) += bylat < 0 ? 0x30000 : -0x30000;
        break;
    }
    if (OY(o) > 0x2000000) OY(o) = 0x2000000;
    if (!ground(W, o)) {
        OI(o, 0xb8) = 0x40000; OI(o, 0xbc) = 0x80000; OI(o, 0xc0) = 0x28000; OB(o, 0x95) = 0;
        vertical(W);
    }
    if (run_script(W) == 1) go_run(W);
}

static void st_die(E3World *W)       /* FUN_10019b70 */
{
    vertical(W);
    clamp_y(W);
    if (!ground(W, PL)) { }
    if (run_script(W)) { W->dead_timer = 1; }
}

static void st_win2(E3World *W);
static void st_win(E3World *W)       /* FUN_10019cd0 */
{ if (getenv("E3_WINDBG")) printf("win idx=%d t=%d script=%x frame=%d\n", OS(PL, 0x32), OS(PL, 0x30), OU(PL, 0x64), OW(PL, 0x34)); st_win2(W); }
static void st_win2(E3World *W)
{
    if (run_script(W)) W->done = 1;
}

static void st_end_run(E3World *W)   /* FUN_10018c90 */
{
    E3Obj *o = PL;
    OI(o, 0xac) = W->level == 8 ? 0x50000 : 0x10000;
    speed_x(W);
    if (!ground(W, o)) go_fall(W);
    run_script(W);
}

static void player_place(E3World *W, int cp)
{
    E3Obj *o = PL;
    memset(o, 0, sizeof *o);
    o->used = 1; o->spath = o->sidx = -1;
    uint32_t t = d32(W, 0x10038534 + (uint32_t)W->level * 0x18);
    uint32_t e = t + (uint32_t)cp * 0x18;
    ODEP(o) = d16(W, e) << 16;
    OY(o) = d16(W, e + 2) << 16;
    OLAT(o) = (int32_t)((uint32_t)d8(W, e + 4) << 23);
    OW(o, 0x38) = d8(W, e + 5);
    if (W->level == 2 || W->level == 8) OY(o) = 0x1c00000;
    if (W->level == 8) OLAT(o) = 0x400000;
    if (getenv("E3_PATH")) OW(o, 0x38) = (uint16_t)atoi(getenv("E3_PATH"));
    if (getenv("E3_DEPTH")) ODEP(o) = atoi(getenv("E3_DEPTH")) << 16;
    if (getenv("T2_LAT")) OLAT(o) = atoi(getenv("T2_LAT")) << 16;   /* TORMENT2: test-only start lateral */
    if (OW(o, 0x38) >= W->L->e3->npaths) OW(o, 0x38) = 0;
    OW(o, 0x2e) = 1; OB(o, 0x22) = 1; OW(o, 0x3a) = 0x100; OW(o, 0x4a) = 0x80;
    OW(o, 0xee) = 0;
    W->cam = W->camtarget = 0x140;
    W->state = 0; W->ending = 0; W->boost = 0; W->airborne = 0; W->slowzone = 0;
    W->lock_l = W->lock_r = 0; W->dead_timer = 0; W->endobj = -1;
    W->cyclops = -1;
    if (W->level == 8) {        /* FUN_10003290: the Cyclops starts 0x680 ahead */
        E3Obj *c = e3_spawn(W, 0x29, ODEP(o) + (getenv("CYC_AHEAD") ? atoi(getenv("CYC_AHEAD")) << 16 : 0x6800000), 0x1c60000, 0, OW(o, 0x38));
        if (c) { e3_type_init(W, c); OW(c, 0x2a) = 0x42; e3_set_anim(W, c, 4); W->cyclops = (int)(c - W->obj); }
    }
    W->health = W->maxhealth;
    /* settle on the floor below the start point */
    int up, dn = e3_floor(W, HI(ODEP(o)), HI(OY(o)), HI(OLAT(o)), OW(o, 0x38), &up);
    if (dn < 0x7fff) OY(o) += dn << 16; else if (up < 0x7fff) OY(o) -= up << 16;
    OI(o, 0xd8) = OY(o);
    set_script(W, S_STAND);
    OI(o, 0x9c) = ODEP(o); OI(o, 0xa0) = OY(o); OI(o, 0xa4) = OLAT(o);
}

/* ---------------------------------------------------------------- spawning */

static void load_spawns(E3World *W)  /* FUN_1001c010 */
{
    const Level *L = W->L;
    const Eng3Map *M = L->e3;
    uint32_t P0 = mrel(L, 0x30);
    for (int p = 0; p < M->npaths && p < 16; p++) {
        uint32_t e = P0 + 16u * (uint32_t)p;
        int n = m16(L, e + 0xc);
        uint32_t so = e + 0xe + (uint32_t)(int32_t)ms16(L, e + 0xe);
        W->sp[p] = calloc((size_t)n + 1, sizeof(E3Spawn));
        W->nsp[p] = n;
        for (int k = 0; k < n; k++) {
            uint32_t r = so + 8u * (uint32_t)k;
            uint32_t ta = W->tmpl + 10u * m16(L, r + 6);
            E3Spawn *s = &W->sp[p][k];
            s->d = (m16(L, r) + ds16(W, ta)) * 0x10000;
            s->y = (m16(L, r + 2) + ds16(W, ta + 2)) * 0x10000;
            s->l = (ms16(L, r + 4) + ds16(W, ta + 4)) * 0x10000;
            s->flags = d16(W, ta + 6);
            s->type = d16(W, ta + 8);
        }
    }
}

static void activate(E3World *W)     /* FUN_1001c290 */
{
    E3Obj *P = PL;
    int path = OW(P, 0x38);
    if (path >= 16 || !W->sp[path]) return;
    int rows = W->level == 2 ? 0x18 : W->level == 8 ? 0x10 : 0xe;     /* DAT_1004063b */
    int lo = HI(ODEP(P)) - 0x100, hi = HI(ODEP(P)) + rows * 0x80;
    for (int k = 0; k < W->nsp[path]; k++) {
        E3Spawn *s = &W->sp[path][k];
        if (s->state & 0xc000) continue;
        int always = (s->flags & 0xc800) == 0x800;
        int d = s->d >> 16;
        if (!always && (d <= lo || d >= hi + 0x100)) continue;
        int cls = s->type & 0x7000;
        if (cls == 0x1000) continue;
        E3Obj *o = e3_spawn(W, s->type, s->d, s->y, s->l, path);
        if (!o) return;
        o->spath = path; o->sidx = k;
        OW(o, 0x2a) = 0;                                /* saved state (entry +0xe) */
        o->variant = s->flags & 0xff;                   /* variant byte read by the handlers (entry +0xc) */
        if (s->flags & 0x1000) { OB(o, 0x94) = 0xff; OU(o, 0x54) |= 0x10; }
        OI(o, 0x9c) = s->d; OI(o, 0xa0) = s->y; OI(o, 0xa4) = s->l;
        s->state |= 0x8000;
        if (!e3_type_init(W, o)) { /* type has no handler: keep it as scenery */ }
    }
}

/* ---------------------------------------------------------------- models (FUN_10023ef0) */

static Face *new_face(E3World *W)
{
    if (W->nfaces == W->capfaces) {
        W->capfaces = W->capfaces ? W->capfaces * 2 : 1024;
        W->faces = realloc(W->faces, sizeof(Face) * (size_t)W->capfaces);
    }
    return &W->faces[W->nfaces++];
}

static void obj_world(E3World *W, E3Obj *o, float dl, float dy, float dd, Vec3 *v)
{
    float wx, wz;
    eng3_to_world(W->L, OW(o, 0x38), (float)HI(ODEP(o)) + dd, (float)HI(OLAT(o)) + dl /* TORMENT: map faces are drawn at X = lateral (FUN_10020ed0: col*128 - 0x2c0 - 2vx); no -64 */, &wx, &wz, NULL);
    v->x = wx; v->z = wz; v->y = (float)HI(OY(o)) + dy;
}

static void draw_model(E3World *W, E3Obj *o)
{
    const Level *L = W->L;
    int m = OW(o, 0x34);
    if (!m || m >= W->nmodels) return;
    uint32_t e = W->mdl_tab + 4u * (uint32_t)m, r = e + (uint32_t)(int32_t)m32(L, e);
    if (r + 10 > L->size) return;
    int nb = L->d[r + 2];
    int pi = ms16(L, r);
    uint32_t ph = W->poly_tab + 4u * (uint32_t)pi;
    uint32_t vp = r + 10 + 8u * (uint32_t)nb;
    float s = OW(o, 0x3a) / 256.0f * 2.0f;
    /* rotation: +0x70 pitch, +0x72 yaw (+0x76/2), +0x74 roll, 4096 = full turn after <<2 */
    float ax = OS(o, 0x70) * 4 * (float)(2 * M_PI / 4096.0), ay = (OS(o, 0x72) * 4 + OS(o, 0x76) / 2) * (float)(2 * M_PI / 4096.0),
          az = OS(o, 0x74) * 4 * (float)(2 * M_PI / 4096.0);
    float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay), cz = cosf(az), sz = sinf(az);
    int mirror = (OU(o, 0x54) & 0x10) != 0;
    Vec3 buf[128]; int nbuf = 0;
    Vec3 org; obj_world(W, o, 0, 0, 0, &org);
    for (int chunk = 0; chunk < 16; chunk++) {
        if (vp + 2 > L->size || ph + 4 > L->size) return;
        int nv = ms16(L, vp);
        if (nv < 0 || nv > 64) return;
        Vec3 V[128]; int base = 0;
        for (int q = 0; q < nbuf && q < 64; q++) V[q] = buf[q];
        base = 0;
        for (int q = 0; q < nv && base + q < 128; q++) {
            uint32_t w = m32(L, vp + 2 + 4u * (uint32_t)q);
            float X = (float)(int16_t)(w >> 8), Y = (float)(int8_t)(w >> 24), Z = (float)(int8_t)w;
            float y1 = Y * cx - Z * sx, z1 = Y * sx + Z * cx;          /* pitch about lateral */
            float x2 = X * cy + z1 * sy, z2 = -X * sy + z1 * cy;        /* yaw */
            float x3 = x2 * cz - y1 * sz, y3 = x2 * sz + y1 * cz;       /* roll */
            if (mirror) z2 = -z2;
            obj_world(W, o, x3 * s, y3 * s, z2 * s, &V[base + q]);
        }
        int nf = m16(L, ph), fl = m16(L, ph + 2);
        for (int f = 0; f < nf; f++) {
            uint32_t fa = ph + 4 + 8u * (uint32_t)f;
            uint32_t f0 = m32(L, fa), f1 = m32(L, fa + 4);
            int idx[4] = {(int)(f0 & 63), (int)((f0 >> 6) & 63), (int)((f0 >> 12) & 63), (int)((f0 >> 18) & 63)};
            int lim = base + nv;
            if (idx[0] >= lim || idx[1] >= lim || idx[2] >= lim) continue;
            int quad = (f0 >> 24) & 0x40;
            if (idx[3] >= lim) continue;
            if (!quad) idx[2] = idx[3];
            Face *F = new_face(W);
            /* model faces list corners in order around the quad; the PSX primitive takes them as 0 1 3 2 */
            F->p[0] = V[idx[0]]; F->p[1] = V[idx[1]]; F->p[2] = V[quad ? idx[3] : idx[2]]; F->p[3] = V[idx[2]];
            F->org = org;
            F->flags = (uint8_t)(f0 >> 24) & 0x20;
            F->otb = 0;
            F->mirror = (uint8_t)mirror;
            if (f1 & 0xff000000u) {
                for (int q = 0; q < 4; q++) { F->uv[q][0] = (uint8_t)f1; F->uv[q][1] = 0xff; }
                F->tpage = 0x9c; F->flags |= 0x80;
            } else {
                uint32_t t = W->tex_tab + (f1 & 0xffff);
                uint32_t e0 = m32(L, t), e1 = m32(L, t + 4), e2 = m32(L, t + 8);
                F->uv[0][0] = e0 & 0xff; F->uv[0][1] = (e0 >> 8) & 0xff;
                F->uv[1][0] = e1 & 0xff; F->uv[1][1] = (e1 >> 8) & 0xff;
                F->uv[2][0] = e2 & 0xff; F->uv[2][1] = (e2 >> 8) & 0xff;
                F->uv[3][0] = (e2 >> 16) & 0xff; F->uv[3][1] = (e2 >> 24) & 0xff;
                if (!quad) { F->uv[3][0] = F->uv[2][0]; F->uv[3][1] = F->uv[2][1]; }
                F->tpage = (uint16_t)(e1 >> 16);
            }
        }
        if (!(fl & 0xffff)) return;
        /* next chunk: carried vertices (high byte count, index bytes after the faces) */
        uint32_t pb = ph + 4 + 8u * (uint32_t)nf;
        int carry = (fl >> 8) & 0xff;
        nbuf = 0;
        for (int q = 0; q < carry && q < 64; q++) buf[nbuf++] = V[L->d[pb + q] & 63];
        vp += 4 + 4u * (uint32_t)nv;
        ph = (pb + (uint32_t)carry + 3) & ~3u;
        if (!(fl & 0xff)) return;
    }
}

/* ---------------------------------------------------------------- game interface */

void eng3_game_start(Game *G)
{
    static E3World world;
    E3World *W = &world;
    for (int p = 0; p < 16; p++) { free(W->sp[p]); W->sp[p] = NULL; }
    Face *keep = W->faces; int cap = W->capfaces;
    Bank keepb[10]; memcpy(keepb, W->banks, sizeof keepb);
    scrolls_free(W->scr);       /* AUDSHELL */
    memset(W, 0, sizeof *W);
    W->faces = keep; W->capfaces = cap;
    memcpy(W->banks, keepb, sizeof keepb);
    Wg = W;
    W->dll = &G->eng3dll;
    W->L = &G->L;
    W->level = G->L.index;
    G->R.ofy = 0xe8 - 0x8a;
    if (!G->have_eng3) { fprintf(stderr, "ENG3.DLL missing: ENG3 levels need it next to HERCULES.FS\n"); return; }
    static const uint32_t ENTRY[3][2] = {{2, 0x1003b900}, {8, 0x1003b948}, {10, 0x1003b990}};
    uint32_t ent = 0;
    for (int i = 0; i < 3; i++) if (ENTRY[i][0] == (uint32_t)W->level) ent = ENTRY[i][1];
    if (!ent) return;
    W->tinfo = d32(W, ent + 0x18); W->thand = d32(W, ent + 0x1c);
    W->tinfo4 = d32(W, ent + 0x20); W->thand4 = d32(W, ent + 0x24);
    W->tinfo2 = d32(W, ent + 0x28); W->thand2 = d32(W, ent + 0x2c);
    W->tmpl = d32(W, ent + 0x40); W->attrflags = d32(W, ent + 0x44);
    /* map tables (FUN_1001af10) */
    const Level *L = W->L;
    int nblocks = m16(L, 8), ngrids = m16(L, 0xa);
    uint32_t p = 0x38 + 4u * (uint32_t)ngrids;
    uint32_t b65c = p + 16;
    W->mdl_tab = b65c + 4u * (uint32_t)(nblocks - 1);
    W->nmodels = m16(L, 0xe) + 1;
    W->poly_tab = mrel(L, 0x28);
    W->tex_tab = mrel(L, 0x2c);
    uint32_t n8 = m16(L, 0xe), n10 = m16(L, 0x10), n12 = m16(L, 0x12), n14 = m16(L, 0x14);
    W->shapes = b65c + 4u * (n14 + n12 + n10 + n8 + (uint32_t)nblocks);
    /* sprite banks 1..9 (DAT_10039548 table) */
    for (int b = 1; b < 10; b++) {
        if (W->banks[b].d) continue;
        uint32_t pa = d32(W, 0x10039548 + 0x14u * (uint32_t)b);
        const uint8_t *s = pe_ptr(W->dll, pa, 1);
        if (!s) continue;
        char path[96]; int n = 0;
        while (n < 60 && s[n]) { path[n] = (char)s[n]; n++; }
        path[n] = 0;
        strcat(path, "\\ANIMPSX.BIN");
        bank_load(&W->banks[b], G->fs, path);
    }
    if (!G->e3bank.d) bank_load(&G->e3bank, G->fs, "P:\\SRC3\\HERO\\HERCULES\\ANIMPSX.BIN");
    if (!W->scr) W->scr = scrolls_new(); else scrolls_reset(W->scr);      /* AUDSHELL */
    W->difficulty = G->P.difficulty;
    if (getenv("E3_DIFF")) W->difficulty = atoi(getenv("E3_DIFF"));   /* TORMENT: test hook */
    W->maxhealth = 0x80;
    W->lives = G->P.lives > 0 ? G->P.lives : 3;
    load_spawns(W);
    { void cyc_level_reset(void); cyc_level_reset(); }     /* CYCLOPS */
    if (getenv("E3_SPAWNS")) {   /* ENG3B: list spawn records of one path */
        int pp = atoi(getenv("E3_SPAWNS"));
        for (int k = 0; pp < 16 && k < W->nsp[pp]; k++) printf("spawn path %d #%d type %#x d=%d y=%d l=%d flags=%#x\n", pp, k, W->sp[pp][k].type, W->sp[pp][k].d >> 16, W->sp[pp][k].y >> 16, W->sp[pp][k].l >> 16, W->sp[pp][k].flags);
    }
    if (getenv("T2_PROBE")) {   /* TORMENT2: test-only raw floor probe: T2_PROBE=path, rows of depth, lateral columns */
        int pp = atoi(getenv("T2_PROBE"));
        for (int dd = 24960; dd < 26500; dd += 64) { printf("P%5d|", dd); for (int ll = -384; ll <= 384; ll += 32) { int up, dn = e3_floor(W, dd, 0, ll, pp, &up); printf(" %4d/%-4d", dn == 0x7fff ? -1 : dn, up == 0x7fff ? -1 : up); } printf("\n"); }
    }
    if (getenv("T2_CELLS")) {   /* TORMENT2: test-only raw cell dump of one path: T2_CELLS=path, T2_ROWS=r0 */
        int pp = atoi(getenv("T2_CELLS")), r0 = getenv("T2_ROWS") ? atoi(getenv("T2_ROWS")) : 0;
        const E3Path *P = &G->L.e3->path[pp];
        printf("path %d ngrids %d:", pp, P->ngrids); for (int k = 0; k < P->ngrids; k++) printf(" g%d(y%d %dx%d)", P->g[k].grid, P->g[k].yoff, G->L.e3->grid[P->g[k].grid].w, G->L.e3->grid[P->g[k].grid].h); printf("\n");
        for (int r = r0; r < r0 + 16; r++) for (int k = 0; k < P->ngrids; k++) {
            printf("r%3d g%d|", r, k);
            for (int c = 0; c < G->L.e3->grid[P->g[k].grid].w; c++) { uint32_t cl = eng3_cell(&G->L, P->g[k].grid, r, c); if (cl & 1) printf("    .    "); else printf(" %03x%c%02x%c", (cl >> 1) & 0x3ff, (cl & 0x1000000) ? 'F' : ' ', (cl >> 24) & 0xfe, ' '); }
            printf("|\n");
        }
    }
    if (getenv("E3_SPLIST")) {   /* ENG3FIX: test-only list of spawn records of one type: path, depth, y, lateral */
        int ty = (int)strtol(getenv("E3_SPLIST"), NULL, 0);
        for (int p = 0; p < 16; p++) for (int k = 0; k < W->nsp[p]; k++) if (ty < 0 || ((W->sp[p][k].type & 0x1ff) == ty && !(W->sp[p][k].type & 0x7000)))
            printf("sp type %#x path %d d %d y %d l %d\n", W->sp[p][k].type, p, HI(W->sp[p][k].d), HI(W->sp[p][k].y), HI(W->sp[p][k].l));
    }
    if (getenv("E3_AUDIT")) {   /* ENG3B: per-type spawn audit */
        int cnt[0x200] = {0};
        for (int p = 0; p < 16; p++) for (int k = 0; k < W->nsp[p]; k++) if (!(W->sp[p][k].type & 0x7000)) cnt[W->sp[p][k].type & 0x1ff]++;
        for (int t = 0; t < 0x200; t++) if (cnt[t]) {
            uint32_t ti = W->tinfo + 16u * (uint32_t)t, th = W->thand + 12u * (uint32_t)t;
            printf("type %#x n=%d bank=%d init=%08x upd=%08x upd2=%08x script=%08x anims=%08x\n", t, cnt[t], d8(W, ti + 12),
                   d32(W, th), d32(W, th + 4), d32(W, th + 8), d32(W, ti), d32(W, ti + 4));
        }
    }
    if (getenv("CYC_ATTR")) {   /* CYCLOPS: attribute map of one path, rows a..a+12 */
        int pp = atoi(getenv("CYC_ATTR")), r0 = getenv("CYC_ROW") ? atoi(getenv("CYC_ROW")) : 0;
        for (int r = r0 * 4; r < r0 * 4 + 52; r++) {
            printf("%3d.%02d|", r / 4, (r & 3) * 25);
            for (int l = -0x140; l < 0x140; l += 16) { int a_ = e3_attr(W, r * 32, 0x1c0, l, pp); if (a_) printf("%02x", a_ & 0xff); else printf(" ."); }
            printf("|\n");
        }
        for (int t = 0x29; t < 0x40; t++) printf("hand4 %#x init=%08x upd=%08x ti=%08x bank=%d | hand2 init=%08x upd=%08x ti=%08x bank=%d\n", t, d32(W, W->thand4 + 8u * t), d32(W, W->thand4 + 8u * t + 4), d32(W, W->tinfo4 + 8u * t), d8(W, W->tinfo4 + 8u * t + 6), d32(W, W->thand2 + 8u * t), d32(W, W->thand2 + 8u * t + 4), d32(W, W->tinfo2 + 8u * t), d8(W, W->tinfo2 + 8u * t + 6));
        for (int t = 0x28; t < 0x50; t++) printf("hand %#x init=%08x upd=%08x upd2=%08x bank=%d\n", t, d32(W, W->thand + 12u * t), d32(W, W->thand + 12u * t + 4), d32(W, W->thand + 12u * t + 8), d8(W, W->tinfo + 16u * t + 12));
    }
    player_place(W, 0);
    if (getenv("E3_ATTRMAP")) {     /* TORMENT: attribute map (hex) per path, rows x lateral (step 16) */
        int p = atoi(getenv("E3_ATTRMAP")), r0 = getenv("E3_FLOORROWS") ? atoi(getenv("E3_FLOORROWS")) : 0;
        for (int r = r0 * 4; r < r0 * 4 + 48 && r < G->L.e3->path[p].nrows * 4; r++) {
            printf("%5.2f|", r / 4.0);
            for (int l = -0x180; l < 0x180; l += 16) { int a = e3_attr(W, r * 32 + 16 + 0x40, 0, l, p); if (a) printf("%02x", a & 0xff); else printf(" ."); }
            printf("|\n");
        }
    }
    if (getenv("E3_FLOORMAP")) {    /* ENG3B: floor map per path (rows x lateral), '#' = floor near the start height */
        for (int p = 0; p < G->L.e3->npaths; p++) {
            printf("path %d rows %d parent %d/%d conn", p, G->L.e3->path[p].nrows, G->L.e3->path[p].parent, G->L.e3->path[p].parent_row);
            for (int k = 0; k < G->L.e3->path[p].nconn; k++) printf(" %d", G->L.e3->path[p].conn[k]);
            printf("\n");
            for (int r = 0; r < G->L.e3->path[p].nrows * 4; r += 1) { if (getenv("E3_FLOORROWS") && (r < 4 * atoi(getenv("E3_FLOORROWS")) || r > 4 * atoi(getenv("E3_FLOORROWS")) + 40)) continue; if (!getenv("E3_FLOORROWS") && (r & 7)) continue;
                char line[80]; int n = 0;
                for (int l = -0x280; l <= 0x280 && n < 79; l += 16) {
                    int up, dn = e3_floor(W, r * 32 + 16, 0, l, p, &up);
                    int h = dn != 0x7fff ? dn : up != 0x7fff ? -up : 0x7fff;
                    line[n++] = h == 0x7fff ? ' ' : h < -0x180 ? '^' : h < 0x180 ? '#' : '.';
                }
                line[n] = 0;
                printf("%5.2f|%s|\n", r / 4.0, line);
            }
        }
    }
    G->camx = HI(ODEP(PL)); G->camy = HI(OY(PL)) - 52;
    { void e3_camera_init(E3World *W); e3_camera_init(W); }   /* ENG3B */
    G->E3.done = 0;
}

static void sync_game(Game *G, E3World *W)
{
    E3Obj *o = PL;
    G->E3.path = OW(o, 0x38);
    G->E3.depth = HI(ODEP(o));
    G->E3.X = HI(OLAT(o));   /* TORMENT */
    G->E3.y = HI(OY(o));
    G->E3.done = W->done;
    G->P.health = W->health;
    G->P.lives = W->lives;
    G->E3.coins = W->coins; G->E3.coins2 = W->coins2; G->E3.vases = W->vases;    /* ENG3B */
    G->E3.scrolls = W->letters; G->E3.maxhealth = W->maxhealth;
}

void eng3_game_tick(Game *G, int in)
{
    E3World *W = Wg;
    G->ticks++;
    if (!W || !W->tinfo) return;
    W->ticks++;
    W->sfx_n = 0;
    uint16_t pad = 0;
    if (in & IN_UP) pad |= 0x10;
    if (in & IN_RIGHT) pad |= 0x20;
    if (in & IN_DOWN) pad |= 0x40;
    if (in & IN_LEFT) pad |= 0x80;
    if (in & IN_JUMP) pad |= 0x4000;
    if (getenv("E3_LOGPOS") && W->ticks % atoi(getenv("E3_LOGPOS")) == 0) printf("pos t=%ld p=%d d=%d l=%d y=%d st=%d\n", W->ticks, OW(PL, 0x38), HI(ODEP(PL)), HI(OLAT(PL)), HI(OY(PL)), W->state);   /* ENG3FIX: test-only */
    if (getenv("E3_AUTO") && W->state != 6) {   /* ENG3B test pilot: plays like a careful human */
        E3Obj *h = PL;
        int d = HI(ODEP(h)), l = HI(OLAT(h)), y = HI(OY(h)), path = OW(h, 0x38);
        #define FL(dd, ll) (pilot_floor(W, (dd), y, (ll), path))
        if (getenv("E3_AMAP") && W->ticks == 2) {   /* ENG3FIX: test-only attribute map (hex) at the hero height for E3_PFPATH from E3_AMAP */
            int d0 = atoi(getenv("E3_AMAP")), pp = getenv("E3_PFPATH") ? atoi(getenv("E3_PFPATH")) : path;
            for (int dd = d0; dd < d0 + 0x500; dd += 64) { printf("A%5d|", dd); for (int ll = -0x100; ll <= 0x100; ll += 16) { int a_ = e3_attr(W, dd, y, ll, pp); if (a_) printf("%02x", a_ & 0xff); else printf(" ."); } printf("|\n"); }
        }
        if (getenv("E3_PFMAP") && W->ticks == 2) {   /* ENG3FIX: test-only map of pilot_floor at the hero height, rows d0..d0+0x400 */
            int d0 = atoi(getenv("E3_PFMAP"));
            int path = getenv("E3_PFPATH") ? atoi(getenv("E3_PFPATH")) : OW(h, 0x38);
            int y = getenv("E3_PFY") ? atoi(getenv("E3_PFY")) : HI(OY(h));
            int st_ = getenv("E3_PFSTEP") ? atoi(getenv("E3_PFSTEP")) : 16, l0_ = getenv("E3_PFL") ? atoi(getenv("E3_PFL")) : -0x100;
            for (int dd = d0; dd < d0 + (getenv("E3_PFLEN") ? atoi(getenv("E3_PFLEN")) : 0x400); dd += (st_ < 16 ? st_ : 32)) { printf("%5d|", dd); for (int ll = l0_; ll <= -l0_; ll += st_) putchar(FL(dd, ll) ? '#' : ' '); printf("|\n"); }
        }
        int target = l, jump = 0, slow = 0;
        for (int k = 16; k <= 0x180; k += 16) {
            if (FL(d + k, l)) continue;
            int best = 0x7fff;     /* nearest floor at that depth */
            for (int dl = 8; dl <= 0x200 && best == 0x7fff; dl += 8) {
                if (FL(d + k, l + dl)) best = l + dl + 10;
                else if (FL(d + k, l - dl)) best = l - dl - 10;
            }
            if (W->level == 8 && best != 0x7fff)    /* CYCLOPS pilot: of the floors ahead, take the one nearest the street centre */
                for (int c = -0x100; c <= 0x100; c += 8) if (FL(d + k, c) && FL(d + k, c - 24) && FL(d + k, c + 24) && abs(c) < abs(best)) best = c;
            if (best == 0x7fff) { if (k <= 56) jump = 1; break; }      /* a gap across the whole path */
            target = best; slow = k < 0x100;
            if (k <= 40 && abs(best - l) > 24) jump = 1;                /* can't steer there in time: jump across */
            break;
        }
        static int e3fix_jt = 0x7fff;     /* ENG3FIX: committed landing lateral for a Torment pilot jump */
        if (W->level == 10 && W->state == 1 && !jump) e3fix_jt = 0x7fff;
        if (W->level == 10 && W->state == 1) {   /* ENG3FIX: Torment pilot (test hook only). The zig-zag pieces are offset
                                                  * sideways with a short gap: walk to the near edge of the current piece, then
                                                  * jump for the middle of the next one (not its edge). */
            int k0 = 0;
            for (int k = 8; k <= 0x100; k += 8) if (!FL(d + k, l)) { k0 = k; break; }
            if (k0) {
                int dn = 0, a = 0, b = 0;
                for (int j = 0; j <= 0x80 && !dn; j += 8) {
                    int bestc = 0x7fff;
                    for (int c = -0x100; c <= 0x100; c += 4) if (FL(d + k0 + j, c) && abs(c - l) < abs(bestc - l)) bestc = c;
                    if (bestc != 0x7fff) { dn = d + k0 + j; a = b = bestc; while (a > -0x140 && FL(dn, a - 4)) a -= 4; while (b < 0x140 && FL(dn, b + 4)) b += 4; }
                }
                int ca = l, cb = l;
                while (ca > -0x140 && FL(d + k0 - 8, ca - 4)) ca -= 4;
                while (cb < 0x140 && FL(d + k0 - 8, cb + 4)) cb += 4;
                if (dn && (l < a || l > b) && b - a >= 16 && FL(d + k0 - 8, l)) {
                    int nc = (a + b) / 2, e = nc < ca + 10 ? ca + 10 : nc > cb - 10 ? cb - 10 : nc;
                    if (k0 > 40) { target = e; jump = 0; slow = abs(e - l) > 8; }
                    else { target = nc; jump = 1; slow = 0; e3fix_jt = nc; }
                }
            }
        }
        int fast = 0;
        if (W->level == 2) { e3g_pilot(W, &target, &jump, &slow); if (slow < 0) { slow = 0; fast = 1; } }   /* GAUNTLET: dodge hazards */
        if (W->level == 8 && W->state == 1 && !FL(d + 0x30, l) && !FL(d + 0x18, l)) jump = 1;   /* CYCLOPS pilot: no floor just ahead: jump */
        if (W->level == 8) {    /* CYCLOPS pilot: steer around obstacles on the street (test hook only) */
            static const int HZ[] = {0x2a, 0x2d, 0x30, 0x3d, 0x42, 0x43, 0x48, 0x4029, 0x4030, 0x4032, 0x4033};
            int best_dd = 0x7fff, ol = 0, omg = 0x50;
            for (int i = 1; i < E3_MAXOBJ; i++) {
                E3Obj *z = &W->obj[i];
                if (!z->used) continue;
                int t = OW(z, 0x2e), hz = 0;
                for (size_t k = 0; k < sizeof HZ / sizeof HZ[0]; k++) if (HZ[k] == t) hz = 1;
                if (!hz || OW(z, 0x38) != path) continue;
                int dd = HI(ODEP(z)) - d, dl = HI(OLAT(z)) - target, dy = HI(OY(z)) - y;
                int mg = t == 0x4029 ? 0x98 : 0x50;
                if (t == 0x4029 && !(OS(&W->obj[z->parent > 0 ? z->parent - 1 : 0], 0x30))) mg = 0x40;
                if (dd < -0x20 || dd > 0x1c0 || dl < -mg || dl > mg || dy < -0x160) continue;
                if (t == 0x2a && OW(z, 0x2a) & 8) continue;     /* still up on the roof */
                { int jw = t == 0x2d ? 0x90 : 0x30; if ((t == 0x4033 || t == 0x42 || t == 0x2d) && dd > 0x10 && dd < (t == 0x2d ? 0xc0 : 0xa0) && HI(OLAT(z)) - l > -jw && HI(OLAT(z)) - l < jw && FL(d + 0x100, l) && FL(d + 0x180, l)) jump = 1; }
                if (t != 0x2d && dd < best_dd) { best_dd = dd; ol = HI(OLAT(z)); omg = mg; }   /* carts cross the street: jump them */
            }
            if (best_dd != 0x7fff) {
                int cand[2] = {ol - omg - 0x28, ol + omg + 0x28}, pick = 0x7fff;
                for (int k = 0; k < 2; k++) {
                    int c = cand[(l < ol) ? k : 1 - k], u_;
                    int dn = e3_floor(W, d + 0x60, y, c, path, &u_);
                    if ((dn != 0x7fff && dn < 0x60) || (u_ != 0x7fff && u_ < 0x40)) { pick = c; break; }
                }
                if (pick != 0x7fff) target = pick;
            }
        }
        if (W->level == 10) /* ENG3FIX: Torment-only pilot block */
        {   /* TORMENT: pick up the pickups ahead when the way to them is floor (health keeps the run going) */
            for (int i = 1; i < E3_MAXOBJ; i++) {
                E3Obj *b = &W->obj[i];
                if (!b->used || OW(b, 0x2e) != 6 || OW(b, 0x38) != path) continue;
                int od = HI(ODEP(b)) - d, ol = HI(OLAT(b));
                if (od < 0 || od > 0x180 || abs(HI(OY(b)) - y) > 0x60) continue;
                if (FL(HI(ODEP(b)), ol) && FL(d + 48, (l + ol) / 2) && FL(d + 48, ol)) { target = ol; break; }
            }
        }
        if (W->level == 10) /* ENG3FIX: Torment-only pilot block */
        {   /* TORMENT: dodge hazards ahead (soul splashes, skull piles, wheels, boulders, ghosts) like a player would */
            for (int i = 1; i < E3_MAXOBJ; i++) {
                E3Obj *b = &W->obj[i];
                int t = OW(b, 0x2e);
                if (!b->used || OW(b, 0x38) != path) continue;
                if (!(t == 0x2b || t == 0x2a || t == 0x40 || t == 0x402a || t == 0x402b || t == 0x31 || t == 0x33 || t == 0x30 || t == 0x3e || t == 0x3d || t == 0x36)) continue;
                int od = HI(ODEP(b)) - d, ol = HI(OLAT(b));
                int w = t == 0x2b ? 100 : 80;
                if (od < -16 || od > (t == 0x2b || t == 0x2a ? 0x220 : 0x140) || abs(ol - l) > w) continue;
                int t1 = ol + w + 20, t2 = ol - w - 20;
                int ok1 = FL(d + 32, t1) && FL(d + 32, (l + t1) / 2), ok2 = FL(d + 32, t2) && FL(d + 32, (l + t2) / 2);
                if (ok1 && (!ok2 || l >= ol)) target = t1; else if (ok2) target = t2;     /* go round on the side Hercules is already on */
                slow = 0;
                break;
            }
        }
        if (W->level == 10 && (W->state == 2 || W->state == 3) && e3fix_jt != 0x7fff) { target = e3fix_jt; slow = 0; }   /* ENG3FIX: hold the jump's aim, full speed */
        pad &= ~(0x10 | 0x20 | 0x40 | 0x80 | 0x4000);
        if (fast) pad |= 0x10;
        if (target > l + 2) pad |= 0x20; else if (target < l - 2) pad |= 0x80;     /* right = +lateral */
        if (slow && !(jump && W->level == 8)) pad |= 0x40;
        if (W->state == 0) pad |= 0x10;
        {   /* hop over gaps: tap jump for E3_HOP ticks (a short hop), keep running slowly over stepping stones */
            static int hold; int hop = getenv("E3_HOP") ? atoi(getenv("E3_HOP")) : 99;
            int stones = 0;
            if (!FL(d + 24, l)) for (int k = 32; k <= 0x140; k += 16) if (FL(d + k, l)) { stones = 1; break; }
            if ((jump || stones) && W->state == 1) { pad |= 0x4000; hold = 0; }
            if (W->state == 2 && hold++ < hop) pad |= 0x4000;
            if (stones && W->level != 8) pad |= 0x40;     /* CYCLOPS pilot: keep full speed over the street gaps */
        }
        if (W->level == 10) /* ENG3FIX: Torment-only pilot block */
        {   /* TORMENT: gaps wider than a running jump: sprint (Up) toward the edge so the jump carries onto a raft */
            int gap = 0, far = 0;
            for (int k = 16; k <= 0x300; k += 16) { if (!FL(d + k, l)) { if (!gap) gap = k; } else if (gap) { far = k; break; } }
            int side = 0;
            if (gap) for (int dl = 16; dl <= 0xc0 && !side; dl += 16) side = FL(d + gap + 8, l + dl) || FL(d + gap + 8, l - dl);
            if (gap && e3_attr(W, d + gap + 0x40, y, l, path) == 99) side = 0;     /* the soul river: only the rafts cross it */
            if (gap && e3_attr(W, d + gap + 0x40, y, l, path) == 99) {     /* aim at the raft ahead */
                for (int i = 1; i < E3_MAXOBJ; i++) {
                    E3Obj *b = &W->obj[i]; int16_t r[6];
                    if (!b->used || !(OU(b, 0x50) & 0x200) || OW(b, 0x38) != path || !world_box(W, b, r)) continue;
                    if (r[5] < d || r[2] > d + 0x300) continue;
                    target = (r[0] + r[3]) / 2;
                    pad &= ~(0x20 | 0x80 | 0x40); if (target > l + 2) pad |= 0x20; else if (target < l - 2) pad |= 0x80;
                    break;
                }
            }
            if (gap && !side && far - gap > 0x60 && W->state == 1) { if (far - gap > 0x1a0) pad |= 0x10; pad &= ~0x40; if (gap > 24) pad &= ~0x4000; else pad |= 0x4000; }
            if ((W->state == 2 || W->state == 3) && (pad & 0x10)) pad &= ~0x40;
        }
        static int t2_air = 0x7fff;     /* TORMENT2: landing lateral of a straight river jump in progress */
        if (W->level == 10 && W->state == 1) t2_air = 0x7fff;
        if (W->level == 10 && (W->state == 2 || W->state == 3) && t2_air != 0x7fff) {   /* TORMENT2: full speed and full height */
            pad &= ~(0x10 | 0x20 | 0x40 | 0x80);
            if (t2_air > l + 2) pad |= 0x20; else if (t2_air < l - 2) pad |= 0x80;
            if (W->state == 2) pad |= 0x4000;
        }
        if (W->level == 10 && (W->state == 1 || W->state == 8)) {   /* TORMENT2: test pilot only. Over the soul river with
                * no raft ahead (path 3/4 depth 25088-26368) the stones are crossed by straight running jumps along one side
                * (the opposite stone is only a bonus); with the speed power-up a jump carries much further. Pick a
                * straight-ahead landing piece and jump when the predicted landing falls on it. */
            int spd = abs(HI(ODEP(PL) - OI(PL, 0x9c))); if (spd < 6) spd = 6;
            int land = spd * 26, g = 0;
            for (int k = 8; k <= 0x300; k += 8) if (!FL(d + k, l)) { g = k; break; }
            {   /* only the side stepping-stone stretches: no floor in the middle of the path for the next 0x200 */
                int mid_floor = 0;
                for (int k = g + 8; g && k <= g + 0x200 && !mid_floor; k += 16) for (int c = -64; c <= 64; c += 16) if (FL(d + k, c)) { mid_floor = 1; break; }
                if (mid_floor) g = 0;
            }
            if (g) {     /* a break shorter than two ticks of running is not a gap (pieces meeting at an angle) */
                int k3 = g; while (k3 < g + 0x40 && !FL(d + k3, l)) k3 += 8;
                if (k3 - g <= 24 && FL(d + k3, l)) { g = 0; if (e3_attr(W, d + k3, y, 0, path) == 99) pad &= ~0x4000; }   /* step across, don't jump */
                for (int dl2 = 8; g && dl2 <= 40; dl2 += 8) {     /* floor just beside the line: sidestep, don't jump */
                    int side = FL(d + g + 8, l - dl2) && FL(d + g + 40, l - dl2) ? -dl2 : FL(d + g + 8, l + dl2) && FL(d + g + 40, l + dl2) ? dl2 : 0;
                    if (side) { g = 0; pad &= ~(0x4000 | 0x20 | 0x80); pad |= side > 0 ? 0x20 : 0x80; }
                }
            }
            if (g && (e3_attr(W, d + g + 8, y, l, path) == 99 || e3_attr(W, d + g + 8, y, 0, path) == 99)) {
                int bc = 0x7fff, bk = 0, be = 0, bslow = 0;     /* landing lateral, piece start / end (relative depth), slow jump */
                int lreach = 4 * (g / spd) + 4 * 24;     /* lateral reach: steering before the edge plus in the air */
                for (int c = -0xf0; c <= 0xf0; c += 8) {
                    if (abs(c - l) > lreach) continue;
                    for (int k2 = g + 8; k2 <= g + spd * 40; k2 += 8) {
                        if (!(FL(d + k2, c) && FL(d + k2, c - 16) && FL(d + k2, c + 16))) continue;
                        int e = k2; while (e < k2 + 0x400 && FL(d + e + 8, c)) e += 8;
                        int sl = 0;
                        if (land + spd * 2 > e - 24) {      /* a running jump overshoots this piece: only a slow (Down) jump fits */
                            if (5 * 26 + 10 > e - 24 || W->state == 8) { k2 = e; continue; }
                            sl = 1;
                        }
                        if (bc == 0x7fff || k2 < bk || (k2 == bk && abs(c - l) < abs(bc - l))) { bc = c; bk = k2; be = e; bslow = sl; }
                        break;
                    }
                }
                if (getenv("T2_DBG")) printf("t2 t=%ld d=%d l=%d g=%d spd=%d bc=%d bk=%d lreach=%d\n", W->ticks, d, l, g, spd, bc, bk, lreach);
                if (bc != 0x7fff) {
                    pad &= ~(0x10 | 0x20 | 0x40 | 0x80 | 0x4000);
                    if (bc > l + 2) pad |= 0x20; else if (bc < l - 2) pad |= 0x80;
                    int adj = 0;     /* the next piece starts right beside this one: step across instead of jumping */
                    for (int c = -0xf0; c <= 0xf0 && !adj; c += 8)
                        if (FL(d + g + 8, c) && FL(d + g + 24, c) && abs(c - l) <= 4 * (g / spd) + 16 && FL(d + g - 8, (c + l) / 2)) { adj = 1; bc = c; bslow = 0; bk = g + 8; }
                    if (adj) { pad &= ~(0x20 | 0x80); if (bc > l + 2) pad |= 0x20; else if (bc < l - 2) pad |= 0x80; if (abs(bc - l) > 4 * (g / spd)) pad |= 0x40; }
                    else {
                    if (bslow && spd > 6) pad |= 0x40;     /* slow down for a short landing piece */
                    int mid = (bk + be) / 2;     /* land in the near half of the piece, clear of its ragged edges */
                    if ((land >= bk + 16 && land <= mid && abs(bc - l) <= 4 * 24 && (!bslow || spd <= 6)) || g <= spd + 8) { pad |= 0x4000; e3fix_jt = bc; t2_air = bc; }
                    }
                }
            }
        }
        #undef FL
        if (getenv("E3_AUTODBG")) printf("pilot p=%d t=%ld d=%d l=%d target=%d jump=%d slow=%d pad=%x y=%d st=%d dep=%d\n", path, W->ticks, d, l, target, jump, slow, pad, y, W->state, ODEP(&W->obj[0]) >> 16);
    }
    W->pressed = pad & ~W->pad;
    W->pad = pad;
    E3Obj *o = PL;
    if (W->dead_timer) {
        if (++W->dead_timer > 30) {
            if (W->gameover) { G->P.game_over = 1; W->lives = 3; W->gameover = 0; }
            /* CYCLOPS: clear the objects before player_place, which spawns the Cyclops again */
            for (int i = 1; i < E3_MAXOBJ; i++) if (W->obj[i].used) e3_remove(W, &W->obj[i]);
            { void cyc_level_reset(void); cyc_level_reset(); W->camshake = 0; }
            player_place(W, W->cp);
            { void e3_camera_init(E3World *W); e3_camera_init(W); }   /* CYCLOPS: the restart puts the camera back too */
            for (int p = 0; p < 16; p++) for (int k = 0; k < W->nsp[p]; k++) W->sp[p][k].state &= 0x4000;
        }
        sync_game(G, W);
        return;
    }
    /* FUN_100174e0 */
    if (W->cam <= W->camtarget) W->cam += 4;
    if (W->camtarget <= W->cam) W->cam -= 4;
    if (OC(o, 0xec)) OC(o, 0xec)--;
    int a = e3_attr(W, HI(ODEP(o)), HI(OY(o)), HI(OLAT(o)), OW(o, 0x38));
    path_change(W, a);
    if (getenv("E3_ATTRDBG")) printf("attr t=%ld d=%d l=%d a=%x\n", W->ticks, HI(ODEP(o)), HI(OLAT(o)), a);   /* TORMENT: debug */
    /* FUN_100180e0 */
    OI(o, 0x9c) = ODEP(o); OI(o, 0xa0) = OY(o); OI(o, 0xa4) = OLAT(o);
    if (W->state != 6) attr_effects(W, a);
    if (W->level == 2 && ODEP(o) < 0x3000000) {      /* Gauntlet: keep to the side of the first forks */
        int pth = OW(o, 0x38);
        if ((pth == 2 || pth == 6 || pth == 4) && OLAT(o) < 0x200001) OLAT(o) = 0x200000;
        if ((pth == 1 || pth == 5 || pth == 3) && OLAT(o) > -0x200001) OLAT(o) = -0x200000;
    }
    if (W->level == 10 && ODEP(o) < 0x3000000) {
        if (OW(o, 0x38) == 2 && OLAT(o) < 0x200001) OLAT(o) = 0x200000;
        if (OW(o, 0x38) == 1 && OLAT(o) > -0x200001) OLAT(o) = -0x200000;
    }
    if (OS(o, 0xee) > 0) OS(o, 0xee)--;
    if (W->state == 0 || W->state == 1 || W->state == 2 || W->state == 3) { if (W->boost == 0) OW(o, 0xee) = 0; }
    if (getenv("E3_RESCUE") && W->state == 1) e3t_safe(W);   /* TORMENT test hook */
    switch (W->state) {
    case 0: st_stand(W); break;
    case 1: st_run(W); break;
    case 2: st_jump(W); break;
    case 3: st_fall(W); break;
    case 5: st_hit(W); break;
    case 6: st_die(W); break;
    case 8: st_boost(W); break;
    case 9: st_win(W); break;
    case 10: st_end_run(W); break;
    default: W->state = 1; break;
    }
    if (W->state != 6 && OI(o, 0xd8) > OY(o)) OI(o, 0xd8) = OY(o);
    if (W->state == 1 || W->state == 0) OI(o, 0xd8) = OY(o);
    /* objects */
    activate(W);
    for (int i = 1; i < E3_MAXOBJ; i++) if (W->obj[i].used) e3_type_update(W, &W->obj[i]);
    if (getenv("SCROLL_TEST") && W->ticks == 10) { W->letters |= 5; e3_scroll_trigger(W, atoi(getenv("SCROLL_TEST"))); }   /* AUDSHELL: test hook */
    if (W->scr) { ScrollCfg c = e3_scr_cfg(W); scrolls_tick(W->scr, &c, e3_scr_sound, W); }   /* AUDSHELL */
    /* sounds */
    e3_sfx_world = W;
    for (int i = 0; i < W->sfx_n; i++) {
        int own = W->sfx[i].own;
        if (W->sfx[i].id == 0xffff) { sfx_stop_owner_loops(own); continue; }
        SfxPos pos = {W->sfx[i].sx, 160, 0};
        if (own >= 2) sfx_play_ex((uint16_t)W->sfx[i].id, own, &pos, e3_obj_sfx_pos, (void *)(intptr_t)own);
        else sfx_play((uint16_t)W->sfx[i].id, own, &pos);
    }
    sfx_tick();
    if (e3g_speech >= 0) { sfx_speech(G->fs, e3g_speech); e3g_speech = -1; }     /* GAUNTLET: FUN_1001faf0 (Phil) */
    sync_game(G, W);
    {   /* ENG3B: original camera object */
        void e3_camera_tracks(E3World *W); void e3_camera_follow(E3World *W);
        e3_camera_tracks(W); e3_camera_follow(W);
    }
    G->camx = (float)W->cd; G->camy = (float)W->cy;
    if (getenv("E3_OBJS") && G->ticks % 100 == 0) { void e3_debug_dump(void); e3_debug_dump(); }
    if (getenv("E3_DEBUG") && (G->ticks % 20 == 0 || getenv("E3_DEBUG")[0] == '2'))
        printf("e3 t=%ld state=%d path=%d depth=%d lat=%d y=%d hp=%d lives=%d cam=%d done=%d cd=%d cy=%d cl=%d ofs=%d\n", G->ticks, W->state, OW(o, 0x38),
               HI(ODEP(o)), HI(OLAT(o)), HI(OY(o)), W->health, W->lives, W->cam, W->done, W->cd, W->cy, W->clat, W->camyofs);
}

/* FX2: ENG3 shadow children (FUN_1001d600 spawns them; drawn here from the parent each frame).
 *  0x2002 (FUN_10014270 / FUN_100142b0): a drop-shadow sprite (bank slot 1, frames 0xcd..0xcf) on the floor under
 *     a map-model parent, per parent model; semi (average), brightness 0x50 Gauntlet / 0 Cyclops; scale base - h;
 *     only while the floor is within 0x200.
 *  0x2017 (FUN_100145a0 / FUN_100145d0): the parent sprite laid flat on the floor (sprite mode 5, x rot parent
 *     - 0xff, 0x40 yaw), semi average at brightness 0: a 50 % darkening; scale parent - h/2; floor within 0x100.
 *  0x2018 (FUN_10014820 / FUN_10014850): the same with the parent's brightness (lighting ramps 0x600). The water
 *     reflection branch (DAT_100410e0) is not drawn. */
static void fx2_e3_shadow(Game *G, E3World *W, E3Obj *o, Bank *bk)   /* FX3: bk = Hercules' own bank (NULL: +0x22) */
{
    int type = o->fx2_shadow;
    int d = HI(ODEP(o)), y = HI(OY(o)), l = HI(OLAT(o)), path = OW(o, 0x38), up;
    if (getenv("FX2_LOG")) { int u2, h2 = e3_floor(W, d, y, l, path, &u2); printf("e3shadow type=%x model=%d b22=%d d=%d y=%d l=%d h=%d up=%d\n", type, OW(o, 0x34), OB(o, 0x22), d, y, l, h2, u2); }
    if (type == 0x2002) {
        if (!(OU(o, 0x54) & 0x10000000) || OB(o, 0x22) != 0) return;     /* map-model parents only */
        static const int T2[][4] = {{2, 0xcd, 0x300, 8}, {3, 0xcf, 200, 4}, {6, 0xcf, 0x200, 4}, {9, 0xcd, 0x200, 0x10},
                                    {0xb, 0xce, 0x200, -0x40}, {0xd, 0xcf, 0x200, -8}, {0x12, 0xcd, 0x200, -0x40}};
        static const int T8[][4] = {{1, 0xcd, 0x200, 0x20}, {2, 0xcd, 0x200, 0x20}, {3, 0xcd, 0x200, 0x40},
                                    {0xb, 0xcd, 0x200, 0x20}, {0x21, 0xcd, 0x200, -0x20}};
        const int (*T)[4] = W->level == 2 ? T2 : W->level == 8 ? T8 : NULL;
        int n = W->level == 2 ? 7 : W->level == 8 ? 5 : 0, m = OW(o, 0x34), k = -1;
        for (int i = 0; i < n; i++) if (T[i][0] == m) k = i;
        if (k < 0 || !W->banks[1].d) return;
        int sd = d + T[k][3], sy = y - 8;
        int h = e3_floor(W, sd, sy, l, path, &up);
        if (h == 0x7fff || h > 0x200) return;
        int sc = T[k][2] - h; if (sc <= 0) return;
        const Image *img = bank_frame(&W->banks[1], T[k][1]);
        if (!img) return;
        Vec3 v; obj_world(W, o, 0, (float)(h - 8), (float)T[k][3], &v);
        int br = W->level == 2 ? 0x50 : 0;
        render_sprite_fx(&G->R, img, v.x, v.y, v.z, 0, -3.9f, sc, (uint32_t)br * 0x010101u, 0, 1.0f);
        return;
    }
    int b = OB(o, 0x22);
    if (!bk && (b == 0 || b >= 10 || !W->banks[b].d)) return;
    const Image *img = bank_frame(bk ? bk : &W->banks[b], OW(o, 0x34));
    if (!img || img->oy <= 0) return;
    int h = e3_floor(W, d, y - 2, l, path, &up);
    /* FX3: FUN_10014850 water branch. DAT_100410e0 (FUN_1001c720) = 0x1e1 Gauntlet, 600 Cyclops / Torment. With no
     * floor within 0x100 the 0x2018 child settles on the water surface (water + 0x20); once it is below the water
     * line it becomes the reflection: laid flat towards the camera (x rot parent + 0xff instead of - 0xff), additive
     * (flags54 | 1), at the parent's brightness. Otherwise, in a level with water, the shadow's +0x4a is 0. */
    int water = type == 0x2018 ? (W->level == 2 ? 0x1e1 : (W->level == 8 || W->level == 10) ? 600 : 0) : 0;
    int dy = h;
    if (h == 0x7fff || h > 0x100) {
        if (!water) return;
        dy = water + 0x20 - (y - 2);
        if (dy < 0) dy = 0;
        if (dy >= 0x100) return;
    }
    int refl = water && (y - 2 + dy) > water;
    int sc = OW(o, 0x3a) - (dy >> 1); if (sc <= 0) return;
    if (refl) dy = water - (y - 2);                              /* drawn from the surface */
    float ht = img->oy * (sc / 256.0f) * (refl ? -1.0f : 1.0f);
    Vec3 base, top; obj_world(W, o, 0, (float)(dy - 2), 0, &base); obj_world(W, o, 0, (float)(dy - 2), ht, &top);
    float bsx, bsy, bz, tsx, tsy, tz;
    if (!render_project(&G->R, base.x, base.y, base.z, &bsx, &bsy, &bz)) return;
    if (!render_project(&G->R, top.x, top.y, top.z, &tsx, &tsy, &tz)) return;
    float ysc = (bsy - tsy) / (img->oy * (320.0f / bz) * (sc / 256.0f));
    if (getenv("FX3_LOG")) printf(bk ? "pshadow " : "oshadow type=%x ", bk ? 0 : type), printf(" h=%d dy=%d refl=%d sc=%d ysc=%.3f bsy=%.1f\n", h, dy, refl, sc, ysc, bsy);   /* FX3: test log */
    if (fabsf(ysc) < 0.02f) ysc = ysc < 0 ? -0.02f : 0.02f;
    int br = type == 0x2018 && (!water || refl) ? OW(o, 0x4a) : 0;
    if (br > 0xff) br = 0xff;
    render_sprite_fx(&G->R, img, base.x, base.y, base.z, (OU(o, 0x54) & 0x10) != 0, -3.9f, sc, (uint32_t)br * 0x010101u,
                     refl ? 1 : 0, ysc);   /* FX3: reflection additive, shadow average (flags54 0x684) */
}

void eng3_game_draw(Game *G)
{
    E3World *W = Wg;
    const Level *L = &G->L;
    if (!W || !W->tinfo) return;
    E3Obj *P = PL;
    /* camera: W->cam behind Hercules along the path (FUN_10022070), facing his running direction */
    /* ENG3B: FUN_10022070 view: camera point (W->cd, W->clat) on the camera path, y = cy - 0x74, GTE offset
     * y = DAT_10040620 - 0x8a */
    float cd = (float)W->cd;
    float tx, tz; int h;
    eng3_to_world(L, OW(P, 0x38), cd, (float)W->clat /* TORMENT */, &tx, &tz, &h);
    float a = h * (float)(2 * M_PI / 4096.0);
    float fx = -cosf(a), fz = sinf(a);
    Camera *C = &G->cam;
    static float eye = -1; if (eye < 0) eye = getenv("E3_EYE") ? (float)atof(getenv("E3_EYE")) : 160.0f;   /* DAT_1004061c: the follow error is measured from cd - 0xa0 */
    float shy = 0;
    if (W->shake > 0) { W->shake--; shy = (float)((int)e3_rand(6) - (int)e3_rand(6)); }   /* ENG3B: DAT_100405fc rumble */
    shy += (float)(W->camshake >> 16);      /* CYCLOPS: DAT_100405fc */
    C->x = tx - eye * fx; C->z = tz - eye * fz; C->y = (float)(W->cy - 0x74) + shy;
    G->R.ofy = W->camyofs - 0x8a;
    C->fx = fx; C->fz = fz; C->rx = sinf(a); C->rz = cosf(a);
    C->path_x = 0;
    uint32_t bg = 0xff000000u | (uint32_t)L->bg[0] << 16 | (uint32_t)L->bg[1] << 8 | L->bg[2];
    render_begin(&G->R, L, C, bg);
    render_level(&G->R);
    W->nfaces = 0;
    for (int i = 1; i < E3_MAXOBJ; i++) {
        E3Obj *o = &W->obj[i];
        if (!o->used || !OW(o, 0x34)) continue;
        if (OU(o, 0x54) & 0x20000000) continue;     /* hidden */
        if (o->fx2_shadow) fx2_e3_shadow(G, W, o, NULL);   /* FX2 */
        if (OB(o, 0x22) == 0) {
            int before = W->nfaces;
            Face *old = W->faces;
            draw_model(W, o);
            (void)before; (void)old;
            continue;
        }
        int b = OB(o, 0x22);
        if (b >= 10 || !W->banks[b].d) continue;
        Vec3 v; obj_world(W, o, 0, 0, 0, &v);
        int flip = (OU(o, 0x54) & 0x10) != 0;
        float sc = OW(o, 0x3a) / 256.0f;
        if (OW(o, 0x3e) != 0x100 && OW(o, 0x3e)) sc = OW(o, 0x3e) / 256.0f;     /* Cyclops: z scale 0x7fa */
        {   /* FX2: +0x1c != 0xff draws the frame with that palette of the bank (crowd colour variants) */
            int pal = OB(o, 0x1c);
            if (getenv("FX2_LOG") && pal != 0xff) printf("e3pal type=%x bank=%d frame=%d pal=%d\n", OW(o, 0x2e), b, OW(o, 0x34), pal);
            const Image *img = pal != 0xff ? bank_frame_pal(&W->banks[b], OW(o, 0x34), pal) : bank_frame(&W->banks[b], OW(o, 0x34));
            /* FX3: FUN_10023b00 colour word: level colour (R->light) x +0x4a / 128; flags54 & 4 = semi with
             * abr = flags54 & 3 (trails, souls, ghosts). 0x600 per-cell lighting is not ported (ramp 0 used). */
            int bv = OW(o, 0x4a); if (bv > 0xff) bv &= 0xff;
            uint32_t fl = OU(o, 0x54);
            int md = (fl & 4) ? (int)(fl & 3) : -1;
            if (sc > 0) render_sprite_fx(&G->R, img, v.x, v.y, v.z, flip, -4, (int)(sc * 256.0f + 0.5f), (uint32_t)bv * 0x010101u, md, 1.0f);
        }
    }
    if (W->nfaces) render_faces(&G->R, W->faces, W->nfaces);
    if (W->scr) { ScrollCfg c = e3_scr_cfg(W); scrolls_draw(W->scr, &c, &G->R, (uint32_t)W->letters, e3_scr_bank, W); }   /* AUDSHELL */
    if (!(OC(P, 0xec) && (W->ticks & 2))) {
        if (!getenv("FX3_NOPSHADOW")) { P->fx2_shadow = 0x2018; fx2_e3_shadow(G, W, P, &G->e3bank); }   /* FX3: FUN_100178a0 gives Hercules a 0x2018 child */
        Vec3 v; obj_world(W, P, 0, 0, 0, &v);
        int flip = (OU(P, 0x54) & 0x10) != 0;
        render_sprite(&G->R, bank_frame(&G->e3bank, OW(P, 0x34)), v.x, v.y, v.z, flip, -8);
    }
    render_flush(&G->R);
}
void e3_debug_dump(void)
{
    E3World *W = Wg;
    if (!W) return;
    for (int i = 0; i < E3_MAXOBJ; i++) if (W->obj[i].used) {
        E3Obj *o = &W->obj[i];
        printf("obj %d type %x bank %d frame %d d %d y %d l %d path %d st %x script %x\n", i, OW(o, 0x2e), OB(o, 0x22), OW(o, 0x34),
               HI(ODEP(o)), HI(OY(o)), HI(OLAT(o)), OW(o, 0x38), OW(o, 0x2a), OU(o, 0x64));
    }
}

/* ---- ENG3B: original camera ------------------------------------------------------------------ */

/* FUN_1001b650: camera track for track index t at depth d. out[0]/out[2] = lateral limits, out[1] = height,
 * out[4] = camera height offset (used by FUN_10018350), out[5] = extra. Returns 0 if no track. */
int e3_track(E3World *W, int t, int d, int out[7])
{
    const Level *L = W->L;
    if (!W->track_tab || t < 0 || t >= W->ntrack) return 0;
    if (d < 0) d = 0;
    uint32_t p1 = W->track_tab + 4u * (uint32_t)t;
    uint32_t rec = p1 + (m32(L, p1) & ~3u);
    int n = m16(L, rec);
    uint32_t e0 = rec + 4, e1 = e0;
    int i = 1, d1 = 0;
    if (n > 1) {
        for (;;) {
            e1 = e0 + 16;
            d1 = m16(L, e1);
            if (d < d1) break;
            i++;
            e0 = e1;
            if (i >= n) break;
        }
    }
    int span = d1 - m16(L, e0), f = 0;
    if (span) f = (int)(((int64_t)(d - m16(L, e0)) << 16) / span);
    int A0 = m16(L, e0 + 2), C0 = m16(L, e0 + 6), A1 = m16(L, e1 + 2), C1 = m16(L, e1 + 6);
    int B0 = m16(L, e0 + 4), D0 = m16(L, e0 + 8), B1 = m16(L, e1 + 4), D1 = m16(L, e1 + 8);
    int E0 = (int8_t)L->d[e0 + 10], E1 = (int8_t)L->d[e1 + 10], F0 = (int8_t)L->d[e0 + 12], F1 = (int8_t)L->d[e1 + 12];
    out[0] = (int16_t)((((C0 + A1 - A0 - C1) * f) >> 16) + A0 - C0 - 0x300);
    out[2] = (int16_t)((((C1 + A1 - A0 - C0) * f) >> 16) + C0 + A0 - 0x300);
    out[1] = out[3] = (int16_t)(B0 + D0 + ((((B1 + D1) - B0 - D0) * f) >> 16));
    out[4] = (int16_t)(((E1 - E0) * f >> 16) + E0);
    out[5] = out[6] = (int16_t)(((F1 - F0) * f >> 16) + F0);
    return 1;
}

static int cam_step(E3World *W, uint32_t tab, int err)    /* threshold tables 0x100399f0 / 0x10039a18 / 0x10039a40 */
{
    int a = err < 0 ? -err : err, k = 0;
    while (k < 40 && (int)pe_u16(W->dll, tab + 2u * (uint32_t)k) < a) k++;
    return k;
}

void e3_camera_init(E3World *W)        /* FUN_1001c720 (positions) + track table from FUN_1001af10 */
{
    const Level *L = W->L;
    E3Obj *o = PL;
    W->cd = HI(ODEP(o)); W->cy = HI(OY(o)); W->clat = HI(OLAT(o));
    W->camyofs = W->level == 2 ? -0x100 : W->level == 10 ? 0xc4 : 0xe8;
    W->cam_iter_y = W->level == 10 ? 1 : 2;
    W->camflags = 0x2a;
    uint32_t blocks = L->e3->blocks;
    W->track_tab = blocks + 4u * (uint32_t)(m16(L, 8) + m16(L, 0xe) + m16(L, 0x10) + m16(L, 0x12));
    W->ntrack = L->d[0x14];
    W->track_sel = 0;
}

void e3_camera_tracks(E3World *W)      /* FUN_100174e0: track indices for the camera's path */
{
    int p = OW(PL, 0x38);
    if (W->level == 8) { static const int A[3] = {0, 3, 6}; if (p < 3) { W->track = A[p]; W->track_alt = A[p] + 1; } }
    else if (W->level == 2) { W->track = p > 7 ? 7 : p; W->track_alt = 999; }
    else if (W->level == 10) { static const int A[5] = {4, 0, 1, 2, 3}; if (p < 5) W->track = A[p]; W->track_alt = 999; }
}

void e3_camera_follow(E3World *W)      /* FUN_100180e0 tail (FUN_10018350) + FUN_100222c0 */
{
    E3Obj *o = PL;
    int t = W->track_sel && W->track_alt != 999 ? W->track_alt : W->track;
    int lane[7];
    W->camflags = 0x3e;
    if (e3_track(W, t, HI(ODEP(o)), lane)) W->camyofs = 0xe8 - lane[4];
    int haveclamp = e3_track(W, t, W->cd, lane);
    int hd = HI(ODEP(o)), hy = HI(OY(o)), hl = HI(OLAT(o));
    for (int i = 0; i < 2; i++) {
        int err = ((hd - W->cd) + 0xa0) * 2 - 0x140 - (W->cam >> 1);
        int st = cam_step(W, 0x100399f0u, err);
        W->cd += err < 1 ? -st : st;
    }
    for (int i = 0; i < W->cam_iter_y; i++) {
        int err = -0x40 - 0x168 + ((hy - W->cy) + W->camyofs) * 2;
        int st = cam_step(W, 0x10039a18u, err);
        W->cy += err < 1 ? -st : st;
    }
    int up = 0x7fff;
    e3_floor(W, W->cd - (W->level == 8 ? 0x80 : 0), W->cy - 0x14 + 0x40, W->clat, OW(o, 0x38), &up);
    if (up != 0x7fff) W->cy -= up;
    int tl = hl;
    if (haveclamp) { if (tl < lane[0]) tl = lane[0]; if (tl > lane[2]) tl = lane[2]; }
    {
        int err = (tl - W->clat) * 2;
        int st = cam_step(W, 0x10039a40u, err);
        W->clat += err < 1 ? -st : st;
    }
}

/* CYCLOPS: helpers for e3_cyclops.c */
void e3_die(E3World *W) { die(W); }
