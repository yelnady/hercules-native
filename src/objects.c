#include "objects_int.h"
#include "fx3.h"   /* FX3 */
#include "fx.h"   /* FX */
#include "titans_ai.h"   /* TITANS2 */     /* ENEMIES: shared helpers */
#include "solids.h"          /* SOLIDS */
#include "scrolls.h"         /* AUDSHELL */
#include "sfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ENG1 addresses. */
enum {
    LEVEL_OBJ_TABLE = 0x10068af0,   /* 0x48-byte entries keyed by level number, 999 terminates */
    BANK_TABLE = 0x1005cee4,        /* 0x14 per bank: u16 count at +0, path pointer at -4 */
    H_PICKUP = 0x10012c00,          /* 0x2000-pool pickup init (update 0x10012d80) */
    H_CHECKPOINT = 0x10013990,      /* 0x4000-pool checkpoint init (update 0x100139f0) */
    H_GENERIC1 = 0x1000f430,        /* townsfolk crowd init (update 0x1000f4f0) */
    SCR_CP_IDLE = 0x100533c8, SCR_CP_ACTIVE = 0x10053370,
    TYPE_SPARKLE = 0x2011,
};

static inline uint16_t u16le(const uint8_t *d, uint32_t a) { return (uint16_t)(d[a] | d[a + 1] << 8); }
static inline int16_t s16le(const uint8_t *d, uint32_t a) { return (int16_t)u16le(d, a); }
static inline uint32_t u32le(const uint8_t *d, uint32_t a) { return d[a] | d[a+1] << 8 | d[a+2] << 16 | (uint32_t)d[a+3] << 24; }

static int iabs(int v) { return v < 0 ? -v : v; }
static int rnd(Objects *O, int n)
{
    O->rng = O->rng * 214013u + 2531011u;
    return n > 0 ? (int)((O->rng >> 16) & 0x7fff) % n : 0;
}
static void snd_q(Objects *O, int id, int32_t x, int owner)
{
    if (O->nsounds < 64) { O->sound_x[O->nsounds] = I(x); O->sound_owner[O->nsounds] = owner; O->sounds[O->nsounds++] = id; }
}
/* AUDSHELL: a sound requested at the x of the object being updated belongs to that object (so it is positioned,
 * gated and tracked like FUN_10006da0(id, obj)); anything else is a position-only emission. */
static void snd_at(Objects *O, int id, int32_t x)
{
    snd_q(O, id, x, O->cur && O->cur->x == x ? (int)(O->cur - O->obj) + 2 : 1);
}
static void snd_obj(Objects *O, int id, const Object *o) { snd_q(O, id, o->x, (int)(o - O->obj) + 2); }
void obj_sound_obj(Objects *O, int id, const Object *o) { snd_obj(O, id, o); }       /* FUN_10006da0(id, o) */
void obj_sound_none(Objects *O, int id, int32_t x);
static void snd_none(Objects *O, int id, int32_t x) { snd_q(O, id, x, 0); }       /* FUN_10006da0(id, 0) */
void obj_sound_stop(Objects *O, int id) { if (O->nsounds < 64) { O->sound_x[O->nsounds] = 0; O->sound_owner[O->nsounds] = -3; O->sounds[O->nsounds++] = id; } }   /* FUN_10007f8e(id, 0) */
void obj_sound_none(Objects *O, int id, int32_t x) { snd_none(O, id, x); }
static void snd_stop_owner(Objects *O, const Object *o)                            /* FUN_10007ef6 */
{
    if (O->nsounds < 64) { O->sound_x[O->nsounds] = (int)(o - O->obj) + 2; O->sound_owner[O->nsounds] = -2; O->sounds[O->nsounds++] = 0; }
}
static void snd(Objects *O, int id) { snd_at(O, id, O->P ? O->P->o.x : 0); }

/* ---------------------------------------------------------------- setup */

static int type_info(Objects *O, uint16_t type, uint32_t *script, uint32_t *anims, uint32_t *move, int *bank, uint32_t *init);
int objects_init(Objects *O, const Fs *fs, const PeImage *pe, const Level *L)
{
    memset(O, 0, sizeof *O);
    O->fs = fs; O->pe = pe; O->L = L;
    nessus_reset(O);            /* NESSUS */
    O->scr = scrolls_new();     /* AUDSHELL */
    O->rng = 12345;
    bosses_start(O);            /* BOSSES: (re)start the boss fight on the next tick */
    O->health = O->health_max = 0x80;
    O->lives = 3;
    O->checkpoint = -1;
    enemies3_reset(O);          /* ENEMIES3: Training station table, drop counters */
    leftovers_reset(O);         /* LEFTOVERS */
    audobj_reset();             /* AUDIT: archery fences done */
    uint32_t ent = 0;
    for (uint32_t a = LEVEL_OBJ_TABLE; pe_ptr(pe, a, 0x48); a += 0x48) {
        uint16_t lv = pe_u16(pe, a);
        if (lv == 999) break;
        if (lv == L->index) { ent = a; break; }
    }
    if (!ent) return -1;
    O->ti_actor = pe_u32(pe, ent + 0x18); O->h_actor = pe_u32(pe, ent + 0x1c);
    O->ti_4000 = pe_u32(pe, ent + 0x20);  O->h_4000 = pe_u32(pe, ent + 0x24);
    O->ti_2000 = pe_u32(pe, ent + 0x28);  O->h_2000 = pe_u32(pe, ent + 0x2c);
    O->tmpl = pe_u32(pe, ent + 0x40);
    if (!pe_ptr(pe, O->tmpl, 10)) { O->nsp = 0; return 0; }   /* BOSSES: Medusa/Vortex have no template table; the level start code spawns everything */

    /* Layer 0 spawn list (FUN_10025d20 / the block at 0x10025xxx that builds DAT_1006fee8). */
    const uint8_t *d = L->d;
    uint32_t lp = 0x30 + (u32le(d, 0x30) & ~3u);
    int n = u16le(d, lp + 8);
    uint32_t off = lp + 10 + (uint32_t)(int32_t)s16le(d, lp + 10);
    if (off + 8u * (uint32_t)n > L->size) return -1;
    O->sp = calloc((size_t)n + 1, sizeof(SpawnRec));
    for (int k = 0; k < n; k++) {
        uint32_t r = off + 8u * (uint32_t)k;
        uint32_t t = O->tmpl + 10u * u16le(d, r + 6);
        if (!pe_ptr(pe, t, 10)) continue;
        SpawnRec *s = &O->sp[O->nsp++];
        s->x = FX(u16le(d, r) + pe_s16(pe, t));
        s->y = FX(u16le(d, r + 2) + pe_s16(pe, t + 2));
        s->z = FX(u16le(d, r + 4) + pe_s16(pe, t + 4));
        s->flags = pe_u16(pe, t + 6);
        s->type = pe_u16(pe, t + 8);
        if (s->type == 0x2018) O->coins18_total++;     /* SOLIDS: gamestruct +0x1a */
        if (s->type == 0x2019) O->coins14_total++;     /* SOLIDS: gamestruct +0x16 */
    }
    if (getenv("OBJ_DUMP")) {   /* SOLIDS: debug listing of spawn records */
        for (int k = 0; k < O->nsp; k++) {
            SpawnRec *s = &O->sp[k];
            int cls = s->type & 0x7000, i = s->type & 0x1ff;
            uint32_t ti = cls == 0 ? O->ti_actor + 16u * i : cls == 0x4000 ? O->ti_4000 + 8u * i : cls == 0x2000 ? O->ti_2000 + 8u * i : 0;
            uint32_t h = cls == 0 ? O->h_actor + 12u * i : cls == 0x4000 ? O->h_4000 + 8u * i : cls == 0x2000 ? O->h_2000 + 8u * i : 0;
            int bank = ti ? (cls == 0 ? pe_ptr(pe, ti + 12, 1)[0] : pe_ptr(pe, ti + 6, 1)[0]) : -1;
            printf("SP %3d type %04x flags %04x pos %d,%d,%d bank %d init %08x upd %08x upd2 %08x\n", k, s->type, s->flags,
                   I(s->x), I(s->y), I(s->z), bank, h ? pe_u32(pe, h) : 0, h ? pe_u32(pe, h + 4) : 0, (h && cls == 0) ? pe_u32(pe, h + 8) : 0);
        }
    }
    if (getenv("SCRIPT_SCAN")) {
        for (int cls = 0x2000; cls <= 0x4000; cls += 0x2000) for (int t = 0; t < 0x80; t++) {   /* word scripts: ops the word interpreter ignores */
            uint32_t sc, an, mv, in; int bk;
            if (!type_info(O, (uint16_t)(cls | t), &sc, &an, &mv, &bk, &in) || sc < 0x10000000 || !pe_ptr(O->pe, sc, 2)) continue;
            for (int w = 0; w < 400 && pe_ptr(O->pe, sc + 2u * (uint32_t)w, 4); w++) {
                int v = pe_s16(O->pe, sc + 2u * (uint32_t)w);
                if (v == -1 || v == -6 || v == -7 || v == -8) break;
                if (v == -0x1f || v == -0x11 || v == -9) printf("SCANW type %04x init %08x op %d arg %x\n", cls | t, in, v, (uint16_t)pe_s16(O->pe, sc + 2u * (uint32_t)w + 2));
            }
        }
                                /* DOLLS: which actor scripts use ops the generic interpreter acts on */
        for (int t = 0; t < 0x200; t++) {
            uint32_t sc, an, mv, in; int bk;
            if (!type_info(O, (uint16_t)t, &sc, &an, &mv, &bk, &in) || !an) continue;
            for (int a = 0; a < 64; a++) {
                uint32_t sp = pe_u32(O->pe, an + 4u * (uint32_t)a);
                if (sp < 0x10000000 || !pe_ptr(O->pe, sp, 4)) continue;
                for (int w = 0; w < 300 && pe_ptr(O->pe, sp + 4u * (uint32_t)w, 4); w++) {
                    int v = pe_s16(O->pe, sp + 4u * (uint32_t)w), g = pe_s16(O->pe, sp + 4u * (uint32_t)w + 2);
                    if (v == -1 || v == -4 || v == -6 || v == -7 || v == -0xc) break;
                    if (v == -0x1f || v == -5 || v == -0x2b || v == -0x2c || v == -0x2d || v == -0x20 || v == -0x2f || v == -0x11)
                        printf("SCAN type %03x init %08x anim %d op %d arg %x\n", t, in, a, v, (uint16_t)g);
                }
            }
        }
    }
    return 0;
}

void objects_free(Objects *O)
{
    for (int i = 0; i < OBJ_BANKS; i++) if (O->banks[i].d) bank_free(&O->banks[i]);
    free(O->sp);
    scrolls_free(O->scr);       /* AUDSHELL */
    memset(O, 0, sizeof *O);
}

static Bank *get_bank(Objects *O, int b)
{
    if (b <= 0 || b >= OBJ_BANKS) return NULL;
    if (!O->bank_tried[b]) {
        O->bank_tried[b] = 1;
        uint32_t p = pe_u32(O->pe, BANK_TABLE + 0x14 * (uint32_t)b - 4);
        const uint8_t *s = p ? pe_ptr(O->pe, p, 1) : NULL;
        if (s) {
            char path[160];
            snprintf(path, sizeof path, "%.120s\\ANIMPSX.BIN", (const char *)s);
            if (bank_load(&O->banks[b], O->fs, path)) memset(&O->banks[b], 0, sizeof(Bank));
        }
    }
    return O->banks[b].d ? &O->banks[b] : NULL;
}

/* Collision box k (1-based) of a sprite frame (FUN_10030e40): x0, y0, z0, x1, y1, z1 relative to the hotspot. */
static int frame_box(Objects *O, int bank, int frame, int k, int out[6])
{
    Bank *b = get_bank(O, bank);
    if (!b || frame < 1 || frame > b->count) return 0;
    uint32_t e = u32le(b->d, 4 + 4 * (uint32_t)frame);
    if (!e) return 0;
    uint32_t f = (e >> 8) + 4;
    if (f + 2 > b->size) return 0;
    int nhit = (int8_t)b->d[f + 1];
    if (k < 1) k = 1;
    if (nhit < k) return 0;
    uint32_t p = f + 2 + 6u * (uint32_t)(k - 1);
    if (p + 6 > b->size) return 0;
    const int8_t *q = (const int8_t *)b->d + p;
    if (!q[0] && !q[1] && !q[2] && !q[3] && !q[4] && !q[5]) return 0;
    out[0] = q[0]; out[1] = q[1]; out[2] = q[2] >> 1;
    out[3] = q[3]; out[4] = q[4]; out[5] = q[5] >> 1;
    return 1;
}

/* FUN_1002a880: world box of an object, mirrored when drawn flipped. */
static int world_box(Objects *O, int bank, int frame, int mirror, int32_t x, int32_t y, int32_t z, int box[6])
{
    int b[6];
    if (!frame_box(O, bank, frame, 1, b)) return 0;
    int X = I(x), Y = I(y), Z = I(z);
    if (!mirror) { box[0] = X + b[0]; box[3] = X + b[3]; }
    else { box[3] = X - b[0]; box[0] = X - b[3]; }
    box[1] = Y + b[1]; box[4] = Y + b[4];
    box[2] = Z + b[2]; box[5] = Z + b[5];
    return 1;
}

static int boxes_overlap(const int a[6], const int b[6])
{
    return a[0] <= b[3] && b[0] <= a[3] && a[1] <= b[4] && b[1] <= a[4] && a[2] <= b[5] && b[2] <= a[5];
}

/* ---------------------------------------------------------------- scripts */

static void set_anim(Objects *O, Object *o, int a)        /* FUN_100292f0 */
{
    o->anim = (int16_t)a;
    o->script = pe_u32(O->pe, o->anims + 4u * (uint32_t)a);
    o->idx = -1;
    o->timer = 0;
}

static void next_anim(Objects *O, Object *o)              /* FUN_10029270 */
{
    if (!o->anims) return;
    o->anim++;
    for (int guard = 0; guard < 64; guard++) {
        uint32_t s = pe_u32(O->pe, o->anims + 4u * (uint32_t)o->anim);
        if (s) { o->script = s; return; }
        o->anim = (int16_t)pe_u16(O->pe, o->anims + 4u * (uint32_t)(o->anim + 1));
    }
}

static void kill_obj(Objects *O, Object *o, int gone)
{
    solids_kill(O, o, gone);    /* SOLIDS: children and parent links */
    if (o->spawn >= 0 && o->spawn < O->nsp) O->sp[o->spawn].state = gone ? 2 : 0;
    if (O->nsounds < 64) { O->sound_x[O->nsounds] = (int)(o - O->obj) + 2; O->sound_owner[O->nsounds] = -4; O->sounds[O->nsounds++] = 0; }   /* AUDSHELL: a looping sound dies with its owner */
    o->used = 0;
}

static void callback(Objects *O, Object *o, int id);
static Object *spawn_type(Objects *O, uint16_t type, int32_t x, int32_t y, int32_t z);

static void script_op_log(const Object *o, int op, int arg)
{
    if (getenv("OP_LOG")) printf("scriptop obj %04x op %d arg %x\n", o->type, op, (uint16_t)arg);
}

/* FUN_100283e0: actor scripts, 4-byte {s16 op, s16 arg} entries. Returns 1 when the script loops. */
static int run_pairs(Objects *O, Object *o)
{
    const PeImage *pe = O->pe;
    int ret = 0;
    if (!o->script) return 0;
    if (--o->timer >= 0) return 0;
    int16_t v = 0;
    for (int guard = 0; guard < 256; guard++) {
        o->idx++;
        uint32_t a = o->script + 4u * (uint32_t)o->idx;
        v = pe_s16(pe, a);
        int16_t arg = pe_s16(pe, a + 2);
        if (v >= 0) break;
        switch (v) {
        case -0x30: o->flags54 &= ~0x800000u; break;
        case -0x29: o->v2a = (uint16_t)arg; break;
        case -0x28: snd_obj(O, (uint16_t)arg, o); break;
        case -0x27: snd_none(O, (uint16_t)arg, o->x); break;
        case -0x2e: snd_stop_owner(O, o); break;
        case -0x11: o->s98 = (int16_t)(arg & 0xff); break;                              /* AUDIT: +0x98 */
        case -0x2d: o->v[8] = (int32_t)arg << 8; script_op_log(o, v, arg); break;      /* DOLLS: +0xc8 / +0xb8 / +0xa8 */
        case -0x2c: o->v[4] = (int32_t)arg << 8; script_op_log(o, v, arg); break;
        case -0x2b: o->v[0] = (int32_t)arg << 8; script_op_log(o, v, arg); break;
        case -0x20: o->cool = arg; script_op_log(o, v, arg); break;                     /* +0x4c */
        case -0x1f: case -0x05: {                                                       /* DOLLS: FUN_100300c0 / FUN_10030180: spawn a child */
            script_op_log(o, v, arg);
            Object *c = spawn_type(O, (uint16_t)arg, o->x, o->y, o->z);
            if (c && v == -0x05) { c->facing = o->facing; c->zface = o->zface; c->b95 = o->b95; }
            break;
        }
        case -0x1e: o->flags50 &= ~(uint32_t)(int32_t)arg; break;
        case -0x1d: o->flags50 |= (uint32_t)(int32_t)arg; break;
        case -0x1c: o->z += FX(arg); break;
        case -0x1b: o->y += FX(arg); break;
        case -0x1a: o->x += o->facing ? -FX(arg) : FX(arg); break;
        case -0x19:
            if (o->spawn >= 0) { o->x = O->sp[o->spawn].x; o->y = O->sp[o->spawn].y; o->z = O->sp[o->spawn].z; }
            break;
        case -0x18: o->s36 = (int16_t)arg; break;           /* +0x36: per-type meaning (ENEMIES3: stored) */
        case -0x17: o->flags54 &= ~((uint32_t)(int32_t)arg << 16); break;
        case -0x16: o->flags54 &= ~(uint32_t)(int32_t)arg; break;
        case -0x15: o->flags54 |= (uint32_t)(int32_t)arg << 16; break;
        case -0x14: o->flags54 |= (uint32_t)(int32_t)arg; break;
        case -0x12: o->bank = (uint8_t)arg; break;
        case -0x10: o->movei = arg; break;
        case -0x0f: o->moving = (int16_t)-o->moving; break;
        case -0x0e: if (o->moving < 0) o->moving = (int16_t)-o->moving; break;
        case -0x0d: if (o->moving > 0) o->moving = (int16_t)-o->moving; break;
        case -0x0c: set_anim(O, o, arg); ret = 1; break;
        case -0x0b: callback(O, o, arg); break;
        case -0x0a: callback(O, o, o->cbid); break;
        case -0x07: kill_obj(O, o, 1); return 1;                                         /* AUDIT: FUN_10027410 marks the record gone, then despawns */
        case -0x06: kill_obj(O, o, 0); return 1;
        case -0x08: if (o->spawn >= 0 && o->spawn < O->nsp) O->sp[o->spawn].state = 2; break;  /* ENEMIES: FUN_10027450 */
        case -0x04: next_anim(O, o); o->idx = -1; ret = 1; break;
        case -0x01: o->idx = -1; ret = 1; break;
        case -0x03: o->flags54 ^= 0x10; break;
        case -0x02: o->facing = (uint8_t)~o->facing; break;
        default: break;   /* callbacks, child objects, sounds-by-object, velocities: handled per type */
        }
        if (!o->script) return ret;
    }
    o->frame = v;
    o->timer = pe_s16(pe, o->script + 2 + 4u * (uint32_t)o->idx);
    return ret;
}

/* FUN_10028820: pickup/effect scripts, s16 words; word 0 is the default frame time. */
static int run_words(Objects *O, Object *o)
{
    const PeImage *pe = O->pe;
    int ret = 0;
    if (!o->script) return 0;
    if (--o->timer >= 0) return 0;
    int16_t v = 0;
    for (int guard = 0; guard < 256; guard++) {
        o->idx++;
        uint32_t a = o->script + 2u * (uint32_t)o->idx;
        v = pe_s16(pe, a);
        if (v >= 0) break;
        int16_t arg = pe_s16(pe, a + 2);
        switch (v) {
        case -0x29: o->idx++; o->v2a = (uint8_t)arg; break;
        case -0x28: o->idx++; snd_obj(O, (uint16_t)arg, o); break;
        case -0x27: o->idx++; snd_none(O, (uint16_t)arg, o->x); break;
        case -0x2e: snd_stop_owner(O, o); break;
        case -0x1f: o->idx++; script_op_log(o, v, arg); spawn_type(O, (uint16_t)arg, o->x, o->y, o->z); break;   /* DOLLS: spawn a child object (FUN_10028820 / FUN_100300c0) */
        case -0x12: o->idx++; o->bank = (uint8_t)arg; break;
        case -0x11: o->idx++; o->s98 = (int16_t)(arg & 0xff); break;
        case -0x10: o->idx++; o->movei = arg; break;
        case -0x0f: o->moving = (int16_t)-o->moving; break;
        case -0x0e: if (o->moving < 0) o->moving = (int16_t)-o->moving; break;
        case -0x0d: if (o->moving > 0) o->moving = (int16_t)-o->moving; break;
        case -0x0a: callback(O, o, o->cbid); break;                                       /* AUDIT: was ignored in word scripts */
        case -0x09: o->idx++; o->timer = arg; break;
        case -0x08: if (o->spawn >= 0 && o->spawn < O->nsp) O->sp[o->spawn].state = 2; break;   /* AUDIT: FUN_10027450 only marks the record gone */
        case -0x07: kill_obj(O, o, 1); return 1;
        case -0x06: kill_obj(O, o, 0); return 1;
        case -0x03: o->flags54 ^= 0x10; break;
        case -0x02: o->facing = (uint8_t)~o->facing; break;
        case -0x01: o->idx = 0; ret = 1; break;
        default: break;
        }
    }
    o->frame = v;
    if (o->timer < 0) o->timer = pe_s16(pe, o->script);
    return ret;
}

/* ---------------------------------------------------------------- movement */

/* FUN_100291d0: step along the movement table. */
static void move_table(Objects *O, Object *o)
{
    if (o->moving < 0 || !o->movetab) return;
    uint32_t e = o->movetab + 6u * (uint32_t)o->movei;
    if (!pe_ptr(O->pe, e, 6)) return;
    int dx = pe_s16(O->pe, e), dy = pe_s16(O->pe, e + 2), dz = pe_s16(O->pe, e + 4);
    o->x += o->facing ? -FX(dx) : FX(dx);
    o->y += FX(dy);
    o->z += o->zface ? FX(dz) : -FX(dz);
}

/* FUN_1002a290: drop onto the floor below. */
static void drop_to_floor(Objects *O, Object *o)
{
    int up, down;
    level_probe(O->L, I(o->x), I(o->y), I(o->z), &up, &down, NULL);
    o->airborne = 1;
    if (down < 0x140) { o->y += FX(down); o->airborne = 0; }
    else if (up < 0x100) { o->y -= FX(up); o->airborne = 0; }
}

/* FUN_10029c20 for objects. */
static int ground_follow(Objects *O, Object *o)
{
    int dx = iabs(I(o->x - o->px));
    int tol = (dx < 4 ? dx + 4 : dx + 2) + iabs(I(o->z - o->pz));
    int up, down;
    level_probe(O->L, I(o->x), I(o->y), I(o->z), &up, &down, NULL);
    if (up <= tol) { o->y = (o->y & (int32_t)0xffff0000) - FX(up); return 1; }
    if (down <= tol) { o->y += FX(down); return 1; }
    return 0;
}

static uint16_t attr_flags(Objects *O, int x, int y, int z)
{
    int a = level_attr(O->L, x, y, z);
    return (a > 0 && a < 128) ? O->L->flags[a] : 0;
}

/* ---------------------------------------------------------------- spawning */

static Object *alloc_obj(Objects *O)
{
    for (int i = 0; i < OBJ_MAX; i++) if (!O->obj[i].used) {
        memset(&O->obj[i], 0, sizeof(Object));
        O->obj[i].used = 1;
        O->obj[i].spawn = -1;
        O->obj[i].attr = -1;
        O->obj[i].scale = 0x100;    /* SOLIDS */
        return &O->obj[i];
    }
    return NULL;
}

/* Type info (script, anims, movement table, bank) and init handler for a type. */
static int type_info(Objects *O, uint16_t type, uint32_t *script, uint32_t *anims, uint32_t *move, int *bank, uint32_t *init)
{
    const PeImage *pe = O->pe;
    int cls = type & 0x7000, i = type & 0x1ff;
    *anims = *move = 0;
    if (cls == 0) {
        uint32_t t = O->ti_actor + 0x10u * (uint32_t)i;
        if (!pe_ptr(pe, t, 16)) return 0;
        *script = pe_u32(pe, t); *anims = pe_u32(pe, t + 4); *move = pe_u32(pe, t + 8); *bank = pe_ptr(pe, t + 12, 1)[0];
        *init = pe_u32(pe, O->h_actor + 0xcu * (uint32_t)i);
    } else if (cls == 0x4000 || cls == 0x2000) {
        uint32_t ti = cls == 0x4000 ? O->ti_4000 : O->ti_2000, h = cls == 0x4000 ? O->h_4000 : O->h_2000;
        uint32_t t = ti + 8u * (uint32_t)i;
        if (!pe_ptr(pe, t, 8)) return 0;
        *script = pe_u32(pe, t); *bank = pe_ptr(pe, t + 6, 1)[0];
        *init = pe_u32(pe, h + 8u * (uint32_t)i);
    } else return 0;
    return 1;
}

static const char *g_spawn_path = "";
static Object *spawn_type(Objects *O, uint16_t type, int32_t x, int32_t y, int32_t z)
{
    uint32_t script, anims, move, init; int bank;
    g_spawn_path = "";
    if (!type_info(O, type, &script, &anims, &move, &bank, &init)) return NULL;
    Object *o = alloc_obj(O);
    if (!o) return NULL;
    o->type = type;
    o->x = o->px = x; o->y = o->py = y; o->z = o->pz = z;
    o->bank = (uint8_t)bank;
    o->script = script; o->anims = anims; o->movetab = move;
    o->pairs = (type & 0x7000) == 0;
    o->list = move;             /* SOLIDS: +0x118 child list / per-type table */
    if (O->have_pending) o->v2a = (uint16_t)O->pending_v2a;   /* SOLIDS */
    o->init_fn = init;          /* SOLIDS */
    if (getenv("FX_INIT")) printf("spawn %04x init %08x bank %d\n", type, init, bank);   /* FX: debug */
    o->idx = 0; o->timer = 0;
    if ((type & 0x7000) == 0x2000) o->idx = 0;
    if (init == H_PICKUP) {
        o->kind = K_PICKUP;
        o->idx = (int16_t)((I(x) + I(y)) >> 1 & 7);         /* FUN_10012c00: animation phase from position */
        if ((type == 0x2012 && (O->P ? O->P->lives : O->lives) >= 9) || (type == 0x2002 && O->health_max == 0xff)) {   /* FUN_10012c00: nothing to gain -> health */
            uint32_t s2, a2, m2, i2; int b2;
            if (type_info(O, 0x2013, &s2, &a2, &m2, &b2, &i2)) { o->type = 0x2013; o->script = s2; }
        }
        if (type == 0x200b) {                                /* SOLIDS: story scroll (FUN_10012c00) */
            o->v2a = O->spawn_flags & 0x3f;
            o->script = pe_u32(O->pe, 0x100537e0 + 4u * o->v2a);   /* PTR_DAT_100537e0[id] */
            o->idx = 0; o->timer = 0;
            if (!o->script) o->kind = K_NONE;
        }
    } else if (init == H_CHECKPOINT) {
        o->kind = K_CHECKPOINT;
        o->script = ((type + 6) & 0xf) == O->checkpoint ? SCR_CP_ACTIVE : SCR_CP_IDLE;
        drop_to_floor(O, o);
    } else if (init == H_GENERIC1) {                         /* FUN_1000f430 */
        o->kind = K_GENERIC1;
        o->cbid = 3; o->count126 = 4;
        o->zface = 0xff;
        o->anim = 0;
        next_anim(O, o);
        o->movei = 0; o->moving = 1;                         /* FUN_100292c0 */
        drop_to_floor(O, o);
        o->facing = rnd(O, 2) ? 0xff : 0;
    } else if ((type & 0x7000) == 0 && titans_ai_init(O, o, init)) {   /* TITANS2: level-9 actors */
        if (o->used) o->kind = K_TITAN;
    } else if (solids_init(O, o, init, 0)) {                    /* SOLIDS: models, groups, platforms */
        o->kind = K_SOLID;
    } else if ((type & 0x7000) == 0 && enemy_init(O, o, init)) {    /* ENEMIES: ported enemy AI */
        o->kind = K_ENEMY;
    } else if ((type & 0x7000) != 0 && leftovers_init(O, o, init)) {   /* LEFTOVERS */
        o->kind = K_ENEMY;
    } else if ((type & 0x7000) != 0 && (enemies3_init(O, o, init) || enemies4_init(O, o, init))) {   /* ENEMIES3 */
        o->kind = K_ENEMY;
    } else if ((type & 0x7000) != 0 && enemies2_init(O, o, init)) {  /* ENEMIES2: 0x2000 / 0x4000-pool behaviours */
        o->kind = K_ENEMY;
    } else if ((type & 0x7000) == 0 && (bank || script || anims)) {
        /* Actors without a ported handler: play their animation list in place (townsfolk, idle enemies). */
        o->kind = K_ACTOR; g_spawn_path = "GENERIC_ACTOR";
        if (!o->script && anims) { o->anim = 0; o->script = pe_u32(O->pe, anims); if (!o->script) next_anim(O, o); }
        o->moving = -1;
        drop_to_floor(O, o);
    } else if ((type & 0x7000) == 0x2000 && script) {
        o->kind = K_EFFECT; g_spawn_path = "GENERIC_EFFECT";
    } else {
        o->kind = K_NONE; g_spawn_path = "NONE";                                    /* invisible trigger or unported logic */
    }
    if (o->kind == K_NONE) { o->used = 0; return NULL; }
    return o;
}

static void spawn_from(Objects *O, int k)
{
    SpawnRec *s = &O->sp[k];
    if ((s->type & 0x7000) == 0x1000) return;
    if (getenv("SPAWN_LOG")) printf("spawnrec %04x at %d,%d,%d\n", s->type, I(s->x), I(s->y), I(s->z));
    O->spawn_flags = s->flags;  /* ENEMIES */
    Object *o = spawn_type(O, s->type, s->x, s->y, s->z);
    O->spawn_flags = 0;
    if (!o) { s->state = 2; return; }
    o->spawn = k;
    o->facing = (s->flags & 0x1000) ? 0xff : o->facing;       /* FUN_100270c0 */
    if (o->kind != K_GENERIC1) o->flags54 = (o->flags54 & ~0x10u) | (o->facing & 0x10);
    s->state = 1;
}

/* FUN_10026480 / FUN_100265xx: activate spawn records near the camera. */
static void scroll_spawn(Objects *O, int camx)
{
    for (int k = 0; k < O->nsp; k++) {
        SpawnRec *s = &O->sp[k];
        if (s->state) continue;
        int range = (s->flags & 0x40) ? 500 + 160 : 0x100 + 160;
        if (iabs(I(s->x) - camx) < range) spawn_from(O, k);
    }
}

/* ---------------------------------------------------------------- per-type logic */

static void player_box(Objects *O, const Player *P, int box[6])
{
    const Obj *p = &P->o;
    if (!world_box(O, p->bank, p->frame, (p->flags54 & 0x10) != 0, p->x, p->y, p->z, box)) {
        box[0] = I(p->x) - 14; box[3] = I(p->x) + 14;
        box[1] = I(p->y) - 72; box[4] = I(p->y);
        box[2] = I(p->z) - 16; box[5] = I(p->z) + 16;
    }
}

static int touch_player(Objects *O, Object *o, const int pbox[6], int pad)
{
    int b[6];
    if (!world_box(O, o->bank, o->frame, (o->flags54 & 0x10) != 0, o->x, o->y, o->z, b)) {
        b[0] = I(o->x) - 12; b[3] = I(o->x) + 12; b[1] = I(o->y) - 24; b[4] = I(o->y); b[2] = I(o->z) - 16; b[5] = I(o->z) + 16;
    }
    b[0] -= pad; b[3] += pad;
    return boxes_overlap(b, pbox);
}

static void become_effect(Objects *O, Object *o, uint16_t type)    /* FUN_10029320 */
{
    uint32_t script, anims, move, init; int bank;
    if (!type_info(O, type, &script, &anims, &move, &bank, &init) || !script) { kill_obj(O, o, 1); return; }
    if (o->spawn >= 0) O->sp[o->spawn].state = 2;
    o->spawn = -1;
    o->type = type; o->kind = K_EFFECT; o->script = script; o->bank = (uint8_t)bank;
    o->pairs = 0; o->idx = 0; o->timer = 0; o->life = 60;
}

/* FUN_10012ef0: what a pickup gives. */
static void collect(Objects *O, Object *o)
{
    if (o->type == 0x200b) {    /* SOLIDS: FUN_10012ef0 - story scroll */
        O->scroll_bits |= (uint16_t)(1u << (o->v2a & 15));
        O->scroll_last = o->v2a; O->scroll_show = 1;
        obj_scrolls_trigger(O, o->v2a);     /* AUDSHELL: FUN_10012620 */
        snd(O, 0xc03c);
        return;
    }
    obj_collect(O, o->type);
}

void obj_collect(Objects *O, uint16_t type)    /* BOSSES: exported */
{
    Player *P = O->P;       /* PLAYER: FUN_10012ef0 changes the gamestruct the player reads (health +0x24, max +0x1e, lives +0xc, ammo +0x26..+0x2c) */
    if (P && P->dying) return;
    if (getenv("LV_LOG") && P) printf("LV_LOG collect %04x (ammo %d %d %d, lives %d, health %d)\n", type, P->ammo[1], P->ammo[2], P->ammo[3], P->lives, P->health);   /* LV_TITANS */
    switch (type) {
    case 0x2002: O->health_max += 0x20; if (O->health_max > 0xff) O->health_max = 0xff; if (P) P->max_health = O->health_max; break;
    case 0x2012: if (++O->lives > 9) O->lives = 9; if (P && ++P->lives > 9) P->lives = 9; snd(O, 0xc03d); break;
    case 0x2013:
        O->health += 0x20; if (O->health > O->health_max) O->health = O->health_max;
        if (P) {
            P->health += 0x20; if (P->health > P->max_health) P->health = P->max_health;
            if (P->heal_cd == 0) { snd(O, 0xc03b); P->heal_cd = 0x32; } else snd(O, 0xc03f);
        } else snd(O, 0xc03b);
        break;
    case 0x2014: case 0x2015: case 0x2016: case 0x2017: {          /* Gifts of the Gods: +0x80 charges, max 0xff */
        static const int W[4] = {WPN_INVULNERABLE, WPN_FIREBALL, WPN_LIGHTNING, WPN_SONIC};
        if (P) { int *a = &P->ammo[W[type - 0x2014]]; *a += 0x80; if (*a > 0xff) *a = 0xff; }
        snd(O, 0xc03e);
        break;
    }
    case 0x2018: if (O->coins18 < 1000) O->coins18++; snd(O, 0xc03f); break;
    case 0x2019: if (O->coins14 < 1000) O->coins14++; snd(O, 0xc03f); break;
    case 0x201a: O->letters |= 1; snd(O, 0xc03e); break;
    case 0x201b: O->letters |= 2; snd(O, 0xc03e); break;
    case 0x201c: O->letters |= 4; snd(O, 0xc03e); break;
    case 0x201d: O->letters |= 8; snd(O, 0xc03e); break;
    default: snd(O, 0xc03e); break;
    }
}

static int lv_cnt;   /* LV_TITANS */
static void upd_pickup(Objects *O, Object *o, const int pbox[6])   /* FUN_10012d80 */
{
    if (getenv("LV_LOG") && O->L->index == 9) {   /* LV_TITANS */
        int b[6]; int ok = world_box(O, o->bank, o->frame, (o->flags54 & 0x10) != 0, o->x, o->y, o->z, b);
        if (abs(I(o->x) - pbox[0]) < 150 && (lv_cnt++ % 10) == 0) printf("LV_LOG pickup %04x box(%d) x %d..%d y %d..%d z %d..%d  peg x %d..%d y %d..%d z %d..%d\n", o->type, ok, b[0], b[3], b[1], b[4], b[2], b[5], pbox[0], pbox[3], pbox[1], pbox[4], pbox[2], pbox[5]);
    }
    if (touch_player(O, o, pbox, 0)) {
        collect(O, o);
        become_effect(O, o, TYPE_SPARKLE);
        o->y -= FX(8);
        return;
    }
    run_words(O, o);
}

static void upd_checkpoint(Objects *O, Object *o, const int pbox[6])  /* FUN_100139f0 */
{
    int id = (o->type + 6) & 0xf;
    if (o->script == SCR_CP_IDLE && O->checkpoint != id && touch_player(O, o, pbox, 0)) {
        snd(O, 0x28);
        o->script = SCR_CP_ACTIVE; o->idx = 0; o->timer = 0;
        O->checkpoint = id;
        O->have_cp = 1; O->cp_x = o->x; O->cp_y = o->y; O->cp_z = o->z;
    } else if (o->script == SCR_CP_ACTIVE && O->checkpoint != id) {
        o->script = SCR_CP_IDLE; o->idx = 0; o->timer = 0;
    } else if (O->checkpoint == id && o->script != SCR_CP_IDLE && touch_player(O, o, pbox, 0)) {
        if (o->inv == 0) snd(O, 0x28);              /* touching an active checkpoint again chimes too (not in the original) */
        o->inv = 30;
    }
    if (o->inv && !touch_player(O, o, pbox, 0)) o->inv--;       /* re-arm shortly after stepping off */
    run_words(O, o);
}

/* FUN_1000f4f0: Thebes/Training crowds wander, turn at walls and walk between depth planes. */
static void upd_generic1(Objects *O, Object *o, int camx)
{
    if (!o->airborne) {
        run_pairs(O, o);
        if (!o->used) return;
        move_table(O, o);
        int32_t z = o->z;
        int zp = I(z + 0x400000) & 0xf80;
        switch (o->v2a) {
        case 1: case 0xb:
            if (z < o->ztarget) {
                if (!(attr_flags(O, I(o->x), I(o->y), zp) & 0x20)) {
                    o->z = o->ztarget; o->zface = (uint8_t)~o->zface; set_anim(O, o, 0x15); o->v2a = 0;
                } else o->ztarget -= 0x800000;
            }
            break;
        case 2: case 0xc:
            if (o->ztarget < z) {
                if (!(attr_flags(O, I(o->x), I(o->y), zp) & 0x10)) {
                    o->z = o->ztarget; o->zface = (uint8_t)~o->zface; set_anim(O, o, 0xf); o->v2a = 0;
                } else o->ztarget += 0x800000;
            }
            break;
        case 3: break;
        default: {
            int ax = I(o->x) + (o->facing ? -0x30 : 0x18);
            if (attr_flags(O, ax, I(o->y) - 0x10, zp) & 3) set_anim(O, o, 9);
            else if (attr_flags(O, I(o->x), I(o->y), zp) & 0xc0) set_anim(O, o, 0x1b);
        }
        }
    } else drop_to_floor(O, o);
    if (!ground_follow(O, o)) drop_to_floor(O, o);
    o->flags54 = (o->facing & 0x10) | (o->flags54 & ~0x10u);
    if (o->y > 0x4f00000 || iabs(I(o->x) - camx) > 0x500) kill_obj(O, o, 0);
}

static void upd_actor(Objects *O, Object *o)
{
    run_pairs(O, o);
}

/* Script callbacks (level table +0x34, DAT_10071384). */
static void callback(Objects *O, Object *o, int id)
{
    if (o->kind == K_ENEMY) { enemy_callback(O, o, id); return; }   /* ENEMIES */
    if (o->kind == K_BOSS) { boss_callback(O, o, id); return; }     /* BOSSES */
    if (id != 3 || o->kind != K_GENERIC1) return;
    /* FUN_1000f770: crowd AI - head home if too far, use doorways, otherwise pick a random action. */
    if (o->v2a != 5) o->v2a = 0;
    if (o->spawn >= 0 && iabs(I(o->x - O->sp[o->spawn].x)) > 0x800) { set_anim(O, o, 9); return; }
    int a = level_attr(O->L, I(o->x), I(o->y), I(o->z + 0x400000) & 0xf80);
    if (a > 0 && a < 128) {
        uint16_t f = O->L->flags[a];
        if (!o->facing) {
            if (f & 0x10) { o->ztarget = (o->z & 0x7f800000) + 0x800000; set_anim(O, o, 0xc); o->v2a = 2; return; }
        } else if (f & 0x20) {
            set_anim(O, o, 0x12); o->v2a = 1; o->ztarget = (o->z & 0x7f800000) - 0x800000; return;
        }
    }
    if (o->count126 == 0) {
        o->count126 = (int16_t)(rnd(O, 4) + 4);
        if (o->v2a != 5 && o->v2a != 6) {
            static const int pick[4] = {0x1f, 0x20, 0x21, 0x1b};
            int r = rnd(O, 12);
            set_anim(O, o, r < 4 ? pick[r] : 0x26);
        } else set_anim(O, o, 0x28);
        return;
    }
    o->count126--;
}

/* ---------------------------------------------------------------- frame */

static long dbg_tick;
void objects_tick(Objects *O, Player *P, float camx)
{
    ++dbg_tick;
    O->shake_n = 0; O->shake_sum = 0; O->cam_dy = 0;   /* LEFTOVERS: DAT_10071680 rebuilt every tick */
    if (!O->cam_bias_set) O->cam_bias = 0;      /* LEFTOVERS: DAT_10070d04 only while a 0x202d zone holds it */
    O->cam_bias_set = 0;
    if (getenv("ENEMY_LOG") && dbg_tick % atoi(getenv("ENEMY_LOG")) == 0)   /* ENEMIES: debug */
        for (int i = 0; i < OBJ_MAX; i++) {
            const Object *o = &O->obj[i];
            if (o->used && (o->kind == K_ENEMY || o->ai))
                printf("  [%ld] enemy %04x ai %d pos %d,%d,%d anim %d v2a %d frame %d hp %d inv %d face %d\n", dbg_tick, o->type, o->ai,
                       I(o->x), I(o->y), I(o->z), o->anim, o->v2a, o->frame, o->hp, o->inv, o->facing);
        }
    O->nsounds = 0;
    if (O->health_snd > 0) O->health_snd--;     /* AUDSHELL: FUN_10015780 */
    int cx = (int)camx;
    O->P = P; O->camx = cx;     /* ENEMIES */
    if (dbg_tick == 1 && getenv("ENEMY_TELEPORT")) {   /* ENEMIES: test hook "x,y,z" */
        int tx, ty, tz;
        if (sscanf(getenv("ENEMY_TELEPORT"), "%d,%d,%d", &tx, &ty, &tz) == 3) {
            P->o.x = P->o.px = FX(tx); P->o.y = P->o.py = FX(ty); P->o.z = P->o.pz = FX(tz);
        }
    }
    enemies_player_spawns(O);   /* ENEMIES: weapon effects the player's scripts asked for */
    enemies2_frame(O);          /* ENEMIES2: level per-frame hooks (mini-boss bars, Training lessons) */
    nessus_frame(O);            /* NESSUS */
    enemies3_frame(O); enemies4_frame(O);   /* ENEMIES3 */
    leftovers_frame(O);                     /* LEFTOVERS */
    solids_pre(O, P);           /* SOLIDS: track the charged-punch meter before objects react */
    P->obj_floor = objects_floor; P->obj_ctx = O;   /* SOLIDS */
    bosses_level_tick(O);       /* BOSSES: level start / per-frame boss hooks */
    scroll_spawn(O, cx);
    if (dbg_tick == 1 && getenv("SPAWN_AUDIT")) {   /* AUDIT: spawn every record once and report what the port made of it */
        for (int k = 0; k < O->nsp; k++) {
            SpawnRec *s = &O->sp[k];
            if ((s->type & 0x7000) == 0x1000) { printf("AUD %04x trigger-class (never spawned, flags %04x)\n", s->type, s->flags); continue; }
            Object *o = NULL; uint32_t sc, an, mv, in; int bk;
            int ok = type_info(O, s->type, &sc, &an, &mv, &bk, &in);
            if (s->state == 0) { O->spawn_flags = s->flags; o = spawn_type(O, s->type, s->x, s->y, s->z); O->spawn_flags = 0; }
            else for (int i = 0; i < OBJ_MAX; i++) if (O->obj[i].used && O->obj[i].spawn == k) o = &O->obj[i];
            printf("AUD %04x at %d,%d,%d flags %04x init %08x bank %d script %08x kind %d%s\n", s->type, I(s->x), I(s->y), I(s->z), s->flags, ok ? in : 0, ok ? bk : -1, ok ? sc : 0, o ? o->kind : -1, o ? "" : " NONE");
            if (o && s->state == 0) { o->spawn = -1; kill_obj(O, o, 0); }
        }
    }
    if (dbg_tick == 1 && getenv("SND_AUDIT")) {   /* AUDIT: sound ids in object scripts that the level's sound table lacks */
        for (int cls = 0; cls <= 0x4000; cls += 0x2000) for (int t = 0; t < 0x60; t++) {
            uint32_t sc, an, mv, in; int bk;
            uint16_t ty = (uint16_t)(cls | t);
            if (!type_info(O, ty, &sc, &an, &mv, &bk, &in) || in < 0x10001000 || in > 0x10050000) continue;
            uint32_t list[65]; int nl = 0;
            if (sc >= 0x10000000 && pe_ptr(O->pe, sc, 4)) list[nl++] = sc;
            if (cls == 0 && an) for (int a = 0; a < 64; a++) { uint32_t q = pe_u32(O->pe, an + 4u * (uint32_t)a); if (q >= 0x10000000 && q < 0x10080000 && pe_ptr(O->pe, q, 4)) list[nl++] = q; }
            for (int k = 0; k < nl; k++) {
                for (int w = 0; w < 300; w++) {
                    int pair = cls == 0;
                    uint32_t a = list[k] + (pair ? 4u : 2u) * (uint32_t)w;
                    if (!pe_ptr(O->pe, a, 4)) break;
                    int v = pe_s16(O->pe, a);
                    if (v < -0x31) break;
                    if (v == -1 || v == -4 || v == -6 || v == -7) break;
                    if (v == -0x28 || v == -0x27) {
                        int id = (uint16_t)pe_s16(O->pe, a + 2);
                        if (!sfx_has((uint16_t)id)) printf("SNDMISS type %04x id %04x (op %d)\n", ty, id, v);
                        if (!pair) w++;
                    } else if (!pair && (v == -0x29 || v == -0x1f || v == -0x12 || v == -0x11 || v == -0x10 || v == -9)) w++;
                }
            }
        }
    }
    if (dbg_tick == 1 && getenv("SPAWN_AUDIT2")) {   /* AUDIT: every type index of every pool */
        for (int cls = 0; cls <= 0x4000; cls += 0x2000) for (int t = 0; t < 0x60; t++) {
            uint32_t sc, an, mv, in; int bk;
            uint16_t ty = (uint16_t)(cls | t);
            if (!type_info(O, ty, &sc, &an, &mv, &bk, &in) || in < 0x10001000 || in > 0x10050000) continue;
            Object *o = spawn_type(O, ty, FX(100), FX(100), FX(0));
            printf("AUD2 %04x init %08x bank %d script %08x anims %08x path %s kind %d ai %d\n", ty, in, bk, sc, an, g_spawn_path, o ? o->kind : -1, o ? o->ai : 0);
            if (o) { o->spawn = -1; kill_obj(O, o, 0); }
        }
    }
    int pbox[6];
    player_box(O, P, pbox);
    if (O->L->index == 9 && titans_ai_ctx()->have_pbody)   /* TITANS3: Pegasus' body box (+0x88) on level 9 */
        memcpy(pbox, titans_ai_ctx()->pbody, sizeof pbox);
    /* LEVELS: end-of-level object 0x200a (pickup-pool update FUN_10013880): touching it starts the victory
     * pose (FUN_10019240). Tested straight against the map record so it works without a sprite. */
    for (int k = 0; k < O->nsp && !O->level_end; k++) {
        const SpawnRec *s = &O->sp[k];
        if (s->type != 0x200a) continue;
        int b[6] = {I(s->x) - 32, I(s->y) - 64, I(s->z) - 64, I(s->x) + 32, I(s->y) + 128, I(s->z) + 64};
        if (boxes_overlap(b, pbox)) { O->level_end = 1; snd(O, 0x4012); }
    }
    for (int i = 0; i < OBJ_MAX; i++) {
        Object *o = &O->obj[i];
        if (!o->used) continue;
        o->px = o->x; o->py = o->y; o->pz = o->z;
        if (o->inv) o->inv--;   /* ENEMIES: +0xec countdown */
        O->cur = o;             /* AUDSHELL */
        switch (o->kind) {
        case K_ENEMY: enemy_tick(O, o); break;     /* ENEMIES */
        case K_SOLID: solids_tick(O, o); break;    /* SOLIDS */
        case K_DEBRIS: debris_tick(O, o); break;   /* ENEMIES */
        case K_BOSS: boss_tick(O, o); break;       /* BOSSES */
        case K_TITAN: titans_ai_tick(O, o); break;   /* TITANS2 */
        case K_PICKUP: upd_pickup(O, o, pbox); break;
        case K_CHECKPOINT: upd_checkpoint(O, o, pbox); break;
        case K_GENERIC1: upd_generic1(O, o, cx); break;
        case K_ACTOR: upd_actor(O, o); break;
        case K_EFFECT:
            if (o->ai) { enemies_tick_proj(O, o); break; }   /* ENEMIES: projectiles */
            if (run_words(O, o) || --o->life <= 0) kill_obj(O, o, 1);
            break;
        default: break;
        }
        /* FUN_1002a9e0: objects far off screen go dormant and may respawn later. */
        if (o->used && o->kind != K_GENERIC1 && o->kind != K_ENEMY && o->kind != K_SOLID && o->kind != K_BOSS && o->kind != K_TITAN /* TITANS2 */ && !o->parent && iabs(I(o->x) - cx) > 0x240)
            kill_obj(O, o, o->kind == K_EFFECT || o->kind == K_DEBRIS);
    }
    O->cur = NULL;
    solids_frame(O, P);         /* SOLIDS: carrying, poles, riders */
    if (getenv("SCROLL_TEST") && dbg_tick == 10) { O->scroll_bits |= 0x5; obj_scrolls_trigger(O, atoi(getenv("SCROLL_TEST"))); }   /* AUDSHELL: test hook */
    obj_scrolls_tick(O);        /* AUDSHELL */
}

void objects_draw(Objects *O, Renderer *R)
{
    obj_scrolls_draw(O, R);     /* AUDSHELL: screen-space, sorted by key only */
    solids_draw_reset();        /* SOLIDS */
    bosses_draw_begin();    /* BOSSES */
    for (int i = 0; i < OBJ_MAX; i++) {
        Object *o = &O->obj[i];
        if (o->used && o->kind == K_BOSS) { boss_draw(O, R, o); continue; }   /* BOSSES */
        if (o->used && o->kind == K_TITAN) { titans_ai_draw(O, R, o); continue; }   /* TITANS2 */
        if (!o->used || o->frame <= 0) continue;
        if (o->bank == 0 && (o->flags54 & 0x10000000u)) { solids_draw(O, R, o); continue; }   /* SOLIDS: 3D model */
        Bank *b = get_bank(O, o->bank);
        if (!b) continue;
        if (o->hidden) continue;                                  /* LEFTOVERS */
        const Image *img = o->palx ? bank_frame_pal(b, o->frame, o->palx - 1) : bank_frame(b, o->frame);   /* LEFTOVERS: +0x1c */
        if (!img) continue;
        float wx, wz;
        level_path_to_world(O->L, o->x / 65536.0f, o->z / 65536.0f, &wx, &wz, NULL);
        {   /* FX: FUN_100311a0 colour word: +0x4a brightness (0x80 = normal) scales the GPU colour; flags54 & 4 =
             * semi-transparent with abr = flags54 & 3 (the port's o->semi stands for the additive "| 5" setting).
             * flags54 & 0x600 objects take their colour from the lighting ramps, which are not ported: drawn unlit. */
            uint32_t tint = 0x808080;
            /* FX3: flags54 & 0x600 objects take ramp[+0x2c & 7][idx] (FUN_100311a0); the projection FUN_10032300
             * clears +0x2c before every draw, so it is ramp 0, which FUN_10005a60 fills with the plain level colour:
             * the same as every other object. The level colour itself is applied in render_flush (R->light). */
            {
                int bv = o->bright ? o->bright : 0x80;
                if (o->semi || (o->flags54 & 4)) bv = o->bright;
                if (bv < 0) bv = 0;
                if (bv > 0xff) bv = 0xff;
                tint = (uint32_t)bv * 0x010101u;
            }
            int mode = (o->flags54 & 4) ? (int)(o->flags54 & 3) : o->semi ? 1 : -1;
            if (!fx3_draw_mode(O, R, o, img, wx, o->y / 65536.0f, wz, tint, mode)) {   /* FX3: draw modes 4 / 7 / 8 */
                if (render_sprite_fx(R, img, wx, o->y / 65536.0f, wz, (o->flags54 & 0x10) != 0, -4, o->scale, tint, mode, 1.0f) &&
                    (o->rot[2] & 0x3ff))     /* RENDERFIX: obj+0x74 != 0 selects the rotated draw (FUN_10004330 / 100048b0) */
                { if (getenv("ROT_LOG")) fprintf(stderr, "ROTSPRITE type %04x rot2 %d\n", o->type, o->rot[2]); render_last_angle(R, (float)(o->rot[2] * 4)); }
            }
        }
        if (!fx_has_shadow(o->init_fn) && (o->flags54 & 8))   /* FX: flags54 & 8 floor shadow (FUN_10003cd0 -> FUN_10030790) */
            fx_shadow_draw(R, O->L, O, img, o->x, o->y, o->z, (o->flags54 & 0x10) != 0, o->scale, 0, -1);
        if (fx_has_shadow(o->init_fn))   /* FX: the 0x2024 shadow / reflection child (FUN_1000f180) */
            fx_shadow_draw(R, O->L, O, img, o->x, o->y, o->z, (o->flags54 & 0x10) != 0, o->scale, o->bright ? o->bright : 0x80,
                           O->P ? O->P->water_y : 0);
    }
}

int objects_floor(void *ctx, int x, int y, int z, int *up, int *down) { return solids_floor(ctx, x, y, z, up, down); }

int objects_attr(void *ctx, int x, int y, int z)
{
    Objects *O = ctx;
    { int a = solids_attr(O, x, y, z); if (a >= 0) return a; }   /* SOLIDS */
    for (int i = 0; i < OBJ_MAX; i++) {
        Object *o = &O->obj[i];
        if (!o->used || o->attr < 0 || o->kind == K_SOLID) continue;
        int b[6];
        if (!world_box(O, o->bank, o->frame, (o->flags54 & 0x10) != 0, o->x, o->y, o->z, b)) continue;
        if (x >= b[0] && x <= b[3] && y >= b[1] && y <= b[4] && z >= b[2] && z <= b[5]) return o->attr;
    }
    return -1;
}

/* ---------------------------------------------------------------- ENEMIES: exported helpers */

int obj_rnd(Objects *O, int n) { return rnd(O, n); }
void obj_sound(Objects *O, int id, int32_t x) { snd_at(O, id, x); }
void obj_set_anim(Objects *O, Object *o, int a) { set_anim(O, o, a); }
int obj_run_script(Objects *O, Object *o) { return o->pairs ? run_pairs(O, o) : run_words(O, o); }
void obj_kill(Objects *O, Object *o, int gone) { kill_obj(O, o, gone); }
int obj_box(Objects *O, const Object *o, int k, int box[6])
{
    int b[6];
    if (!frame_box(O, o->bank, o->frame, k, b)) return 0;
    /* FUN_10030e40: the stored corners become origin + extent, and each of origin / extent is scaled by +0x3a
     * (x * s >> 8) separately, not the far corner. */
    int x = b[0], y = b[1], z = b[2], w = b[3] - b[0], h = b[4] - b[1], dz = b[5] - b[2];
    if (o->scale && o->scale != 0x100) {
        int s = o->scale;
        x = x * s >> 8; y = y * s >> 8; z = z * s >> 8; w = w * s >> 8; h = h * s >> 8; dz = dz * s >> 8;
    }
    if (!w && !h) return 0;                         /* FUN_1002a880 */
    int X = I(o->x), Y = I(o->y), Z = I(o->z);
    if (!(o->flags54 & 0x10)) { box[0] = X + x; box[3] = box[0] + w; }
    else { box[3] = X - x; box[0] = box[3] - w; }
    if (!(o->flags54 & 0x20)) { box[1] = Y + y; box[4] = box[1] + h; }     /* 0x20: drawn upside down */
    else { box[4] = Y - y; box[1] = box[4] - h; }
    box[2] = Z + z; box[5] = box[2] + dz;
    return 1;
}
int obj_ground_follow(Objects *O, Object *o) { return ground_follow(O, o); }
void obj_drop(Objects *O, Object *o) { drop_to_floor(O, o); }
uint16_t obj_attr_flags(Objects *O, int x, int y, int z) { return attr_flags(O, x, y, z); }
Object *obj_spawn(Objects *O, uint16_t type, int32_t x, int32_t y, int32_t z) { return spawn_type(O, type, x, y, z); }
int obj_boxes_overlap(const int a[6], const int b[6]) { return boxes_overlap(a, b); }

/* SOLIDS: exported helpers */
Object *obj_alloc(Objects *O) { return alloc_obj(O); }
int obj_run_words(Objects *O, Object *o) { return run_words(O, o); }
int obj_type_info(Objects *O, uint16_t type, uint32_t *script, uint32_t *anims, uint32_t *list, int *bank, uint32_t *init)
{ return type_info(O, type, script, anims, list, bank, init); }
Bank *obj_bank(Objects *O, int b) { return get_bank(O, b); }

/* LEFTOVERS: exports for leftovers.c */
void obj_move_table(Objects *O, Object *o) { move_table(O, o); }   /* FUN_100291d0 */

/* ---------------------------------------------------------------- AUDSHELL: HERCULES letter display (ENG1) */

static ScrollCfg scr_cfg(Objects *O)
{
    ScrollCfg c = {O->pe, 0x10052b50u, 0x10052b70u, 0x100537e0u, 0x460000, 0, 2, 6};
    return c;
}
static void scr_tick_sound(void *ctx, int x) { (void)x; obj_sound_none(ctx, 0xd, 0); }     /* FUN_10006da0(0xd, 0) */
static Bank *scr_bank(void *ctx, int b) { return obj_bank(ctx, b); }
void obj_scrolls_trigger(Objects *O, int id) { if (O->scr) scrolls_trigger(O->scr, id); }
void obj_scrolls_tick(Objects *O) { if (O->scr) { ScrollCfg c = scr_cfg(O); scrolls_tick(O->scr, &c, scr_tick_sound, O); } }
void obj_scrolls_draw(Objects *O, Renderer *R) { if (O->scr) { ScrollCfg c = scr_cfg(O); scrolls_draw(O->scr, &c, R, O->scroll_bits, scr_bank, O); } }
