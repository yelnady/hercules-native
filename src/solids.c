/* SOLIDS: 3D map-model objects, groups, solid/platform object lists, carrying and poles.
 * Ports of ENG1.DLL:
 *   FUN_10031330 / FUN_100313e0 / FUN_10031470 / FUN_10031540 / FUN_100318d0   model drawing
 *   FUN_10031ec0                                                               model collision box
 *   FUN_1002b130                                                               group children
 *   FUN_1002a170 / FUN_100296e0 / FUN_10029b70                                 solid + platform lists
 * and the per-type init/update handlers listed in solids_init(). */
#include "solids.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline uint32_t mu32(const uint8_t *d, uint32_t a) { return d[a] | d[a+1] << 8 | d[a+2] << 16 | (uint32_t)d[a+3] << 24; }
static inline uint16_t mu16(const uint8_t *d, uint32_t a) { return (uint16_t)(d[a] | d[a+1] << 8); }
static inline int16_t ms16(const uint8_t *d, uint32_t a) { return (int16_t)mu16(d, a); }
static int iabs(int v) { return v < 0 ? -v : v; }

/* ---------------------------------------------------------------- model data */

/* FUN_10020670: DAT_10070d2c = block table + 4 * (header u16 +0xc) - 4; entry i holds a self-relative
 * pointer to model record i (1-based). */
uint32_t model_rec(const Level *L, int idx)
{
    if (idx <= 0 || !L->d) return 0;
    int ngroup = mu16(L->d, 0xe);
    if (idx > ngroup) return 0;
    uint32_t e = L->blocks + 4u * ((uint32_t)mu16(L->d, 0xc) - 1) + 4u * (uint32_t)idx;
    if (e + 4 > L->size) return 0;
    uint32_t r = e + mu32(L->d, e);
    if (r + 16 > L->size) return 0;
    return r;
}

static void model_offset(const uint8_t *d, uint32_t rec, int *ox, int *oy, int *oz)
{
    uint32_t v = mu32(d, rec + 4);
    *ox = (int8_t)(v & 0xff);
    *oy = (int16_t)((v >> 8) & 0xffff);
    *oz = (int8_t)(v >> 24);
}

int model_box(const Level *L, const Object *o, int k, int out[6])
{
    uint32_t r = model_rec(L, o->frame);
    if (!r) return 0;
    const uint8_t *d = L->d;
    int n = d[r + 2];
    if (k == 0) { if (!n) return 0; k = 1; }
    if (n < k) return 0;
    uint32_t b = r + 8u * (uint32_t)k;
    uint32_t w0 = mu32(d, b), w1 = mu32(d, b + 4);
    int z0 = (int8_t)(w0 & 0xff), x0 = (int16_t)((w0 >> 8) & 0xffff), y0 = (int8_t)(w0 >> 24);
    int dd = (int)(w1 & 0xff), ww = (int)((w1 >> 8) & 0xffff), hh = (int)(w1 >> 24);
    int ox, oy, oz;
    model_offset(d, r, &ox, &oy, &oz);
    if (mu32(d, r + 4)) { x0 += ox; y0 += oy; z0 += oz; }
    int s = o->scale ? o->scale : 0x100;
    if (s != 0x100) { x0 = x0 * s >> 8; y0 = y0 * s >> 8; z0 = z0 * s >> 8; ww = ww * s >> 8; hh = hh * s >> 8; dd = dd * s >> 8; }
    out[0] = x0; out[1] = y0; out[2] = z0; out[3] = ww; out[4] = hh; out[5] = dd;
    return n;
}

/* ---------------------------------------------------------------- drawing */

#define POOL 6000
static Face pool[POOL];
static int npool;

void solids_draw_reset(void) { npool = 0; }

static void rotmat(const int16_t rot[3], float m[3][3])
{
    float ax = rot[0] * (6.2831853f / 1024.0f), ay = rot[1] * (6.2831853f / 1024.0f), az = rot[2] * (6.2831853f / 1024.0f);
    float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay), cz = cosf(az), sz = sinf(az);
    /* PSX RotMatrix: M = Rx * Ry * Rz */
    m[0][0] = cy * cz;                m[0][1] = -cy * sz;               m[0][2] = sy;
    m[1][0] = sx * sy * cz + cx * sz; m[1][1] = -sx * sy * sz + cx * cz; m[1][2] = -sx * cy;
    m[2][0] = -cx * sy * cz + sx * sz; m[2][1] = cx * sy * sz + sx * cz; m[2][2] = cx * cy;
}

static const int g_xsign = 1;     /* model x runs along the path; mirror-flagged faces keep the GTE winding */

static int audit_on; static float audit_mn[3], audit_mx[3];
void solids_draw(Objects *O, Renderer *R, const Object *o)
{
    const Level *L = O->L;
    const uint8_t *d = L->d;
    uint32_t r = model_rec(L, o->frame);
    if (!r) return;
    float m[3][3];
    rotmat(o->rot, m);
    float s = (o->scale ? o->scale : 0x100) / 256.0f;
    int ox = 0, oy = 0, oz = 0;
    if (mu32(d, r + 4)) model_offset(d, r, &ox, &oy, &oz);
    float OX = o->x / 65536.0f, OY = o->y / 65536.0f, OZ = o->z / 65536.0f;
    { float bx, bz; level_path_to_world(L, OX, OZ, &bx, &bz, NULL); render_model_begin(R, bx, OY, bz); }   /* RENDERFIX */
    uint32_t texbase = L->d ? (0x28 + (mu32(d, 0x28) & ~3u)) : 0;
    uint32_t facebase = 0x24 + (mu32(d, 0x24) & ~3u);
    int nbox = d[r + 2];
    uint32_t g = r + 10 + 8u * (uint32_t)nbox;              /* vertex groups */
    uint32_t fd = facebase + 4u * (uint32_t)mu16(d, r);       /* face groups */
    for (int grp = 0; grp < 32; grp++) {
        if (g + 2 > L->size || fd + 4 > L->size) return;
        int nv = ms16(d, g);
        if (nv <= 0 || nv > 64 || g + 2 + 4u * nv > L->size) return;
        Vec3 V[64];
        for (int k = 0; k < nv; k++) {
            uint32_t v = mu32(d, g + 2 + 4u * k);
            float vx = (float)(int16_t)((v >> 8) & 0xffff), vy = (float)(int8_t)(v >> 24), vz = (float)(int8_t)(v & 0xff);
            float rx = m[0][0] * vx + m[0][1] * vy + m[0][2] * vz;
            float ry = m[1][0] * vx + m[1][1] * vy + m[1][2] * vz;
            float rz = m[2][0] * vx + m[2][1] * vy + m[2][2] * vz;
            float px = OX + g_xsign * (rx * s + ox), py = OY + ry * s + oy, pz = OZ + rz * s + oz;
            if (audit_on) { if (px < audit_mn[0]) audit_mn[0] = px; if (px > audit_mx[0]) audit_mx[0] = px; if (py < audit_mn[1]) audit_mn[1] = py;
                if (py > audit_mx[1]) audit_mx[1] = py; if (pz < audit_mn[2]) audit_mn[2] = pz; if (pz > audit_mx[2]) audit_mx[2] = pz; }
            level_path_to_world(L, px, pz, &V[k].x, &V[k].z, NULL);
            V[k].y = py;
        }
        int nf = mu16(d, fd), fl = mu16(d, fd + 2);
        uint32_t f = fd + 4;
        for (int k = 0; k < nf && f + 8 <= L->size; k++, f += 8) {
            uint32_t f0 = mu32(d, f), f1 = mu32(d, f + 4);
            int i0 = f0 & 63, i1 = (f0 >> 6) & 63, i2 = (f0 >> 12) & 63, i3 = (f0 >> 18) & 63;
            int fb = (int)(f0 >> 24);
            if (i0 >= nv || i1 >= nv || i2 >= nv || i3 >= nv) continue;
            int cull = (fb & 0x40) ? 2 : 1;
            if (fb & 8) { if (fb & 1) cull = 0; else if (fb & 4) continue; }
            if (npool >= POOL) return;
            Face *F = &pool[npool];
            F->p[0] = V[i0]; F->p[1] = V[i1]; F->p[2] = V[i3]; F->p[3] = V[i2];   /* GPU order v0 v1 v3 v2 */
            F->org.x = (V[i0].x + V[i2].x) * 0.5f; F->org.y = (V[i0].y + V[i2].y) * 0.5f; F->org.z = (V[i0].z + V[i2].z) * 0.5f;
            F->flags = 0; F->otb = 0; F->mirror = g_xsign > 0;
            if (f1 & 0xff000000u) {
                for (int q = 0; q < 4; q++) { F->uv[q][0] = (uint8_t)f1; F->uv[q][1] = 0xff; }
                F->tpage = 0x9c;
            } else {
                uint32_t t = texbase + f1;
                if (t + 12 > L->size) continue;
                uint32_t e0 = mu32(d, t), e1 = mu32(d, t + 4), e2 = mu32(d, t + 8);
                F->uv[0][0] = e0 & 0xff; F->uv[0][1] = (e0 >> 8) & 0xff;
                F->uv[1][0] = e1 & 0xff; F->uv[1][1] = (e1 >> 8) & 0xff;
                F->uv[2][0] = e2 & 0xff; F->uv[2][1] = (e2 >> 8) & 0xff;
                F->uv[3][0] = (e2 >> 16) & 0xff; F->uv[3][1] = (e2 >> 24) & 0xff;
                F->tpage = (uint16_t)(e1 >> 16);
                if ((F->tpage >> 5) & 3) F->flags = 0;
            }
            npool++;
            render_face(R, F, cull);
        }
        g += 4u + 4u * (uint32_t)nv;
        if ((fl & 0xffff) == 0) return;
        uint32_t p = f + (uint32_t)((fl >> 8) & 0xff);
        fd = (p + 3) & ~3u;
        if (!(fl & 0xff)) return;
    }
}

/* ---------------------------------------------------------------- toolkit */

enum {
    /* init handlers (level handler tables) */
    I_T_CANNON = 0x1003f140,    /* Training 0x37 (update 0x1003f1c0) */
    I_T_RACK = 0x1003ebc0,      /* Training 0x3f (0x1003ecb0) */
    I_T_LIFT = 0x1003edf0,      /* Training 0x44 / 0x45 (0x1003eec0) */
    I_T_WHEEL = 0x10042020,     /* Training 0x46 (0x10042090) */
    I_T_BRIDGE = 0x10041820,    /* Training 0x41 / 0x47 collapsing bridge (0x100418a0) */
    I_T_TILE = 0x10041a50,      /* 0x4035 / 0x403e bridge planks (0x10041ad0 / 0x10041b70) */
    I_T_FENCE = 0x1003f620,     /* Training 0x49 (0x1003f720) */
    I_STACK_T = 0x10042710,     /* Training 0x4e stone stack (0x10042770) */
    I_STACK = 0x10036150,       /* Thebes 0x49 stone stack (0x100361a0) */
    I_STONE_T = 0x100429c0,     /* 0x4040 stack stone (0x10042bd0) */
    I_STONE = 0x100363e0,       /* 0x402d stack stone (0x100365f0) */
    I_POT = 0x1000f910,         /* pots / urns 0x40 (T) 0x4f (F) 0x46 (Th) (0x1000fa00) */
    I_SHARD = 0x1000fd00,       /* 0x4023 pot pieces (0x1000fe60) */
    I_FWALL_A = 0x10009fb0,     /* Forest 0x4c breakable rock (0x1000a010) */
    I_FWALL_B = 0x1000a420,     /* Forest 0x4e breakable rock (0x1000a490) */
    I_BLOCK = 0x10014790,       /* Thebes 0x04 carryable block (0x10014850) */
    I_ROCK5 = 0x10014210,       /* LEFTOVERS: Thebes 0x05 carryable rock kept alive by spawner 0x48 (update 0x100142d0) */
    I_ROCK2 = 0x10013ca0,       /* LEFTOVERS: Training/Forest 0x02 carryable rock (update 0x10013d70) */
    I_DECOR = 0x10040a50,       /* 0x4031 / 0x4034 decorations (0x10040ab0) */
    I_POST = 0x1003f040,        /* 0x4039 solid post */
    I_GLOW = 0x1003f070,        /* 0x202f */
    I_SPR_A = 0x1003eaa0, I_SPR_B = 0x1003eac0, I_SPR_C = 0x1003ea00, I_SPR_D = 0x1003ea70,   /* Training markers */
    I_POLE_T = 0x100422d0,      /* Training 0x2030 swing pole (update 0x100422f0) */
    I_POLE = 0x100360a0,        /* Thebes 0x202b swing pole (update 0x100360f0) */
    I_ST_32 = 0x1003ce10, I_ST_33 = 0x1003ca80, I_ST_35 = 0x1003d800, I_ST_39 = 0x1003d310, I_ST_3E = 0x1003db90,
    /* internal kinds (not ENG1 addresses) */
    I_RUBBLE = 1,               /* 0x4028 rubble model (FUN_10036990 / FUN_100367b0) */
};

#define P_ (O->P)
static int rnd(Objects *O, int n) { return obj_rnd(O, n); }
static void snd(Objects *O, int id, const Object *o) { obj_sound(O, id, o ? o->x : P_->o.x); }


/* FUN_10030e40 + FUN_1002a880: world box k of an object (model box, explicit +0x88 box or sprite box). */
static int wbox(Objects *O, const Object *o, int k, int b[6])
{
    int r[6];
    if (o->has88 && k <= 1) { for (int i = 0; i < 6; i++) r[i] = o->box88[i]; }
    else if (o->bank == 0) { if (!model_box(O->L, o, k, r)) return 0; }
    else {
        int s[6];
        if (!obj_box(O, o, k < 1 ? 1 : k, s)) return 0;
        memcpy(b, s, sizeof s);
        return 1;
    }
    int X = I(o->x), Y = I(o->y), Z = I(o->z);
    if (!(o->flags54 & 0x10)) { b[0] = X + r[0]; b[3] = b[0] + r[3]; } else { b[3] = X - r[0]; b[0] = b[3] - r[3]; }
    if (!(o->flags54 & 0x20)) { b[1] = Y + r[1]; b[4] = b[1] + r[4]; }       /* FUN_1002a880: flags54 & 0x20 mirrors y */
    else { b[4] = Y - r[1]; b[1] = b[4] - r[4]; }
    b[2] = Z + r[2]; b[5] = b[2] + r[5];
    return 1;
}

/* FUN_10026de0: 0 none, 1 the player's blow, 2 a projectile. */
static int took_hit(Objects *O, Object *o)
{
    int b[6];
    if (!wbox(O, o, 1, b)) return 0;
    for (int i = 0; i < OBJ_MAX; i++) {
        Object *p = &O->obj[i];
        if (!p->used || p->kind != K_EFFECT || !p->ai || p->life <= 0) continue;
        int X = I(p->x), Y = I(p->y), Z = I(p->z);
        int pb[6] = {X - 10, Y - 10, Z - 24, X + 10, Y + 10, Z + 24};
        if (obj_boxes_overlap(pb, b)) { p->life = 0; return 2; }
    }
    Box ab;
    if (player_attack_box(P_, &ab)) {
        int a[6] = {ab.x0, ab.y0, ab.z0, ab.x1, ab.y1, ab.z1};
        if (getenv("HIT_LOG")) printf("HIT_LOG solid type %x atk (%d,%d,%d)-(%d,%d,%d) body (%d,%d,%d)-(%d,%d,%d) %s\n", o->type, a[0], a[1], a[2], a[3], a[4], a[5], b[0], b[1], b[2], b[3], b[4], b[5], obj_boxes_overlap(a, b) ? "OVERLAP" : "miss");   /* LV_FOREST: verification only */
        if (obj_boxes_overlap(a, b)) return 1;
    }
    return 0;
}

/* DAT_10072012 & 0x5f & ~DAT_10072168: blow kinds not used yet this tick; marks them used. */
static int fresh_blow(Objects *O)
{
    int k = P_->hitflags & 0x5f & ~P_->consumed;
    if (k) P_->consumed |= (uint16_t)k;
    return k;
}
/* Full power punch: hit kind 2 with the charge meter (DAT_1007082c + 2) at 200 or more. */
/* DAT_1007082c + 2 holds the meter as it was when the blow started; the player's state 0x2a drains its own
 * copy by 0x3c per tick before objects update, so the peak of the current punch stands in for it here. */
static int power_peak;   /* highest meter reading during the current charged punch (state 0x2a) */
static int power_blow(Objects *O) { return (P_->hitflags & 2) && power_peak >= 200; }

/* FUN_10029f50 / FUN_10029e20 for objects: landed on the floor (map or platform object)? */
static int land(Objects *O, Object *o)
{
    int up, down;
    level_probe(O->L, I(o->x), I(o->y), I(o->z), &up, &down, NULL);
    int dy = iabs(I(o->y - o->py)), dx = iabs(I(o->x - o->px)), dz = iabs(I(o->z - o->pz));
    if (down == 0) return 1;
    if (up != 0x7fff && up <= (dy + dx + dz) * 2 + 16) { o->y -= FX(up); return 1; }
    return 0;
}

/* FUN_1002bd20: velocity integration with bounce on landing. Returns 1 landed, 3 landed and stopped. */
static int phys(Objects *O, Object *o)
{
    int32_t *v = o->v;
    int ret = 0;
    v[0] += v[2] - v[3]; if (v[0] < 0) v[0] = 0; if (v[0] > v[1]) v[0] = v[1];
    v[4] += v[6] - v[7]; if (v[4] < 0) v[4] = 0; if (v[4] > v[5]) v[4] = v[5];
    v[8] += v[10] - v[11]; if (v[8] < 0) v[8] = 0; if (v[8] > v[9]) v[8] = v[9];
    o->x += o->facing ? -v[0] : v[0];
    o->y += o->b95 ? -v[4] : v[4];
    o->z += o->zface ? -v[8] : v[8];
    if (v[4] == 0) { v[6] = v[7]; v[7] = 0; o->b95 = 0; v[3] = v[2]; v[2] = 0; }
    if (!o->b95 && land(O, o)) {
        v[4] >>= 1; v[7] = v[6]; v[6] = 0; o->b95 = 0xff; v[2] = v[3]; v[3] = 0;
        ret = v[4] == 0 ? 3 : 1;
    }
    return ret;
}

static void drop_floor(Objects *O, Object *o) { obj_drop(O, o); }     /* FUN_1002a290 */

static int offscreen(Objects *O, const Object *o, int range)       /* FUN_1002a9e0 */
{
    int sx = 160 + I(o->x) - O->camx;
    return sx + range < 0 || sx - range > 0x13f;
}

/* FUN_10041fd0: Training station index from the x position (table 0x10065678, 12-byte entries). */
static int station(Objects *O, int32_t x)
{
    int X = I(x), i = 0;
    for (uint32_t a = 0x10065678; i < 32; a += 12, i++) {
        uint16_t e = pe_u16(O->pe, a);
        if (e == 0xffff) break;
        if (X > 0 && X < e) return i;
    }
    return 0xff;
}

static void dust(Objects *O, const Object *o, int n)                /* 0x2027 dust puffs */
{
    for (int i = 0; i < n; i++) {
        Object *p = obj_spawn(O, 0x2027, o->x + FX(rnd(O, 0x40) - 0x20), o->y - FX(rnd(O, 0x40)), o->z - FX(0x20));
        if (p) p->spawn = -1;
    }
}

/* FUN_10036990 / FUN_100367b0: flying rubble (0x4028 models 9 / 10). */
static void rubble(Objects *O, const Object *src, int n, int hmax, int big)
{
    for (int i = 0; i < n; i++) {
        Object *p = obj_alloc(O);
        if (!p) return;
        p->kind = K_SOLID; p->init_fn = I_RUBBLE; p->type = 0x4028; p->bank = 0;
        p->x = p->px = src->x + FX(rnd(O, 0x20) - 0x10);
        p->y = p->py = src->y - FX(rnd(O, hmax) + (big ? 0 : 0x30));
        p->z = p->pz = src->z + FX(rnd(O, 0x20) - 0x10);
        p->flags54 |= 0x10000000u;
        p->frame = rnd(O, 2) ? 9 : 10;
        p->scale = (uint16_t)(rnd(O, big ? 0xa0 - 3 * n : (0x20 - n) * 4) + 0x40 - (big ? 2 : 4) * n);
        if ((int16_t)p->scale < 0x20) p->scale = 0x20;
        p->facing = (uint8_t)-rnd(O, 2); p->zface = (uint8_t)-rnd(O, 2); p->b95 = 0xff;
        p->v[6] = 0; p->v[5] = p->v[4] = ((rnd(O, 0x200)) + 0x800) * 0x100; p->v[7] = rnd(O, 0x2000) + 0x4000;
        p->v[9] = p->v[8] = (rnd(O, 0x100) + 0x200) * 0x100; p->v[10] = rnd(O, 0x1000) + 0x2000; p->v[11] = 0;
        p->v[1] = p->v[0] = (rnd(O, 0x200) + 0x100) * 0x100; p->v[2] = rnd(O, 0x1000) + 0x1000; p->v[3] = 0;
        p->life = 90;
        p->d[0] = rnd(O, 16) - 8; p->d[2] = rnd(O, 16) - 8;
    }
}

/* ---------------------------------------------------------------- groups */

int solids_spawn_children(Objects *O, Object *o)                   /* FUN_1002b130 */
{
    const PeImage *pe = O->pe;
    uint32_t L = o->list;
    if (!L || !pe_ptr(pe, L, 12)) return 0;
    uint16_t type = pe_u16(pe, L + 6);
    for (int i = 0; type && i < 24; i++) {
        uint32_t e = L + 12u * (uint32_t)i;
        int32_t x = o->x + FX(pe_s16(pe, e)), y = o->y + FX(pe_s16(pe, e + 2)), z = o->z + FX(pe_s16(pe, e + 4));
        int16_t frame = pe_s16(pe, e + 8), v2a = pe_s16(pe, e + 10);
        O->spawn_flags = 0;
        Object *k = NULL;
        if ((type & 0x7000) == 0 || (type & 0x7000) == 0x4000 || (type & 0x7000) == 0x2000) {
            /* the child's init sees +0x2a already set (FUN_1002b130 stores it before calling init) */
            O->pending_v2a = v2a; O->have_pending = 1;
            k = obj_spawn(O, type, x, y, z);
            O->have_pending = 0;
        }
        if (k && getenv("KID_LOG")) printf("KID parent %04x (%d,%d,%d) kid %04x d(%d,%d,%d) frame %d v2a %d\n", o->type, I(o->x), I(o->y), I(o->z), type, pe_s16(pe, e), pe_s16(pe, e + 2), pe_s16(pe, e + 4), frame, v2a);
        if (k) {
            k->spawn = -1;
            k->parent = o; k->kidx = i;
            if (frame) k->frame = frame;
            if (o->nkids < 24) o->kid[o->nkids++] = k;
        }
        type = pe_u16(pe, e + 0x12);
    }
    return 1;
}

void solids_kill(Objects *O, Object *o, int gone)
{
    (void)gone;
    if (o->parent) {
        Object *p = o->parent;
        for (int i = 0; i < p->nkids; i++) if (p->kid[i] == o) { memmove(&p->kid[i], &p->kid[i + 1], sizeof(Object *) * (size_t)(p->nkids - i - 1)); p->nkids--; break; }
        o->parent = NULL;
    }
    for (int i = 0; i < o->nkids; i++) {
        Object *k = o->kid[i];
        if (k && k->used) { k->parent = NULL; obj_kill(O, k, 1); }
    }
    o->nkids = 0;
    if (O->P && O->P->carried == o) { O->P->carried = NULL; }
}

static void remove_kid(Object *p, Object *k)                       /* FUN_1002b320 */
{
    for (int i = 0; i < p->nkids; i++) if (p->kid[i] == k) { memmove(&p->kid[i], &p->kid[i + 1], sizeof(Object *) * (size_t)(p->nkids - i - 1)); p->nkids--; break; }
    k->parent = NULL;
}

/* ---------------------------------------------------------------- init handlers */

static void model(Object *o) { o->flags54 |= 0x10000000u; }
static void station_kids(Objects *O, Object *o)                     /* the common tail of the station inits */
{
    uint32_t sinit = o->type == 0x39 ? I_ST_39 : o->type == 0x35 ? I_ST_35 : 0;   /* FIX32: station-specific kid fix-ups */
    if (!solids_spawn_children(O, o)) { obj_kill(O, o, 0); return; }
    uint16_t done = training_tbl(O, o->b21, 1) /* ENEMIES3 */;
    for (int i = 0; i < o->nkids; i++) {
        Object *k = o->kid[i];
        if (k->type == 0x2f) {
            /* FIX32: FUN_1003d310 (carousel 0x39): the meter is pulled onto Hercules' plane (z = 0x80) whenever the station itself sits on
             * another one (the station is at z 192): without it the prize girl stayed on z 192 and could never be touched from z 128. */
            if (sinit == I_ST_39 && o->z != 0x800000) k->z = 0x800000;
            k->d[0] = k->y; k->d[1] = o->d[1]; k->d[2] = o->d[2];
        }
        if (k->type == 0x4032) { k->d[0] = o->d[0]; k->flags54 |= 0x10000; }
        if (k->type == 0x4034) k->s98 = 4;
        if (k->type == 0x30 || k->type == 0x2a || k->type == 0x3a || k->type == 0x4d) {
            if (done & 1) { k->flags50 &= ~0x200u; obj_set_anim(O, k, 0xc); k->v2a = 9; if (sinit == I_ST_35 && k->type == 0x2a) k->z = 0x800000; }   /* FIX32: FUN_1003d800 */
            done >>= 1;
        }
    }
}

static int audit_init(Objects *O, Object *o, uint32_t init);
static uint8_t fence_done[8];
int solids_init(Objects *O, Object *o, uint32_t init, uint32_t update)
{
    (void)update;
    if (audit_init(O, o, init)) return 1;   /* AUDIT */
    switch (init) {
    case I_T_CANNON:                                                /* FUN_1003f140 */
        model(o); o->flags50 |= 0x100200; o->attr = 2; o->frame = 0x41;
        drop_floor(O, o); o->v2a = 0;
        o->rot[1] = (int16_t)(rnd(O, 0x100) - 0x80); o->hp = 3; o->rot[0] = (int16_t)0xff00;
        o->d[1] = rnd(O, 4) * 4 + 4;
        break;
    case I_T_RACK: {                                                /* FUN_1003ebc0 */
        model(o); o->flags50 |= 0x100000; o->attr = 1; o->v2a = 0;
        drop_floor(O, o);
        o->b21 = (uint8_t)station(O, o->x); o->frame = 0x35;
        uint16_t st = training_tbl(O, o->b21, 5) /* ENEMIES3 */;
        o->d[2] = o->z + 0xe00000; o->v2a = st;
        if (st > 2) o->z += 0xe00000;
        if (!solids_spawn_children(O, o)) return 0;
        for (int i = 0; i < o->nkids; i++)
            if (o->kid[i]->type == 0x4039) o->kid[i]->frame = (int16_t)pe_u16(O->pe, 0x10066040 + 2u * o->b21);
        break;
    }
    case I_T_LIFT:                                                  /* FUN_1003edf0 */
        model(o); o->flags50 |= 0x100000; o->attr = 1; o->v2a = 0;
        drop_floor(O, o); o->b95 = 0xff; o->scale = 0x300;
        o->v[5] = 0x130000; o->v[4] = 0; o->v[6] = 0; o->v[7] = 0x1f000;
        o->d[1] = o->y - 0x800000;
        o->b21 = (uint8_t)station(O, o->x);
        o->frame = (int16_t)pe_u16(O->pe, 0x10066040 + 2u * o->b21);
        o->v2a = training_tbl(O, o->b21, 5) /* ENEMIES3 */;
        solids_spawn_children(O, o);
        break;
    case I_T_WHEEL:                                                 /* FUN_10042020 */
        o->rot[0] = 0x300; o->frame = 0x37; o->flags54 |= 0x10004000u; o->scale = 0x140; o->timer = 0;
        o->v[0] = 0; o->v[1] = 0x80000; o->v[2] = 0x1000;
        solids_spawn_children(O, o);
        {   /* AUDIT: FUN_10027cd0(wheel, 0x202c): the creaking sprite attached to the wheel */
            Object *c = obj_spawn(O, 0x202c, o->x, o->y, o->z);
            if (c) { c->spawn = -1; c->parent = o; c->kidx = 0; if (o->nkids < 24) o->kid[o->nkids++] = c; }
        }
        break;
    case I_T_BRIDGE:                                                /* FUN_10041820 */
        o->flags50 |= 0x20000;
        drop_floor(O, o);
        if (!solids_spawn_children(O, o)) return 0;
        o->v2a = 0; o->hp = 0x1e;
        break;
    case I_T_TILE:                                                  /* FUN_10041a50 */
        o->flags54 |= 0x10000080u; o->flags50 |= 0x40000;
        o->tag = (uint8_t)o->v2a; o->v2a = 0; o->b95 = 0xff;
        o->v[4] = 0; o->v[5] = (rnd(O, 0x400) + 0xe00) * 0x100; o->v[6] = 0; o->v[7] = rnd(O, 0x6000) + 0x8000;
        o->cool = 6;
        break;
    case I_T_FENCE:                                                 /* FUN_1003f620 */
        o->x -= 0x20000;
        o->b21 = (uint8_t)station(O, o->x);
        o->tag = (uint8_t)(O->spawn_flags & 0x3f);                  /* AUDIT: +0x97 = spawn flags, the DAT_10070008 index */
        if (!solids_spawn_children(O, o)) return 0;
        for (int i = 0; i < o->nkids; i++) {
            Object *k = o->kid[i];
            k->frame = ((i + 1) & 1) == 0 ? 4 : 2;
            if (k->type == 0x4b && fence_done[o->tag & 7]) k->v2a = 1;          /* prize already given */
            if (k->type == 0x4a && fence_done[o->tag & 7]) { k->v2a = 9; k->flags50 = 0; k->frame = 0; }
        }
        break;
    case I_STACK_T: case I_STACK:                                   /* FUN_10042710 / FUN_10036150 */
        o->flags54 = 0;
        drop_floor(O, o);
        if (init == I_STACK_T && o->airborne) { o->hp = 0x23; break; }
        if (!solids_spawn_children(O, o)) return 0;
        o->hp = init == I_STACK ? 0x32 : 0x23;
        break;
    case I_STONE_T: case I_STONE: {                                 /* FUN_100429c0 / FUN_100363e0 */
        o->b95 = 0xff; o->zface = 0xff;
        model(o); o->flags50 |= 0x120800; o->attr = init == I_STONE ? 0x1a : 0x1b;
        static const int32_t vz[3] = {0x60000, 0x50000, 0x40000}, az[3] = {0x18000, 0x14000, 0x10000};
        static const int32_t rx[3] = {0x10, 8, 4}, ry[3] = {0, 0xc, 0x10};
        int k = o->v2a <= 2 ? o->v2a : 0;
        o->v[5] = o->v[4] = (rnd(O, 0x200) + 0x400) * 0x100; o->v[7] = rnd(O, 0x2000) + 0x5000;
        o->v[9] = vz[k]; o->v[8] = az[k]; o->v[10] = 0x2800;
        o->v[1] = 0x70000; o->v[0] = 0x1c000; o->v[2] = 0x3000; o->v[3] = 0;
        o->d[2] = rx[k]; o->d[1] = ry[k];
        o->v2a = 0;
        break;
    }
    case I_POT: {                                                   /* FUN_1000f910 */
        model(o); o->flags50 |= 0x240000;
        int v = o->v2a;
        o->tag = v == 3 ? 9 : v == 2 ? 8 : 0;
        o->hp = 0x1e; o->idx = 0; o->timer = 0; o->pairs = 0;
        obj_run_words(O, o);
        o->v2a = 0;
        break;
    }
    case I_SHARD: {                                                 /* FUN_1000fd00 */
        model(o);
        static const int base[3] = {0x480, 0x600, 0x400}, rr[3] = {0x1000, 0x2000, 0x1000}, rb[3] = {0x2000, 0x2000, 0x1000};
        int k = o->v2a <= 2 ? o->v2a : 0;
        o->v[5] = (rnd(O, 0x200) + base[k]) * 0x100; o->v[4] = o->v[5] >> 2;
        o->v[6] = rnd(O, rr[k]) + rb[k];
        o->b95 = 0xff;
        o->d[0] = rnd(O, 16) - 8; o->d[2] = rnd(O, 16) - 8;
        break;
    }
    case I_FWALL_A:                                                 /* FUN_10009fb0 */
        o->flags50 = 0x100002; o->attr = 3; model(o); o->d[12 - 12] = 0; o->v2a = 1;
        o->idx = 0; o->timer = 0; o->pairs = 0; o->hp = 0x32;
        o->y = FX(0x2e1 - 0x20);       /* DAT_100718e0: Forest water line */
        o->s98 = 0;
        obj_run_words(O, o);
        break;
    case I_FWALL_B:                                                 /* FUN_1000a420 */
        o->flags50 = 0x100002; o->attr = 3; model(o);
        o->b21 = (uint8_t)((I(o->z) + 0x40) >> 7);
        drop_floor(O, o);
        o->s98 = 0; o->y -= 0x400000; o->v2a = 1;
        o->idx = 0; o->timer = 0; o->pairs = 0; o->hp = 0x32;
        obj_run_words(O, o);
        break;
    case I_BLOCK: case I_ROCK5: case I_ROCK2: {                     /* FUN_10014790 (LEFTOVERS: 0x10014210 / 0x10013ca0 share it) */
        o->frame = init == I_ROCK5 ? 0x11 : init == I_ROCK2 ? (O->L->index == 1 ? 0x38 : 2) : 3; o->moving = 3; o->zface = 0xff; o->b95 = 0xff;
        model(o); o->flags50 |= 0x140000; o->attr = 7; o->v2a = 1;
        o->v[5] = 0x60000; o->v[4] = 0; o->v[7] = 0x6000; o->s9a = 0;
        int r[6];
        if (model_box(O->L, o, 0, r)) { for (int i = 0; i < 6; i++) o->box88[i] = (int16_t)r[i]; o->has88 = 1; }
        drop_floor(O, o);
        /* LV_THEBES: FUN_10014210 lifts the rock by DAT_100530de (32), not the drum's DAT_10052e76 (48): the rock hung 16 px in the
         * air, Hercules walked under it and could neither touch nor lift it (the minotaur fight's throwing rock). */
        /* LV_TRAINING: the Training/Forest 0x02 rock hung the same way under the drum lift, so Hercules walked under it and
         * could never push or lift it (the Training ledge rock). Rest its model box on the floor instead. */
        if (init == I_ROCK2 && o->has88) o->y -= FX(o->box88[1] + o->box88[4]);
        else o->y -= FX(pe_s16(O->pe, init == I_ROCK5 ? 0x100530de : 0x10052e76));
        break;
    }
    case I_DECOR: {                                                 /* FUN_10040a50 */
        o->flags54 |= 0x10004000u;
        /* DEPTH: FUN_10040a50 stores the 0x800 / 0x100 / 0x400 bits as a ROTATION angle (+0x74 / +0x70 = 0x300 or 0x100,
         * 4096 per turn = 4 * value), never as a scale. The port used to scale these pieces 3x, which turned the small
         * gate / stand posts into huge stone slabs that swallowed Hercules. */
        int v = o->v2a, sp = v & 0x1f, ang = (v & 0x800) ? 0x300 : 0x100;
        if (v & 0x100) { o->rot[2] = (int16_t)ang; o->d[0] = sp; }
        if (v & 0x200) o->d[1] = sp;
        if (v & 0x400) { o->rot[0] = (int16_t)ang; o->d[1] = sp; }
        break;
    }
    case I_POST:                                                    /* FUN_1003f040 */
        o->attr = 1; o->scale = 0x300; model(o); o->flags50 |= 0x100000;
        break;
    case I_GLOW:                                                    /* FUN_1003f070 */
        o->flags50 = 0x10000; model(o); o->frame = 0x40; o->z += 0x300000; o->x -= 0x200000;
        break;
    case I_SPR_A: case I_SPR_B: case I_SPR_C: case I_SPR_D:         /* Training sprite markers (bank 31 frame 0x7f) */
        o->frame = 0x7f;
        if (init == I_SPR_C) { o->y = 0x2f40000; o->z = 0x1000000; }
        break;
    case I_POLE_T:                                                  /* FUN_100422d0 */
        o->flags50 = 0x41; o->flags54 = 0;
        break;
    case I_POLE:                                                    /* FUN_100360a0 */
        o->flags50 = 0x41; o->frame = 0xb; model(o);
        break;
    case I_ST_32: case I_ST_3E:                                     /* FUN_1003ce10 / FUN_1003db90: platform stages */
        o->flags54 |= 0x10000080u; o->frame = 1; o->flags50 = 0x80040801u;
        if (init == I_ST_32) { o->y += 0x800000; o->d[0] = o->y - 0x280000 - 0x800000; o->d[1] = 0x40; o->d[2] = 0x80; o->home[1] = o->y; }
        else { o->timer = 0x2ff; o->d[0] = o->y; o->d[1] = 0x40; o->d[2] = 0x60; }
        o->b21 = (uint8_t)station(O, o->x);
        station_kids(O, o);
        if (init == I_ST_32 && o->used) {   /* FIX31: FUN_1003ce10 end: the risen stage comes back at its table y (entry +8) */
            uint16_t ty = training_tbl(O, o->b21, 4);
            if (ty) o->y = (int32_t)ty << 16; else training_tbl_set(O, o->b21, 4, (uint16_t)(o->y >> 16));
        }
        break;
    case I_ST_33: case I_ST_35: case I_ST_39:                       /* FUN_1003ca80 / FUN_1003d800 / FUN_1003d310 */
        if (init == I_ST_33) o->x -= 0x20000;
        if (init == I_ST_39) { o->flags54 = 0; o->frame = 0; }
        o->d[0] = o->y - 0x800000; o->d[1] = init == I_ST_39 ? 0x50 : 0x40; o->d[2] = init == I_ST_33 ? 0xa0 : init == I_ST_35 ? 0x60 : 0x80;
        o->b21 = (uint8_t)station(O, o->x);
        station_kids(O, o);
        break;
    default:
        return 0;
    }
    if (!o->used) return 1;
    o->init_fn = init;
    o->kind = K_SOLID;
    return 1;
}

/* ---------------------------------------------------------------- updates */

static void cannon_tick(Objects *O, Object *o)                     /* FUN_1003f1c0 */
{
    if (o->airborne) { drop_floor(O, o); goto offs; }
    if (o->hp > 0 && o->inv) o->inv--;
    if (o->v2a == 1) { o->rot[0] += 4; if (o->rot[0] > 0) { o->rot[0] = 0; o->v2a = 2; } }
    else if (o->v2a == 2) o->rot[1] = (int16_t)((o->rot[1] + o->d[1]) & 0x3ff);
    if (o->v2a == 0) goto offs;
    int h = took_hit(O, o);
    if (h) {
        int dmg = 0;
        if (h == 1) { if (fresh_blow(O)) dmg = player_hit_damage(P_, 0x5f) ? 1 : 1; }
        else dmg = 100;
        if (dmg) {
            o->hp -= (int16_t)dmg; o->flags50 &= ~0x200u;
            if (o->hp < 0) { snd(O, 0xc05a, o); dust(O, o, 3); obj_kill(O, o, 1); return; }
        }
    }
offs:
    if (offscreen(O, o, 0x200)) obj_kill(O, o, 0);
}

static void tile_tick(Objects *O, Object *o)                       /* FUN_10041ad0 / FUN_10041b70 */
{
    if (P_->dying) return;
    if (o->v2a == 1) {
        if (--o->cool < 0) {
            o->v2a = 2;
            if (o->tag) {
                snd(O, ((o->tag - 1) + 0x6a) | 0xc000, o); o->tag = 0;
                if (o->type == 0x4035) o->flags50 &= ~0x40000u;     /* FUN_10041ad0 only */
            }
        }
    } else if (o->v2a != 2) return;
    if (o->v2a == 2) {
        int r = phys(O, o);
        if ((r & 1) || I(o->y) > 0x4f0 || o->life++ > 120) { dust(O, o, 1); o->frame = 0; o->v2a = 4; }
    }
}

static int player_on(Objects *O, const Object *o)                  /* the player rides this platform (+0xe4 chain) */
{
    int b[6];
    if (!wbox(O, o, 1, b)) return 0;
    int X = I(P_->o.x), Y = I(P_->o.y), Z = I(P_->o.z);
    return X >= b[0] && X <= b[3] && Z >= b[2] && Z <= b[5] && iabs(Y - b[1]) <= 2;
}

static void bridge_tick(Objects *O, Object *o)                     /* FUN_100418a0 */
{
    if (o->airborne) { drop_floor(O, o); return; }
    if (P_->dying) return;
    int collapse = 0;
    if (o->v2a == 0) {
        int on = 0;
        for (int i = 0; i < o->nkids; i++) if (player_on(O, o->kid[i])) on = 1;
        if (!on) o->hp = 0x1e;
        else if (--o->hp < 0) { o->v2a = 1; collapse = 1; }
    } else collapse = 1;
    /* the collapse runs along the planks: a plank starts to go once the one before it is falling */
    int prev = collapse ? 2 : 0;
    for (int i = 0; i < o->nkids; i++) {
        Object *k = o->kid[i];
        if (k->v2a == 0 && prev == 2) k->v2a = 1;
        prev = k->v2a;
        if (k->v2a == 4) { remove_kid(o, k); obj_kill(O, k, 1); i--; }
    }
    if (o->nkids < 1) { obj_kill(O, o, 1); return; }
    if (iabs(I(o->x) - O->camx) > 0x3f1 + 0x200 && o->v2a == 0) obj_kill(O, o, 0);
}

static void stone_tick(Objects *O, Object *o)                      /* FUN_100365f0 */
{
    Object *par = o->parent;
    int gone = 0;
    if (!par) { obj_kill(O, o, 1); return; }
    if (o->v2a == 0) {
        int h = took_hit(O, o);
        if (h == 1) {
            if (fresh_blow(O)) {
                obj_sound(O, (P_->hitflags & 0x18) ? 0x10 : 0x24, P_->o.x);
                if (!power_blow(O)) {
                    if (O->L->index == 5) rubble(O, par, rnd(O, 6) + 6, 0x60, 0); else dust(O, par, 2);
                    snd(O, 0xd151, o);
                    if (--par->hp > 0) { snd(O, 0xd152, o); par->v2a = 1; return; }
                } else if (O->L->index == 5) rubble(O, par, rnd(O, 0xc) + 0xc, 0x80, 1); else dust(O, par, 5);
                snd(O, 0xd150, o);
                par->v2a = 2;
            }
        } else if (h == 2 && o->y == o->home[1]) {
            if (--par->hp < 1) { snd(O, 0xd150, o); par->v2a = 2; }
            else { snd(O, 0xd152, o); par->v2a = 1; }
        }
    } else {
        o->rot[0] += (int16_t)o->d[2]; o->rot[1] += (int16_t)o->d[1];
        if (phys(O, o) & 1) { snd(O, 0xd150, o); dust(O, o, 1); gone = 1; }
    }
    if (o->z < -0xb40000 || o->y > 0x5800000 || offscreen(O, o, 0x100)) gone = 1;
    if (gone) obj_kill(O, o, 1);
}

static void stack_tick(Objects *O, Object *o)                      /* FUN_100361a0 */
{
    if (o->v2a == 0) {
        for (int i = 0; i < o->nkids; i++) {
            Object *k = o->kid[i];
            if (k->y < k->home[1]) k->y += 0x10000;
        }
    } else if (o->v2a == 1) {
        for (int i = 0; i < o->nkids; i++) o->kid[i]->y -= 0x20000;
    } else if (o->v2a == 2) {
        o->flags50 |= 0;
        if (o->spawn >= 0 && o->spawn < O->nsp) O->sp[o->spawn].state = 2;     /* FUN_10027450 */
        dust(O, o, 3);
        for (int i = 0; i < o->nkids; i++) {
            Object *k = o->kid[i];
            k->v2a = 1;
            k->facing = P_->o.x < k->x ? 0 : 0xff;
            k->flags50 &= ~0x800u;
            k->home[1] = k->y;
        }
    }
    o->v2a = 0;
    if (o->nkids == 0) obj_kill(O, o, 1);
}

static void pot_tick(Objects *O, Object *o)                        /* FUN_1000fa00 */
{
    if (o->v2a == 3 || o->v2a == 9) o->flags50 &= ~0x200000u;
    if (o->v2a < 3) {
        if (iabs(I(o->x - P_->o.x)) < 0x40 && player_on(O, o) && (P_->hitflags & 0x20) && !(P_->consumed & 0x20)) {
            obj_spawn(O, 0x2027, P_->o.x, P_->o.y, P_->o.z);
            P_->consumed |= 0x20;
            obj_run_words(O, o);
            o->v2a = o->v2a ? o->v2a : 3;
            if (o->v2a == 3 || o->v2a == 0) {
                o->tag = 9;
                snd(O, 0xc05a, o);
                /* break into the pieces listed at +0x118 (0x2027 dust and 0x4023 shards) */
                const PeImage *pe = O->pe;
                uint32_t L = o->list;
                uint16_t type = L ? pe_u16(pe, L + 6) : 0;
                for (int i = 0; type && i < 16; i++) {
                    uint32_t e = L + 12u * (uint32_t)i;
                    O->pending_v2a = pe_s16(pe, e + 10); O->have_pending = 1;
                    Object *k = obj_spawn(O, type, o->x + FX(pe_s16(pe, e)), o->y + FX(pe_s16(pe, e + 2)), o->z + FX(pe_s16(pe, e + 4)));
                    O->have_pending = 0;
                    if (k) { k->spawn = -1; if ((type & 0x4000) && pe_s16(pe, e + 8)) k->frame = pe_s16(pe, e + 8); }
                    type = pe_u16(pe, e + 0x12);
                }
                if (o->spawn >= 0 && o->spawn < O->nsp) O->sp[o->spawn].state = 2;
                o->used = 0;
                return;
            }
        }
    } else if (o->v2a != 9 && (--o->hp < 0)) { obj_kill(O, o, 1); return; }
    if (offscreen(O, o, 0x140)) obj_kill(O, o, 0);
}

static void shard_tick(Objects *O, Object *o)                      /* FUN_1000fe60 */
{
    o->rot[0] += (int16_t)o->d[0]; o->rot[1] += (int16_t)o->d[2];
    o->y += 0x200000; o->py += 0x200000;
    int r = phys(O, o);
    o->y -= 0x200000; o->py -= 0x200000;
    if ((r & 1) || ++o->life > 150) { dust(O, o, 1); obj_kill(O, o, 1); }
}

static void fwall_tick(Objects *O, Object *o)                      /* FUN_1000a010 / FUN_1000a490 */
{
    static const int32_t SHAKE[6] = {0, 0x10000, -0x20000, 0x20000, -0x20000, 0x10000};   /* DAT_1004fc18 */
    if (o->init_fn == I_FWALL_B && iabs(I(P_->o.x - o->x)) > 0x9f) o->hp = 0x32;
    if (o->s98) { o->s98--; o->y += SHAKE[o->s98 % 6]; }
    int h = took_hit(O, o);
    if (!h) { obj_run_words(O, o); goto offs; }
    if (h == 1) {
        if (!fresh_blow(O)) goto offs;
        obj_sound(O, (P_->hitflags & 0x18) ? 0x10 : 0x24, P_->o.x);
        if (power_blow(O)) goto smash;
    }
    if (--o->hp < 1) {
smash:
        snd(O, 0xcf, NULL);
        dust(O, o, 5);
        P_->shake = 1;
        obj_kill(O, o, 1);
        return;
    }
    snd(O, 0xce, NULL);
    dust(O, o, 1);
    P_->hitflags = 0;
    o->s98 = 5;
offs:
    if (offscreen(O, o, 0x200)) obj_kill(O, o, 0);
}

static void block_tick(Objects *O, Object *o)                      /* FUN_10014850: lift / carry / throw */
{
    const PeImage *pe = O->pe;
    Player *P = P_;
    /* FX: the Thebes 0x05 rock (update FUN_100142d0) runs the same code on its own tables: pose x/rotations at
     * 0x100530d8, pose y at 0x10052c0a, carry height DAT_10052c0e, drop height DAT_100530de. Both add the pose-0
     * x offset (e70 / e72, d8 / c0a by facing) that the earlier port left out. */
    int rock = o->init_fn == I_ROCK5;
    /* LV_TRAINING: the Training/Forest 0x02 rock (update FUN_10013d70) has its own pose table at 0x10052c08 (x, y +2,
     * rotations +4/+6/+8, 10 bytes per pose) and carry/drop height DAT_10052c0e; the crate table made it float above his hands. */
    int rock2 = o->init_fn == I_ROCK2;
    uint32_t TX = rock ? 0x100530d8 : rock2 ? 0x10052c08 : 0x10052e70, TY = rock ? 0x10052c0a : rock2 ? 0x10052c0a : 0x10052e72;
    uint32_t HC = rock || rock2 ? 0x10052c0e : 0x10052e76, HD = rock ? 0x100530de : rock2 ? 0x10052c0e : 0x10052e76;
    o->flags54 &= ~0x30u;
    if (rock && getenv("THEBES_LOG")) printf("ROCK5 kind %d frame %d has88 %d box88 %d,%d,%d,%d,%d,%d attr %d pos %d,%d,%d s9a %d fl50 %x carried %d pose %d\n", o->kind, o->frame, o->has88, o->box88[0], o->box88[1], o->box88[2], o->box88[3], o->box88[4], o->box88[5], o->attr, I(o->x), I(o->y), I(o->z), o->s9a, o->flags50, P->carried == o, P->carry_pose);   /* LV_THEBES: test log */
    if (rock && o->s9a != 3 && o->s9a != 5) o->flags50 &= ~0x800000u;   /* LV_THEBES: FUN_100142d0 states 0/1 clear the "thrown" flag (DAT_10071be0 list) */
    if (o->airborne) { drop_floor(O, o); if (!o->airborne) o->y -= FX(pe_s16(pe, HD)); return; }
    int held = P->carried == o;
    if (held && o->s9a == 0) o->s9a = 1;
    if (o->s9a == 1) {
        int pose = P->carry_pose;
        if (!held && pose != 0x2f && pose != 0x30) { o->s9a = 0; drop_floor(O, o); o->y -= FX(pe_s16(pe, HD)); return; }
        if (pose == 0x2f) o->s9a = 2;
        else if (pose == 0x30) o->s9a = 4;
        else if (pose == 0) {
            drop_floor(O, o); o->y -= FX(pe_s16(pe, HD));
            return;
        } else {
            int left = P->o.facing != 0;
            int sel = left ? pe_s16(pe, TX) : pe_s16(pe, TY);
            o->y = FX(pe_s16(pe, TY + 10u * (uint32_t)pose)) - FX(pe_s16(pe, HC)) + P->o.y;
            o->rot[0] = pe_s16(pe, TX + 10u * (uint32_t)pose + 4); o->rot[1] = pe_s16(pe, TX + 10u * (uint32_t)pose + 6);
            o->rot[2] = pe_s16(pe, TX + 10u * (uint32_t)pose + 8);
            o->home[0] = o->x;
            if (!left) o->x = FX(pe_s16(pe, TX + 10u * (uint32_t)pose)) + FX(sel) + P->o.x;
            else {
                o->x = ~(pe_s16(pe, TX + 10u * (uint32_t)pose) << 16) + FX(sel) + P->o.x;
                o->rot[1] = (int16_t)-o->rot[1];
                if (!rock) o->rot[2] = (int16_t)-o->rot[2];
            }
            o->rot[1] = (int16_t)(o->rot[1] + (P->carry_ang >> 1));      /* PLAYER: DAT_1006ef9c */
            o->facing = P->o.facing;
            return;
        }
    }
    if (o->s9a == 2) {                                              /* thrown forward */
        o->zface = 0xff; o->b95 = 0xff; o->facing = P->o.facing;
        o->v[1] = o->v[0] = 0x40000; o->v[3] = 0x1000;
        o->v[5] = o->v[4] = 0x10000; o->v[7] = 0x6000; o->v[6] = 0;
        o->v[8] = o->v[9] = 0;
        o->s9a = 3;
        if (rock) o->flags50 |= 0x940000u;                          /* LV_THEBES: FUN_100142d0 state 2 */
    }
    if (o->s9a == 4 && rock) {                                      /* LV_THEBES: FUN_100142d0 state 4: thrown up, flagged 0x800000 (hurts the minotaur) */
        o->zface = 0; o->b95 = 0xff; o->facing = P->o.facing;
        o->flags50 = (o->flags50 & ~0x140000u) | 0x800000u;
        o->v[1] = o->v[0] = 0; o->v[5] = o->v[4] = 0xc0000; o->v[7] = 0x6000; o->v[6] = 0;
        o->v[9] = o->v[8] = 0x40000; o->v[3] = 0x6000; o->v[2] = 0;
        o->s9a = 5;
    }
    if (o->s9a == 5) {                                              /* LV_THEBES: state 5: drifts on towards the arena, shatters on landing */
        o->y += FX(pe_s16(pe, HD));
        if (o->x < 0x15600000) o->x += 0x20000;
        int r = phys(O, o);
        if (!(r & 1) && (o->z < 0xc00000 || o->y < 0x4400000)) {
            o->y -= FX(pe_s16(pe, HD));
            if (!o->b95) { o->v[5] = 0xa0000; o->v[6] = 0x40000; o->v[7] = 0; }
        } else { thebes_rock_shatter(O, o); obj_kill(O, o, 1); }
        return;
    }
    if (o->s9a == 4) {                                              /* thrown upwards / dropped */
        o->zface = 0; o->b95 = 0xff; o->facing = P->o.facing;
        o->v[1] = o->v[0] = 0; o->v[5] = o->v[4] = 0x10000; o->v[7] = 0x3000; o->v[6] = 0;
        o->v[8] = o->v[9] = 0x80000; o->v[3] = 0x2000; o->v[2] = 0;
        o->s9a = 3;
    }
    if (o->s9a == 3) {
        if (o->rot[2] < 0 && (o->rot[2] += 2) > 0) o->rot[2] = 0;
        if (o->rot[2] > 0 && (o->rot[2] -= 2) < 0) o->rot[2] = 0;
        o->y += FX(pe_s16(pe, HD));
        o->home[0] = o->x;
        int r = phys(O, o);
        if ((r & 1) && !o->cool) { snd(O, 0x4151, o); o->cool = 1; }
        if (r & 2) { o->s9a = 0; o->cool = 0; o->rot[0] = o->rot[1] = o->rot[2] = 0; }
        else if (!o->b95) { o->v[5] = 0xa0000; o->v[6] = 0x40000; o->v[7] = 0; }
        o->y -= FX(pe_s16(pe, HD));
        uint16_t f = obj_attr_flags(O, I(o->x) + (o->facing ? -0x20 : 0x20), I(o->y), I(o->z));
        if (f & 7) o->x = o->home[0];
    }
}

/* FUN_1003ecb0: Training gate. Phil's lesson for the station sets +0x2a = 3 and the gate slides back into
 * the scenery (z += 4 per tick up to +0xe0), then 9 = open. The lessons themselves (station updates
 * 0x1003cf90 / 0x1003cb90 / 0x1003d930 / 0x1003d440 / 0x1003dcd0 and their target actors) are not ported,
 * so the gate opens when Hercules reaches it instead. */
static void rack_tick(Objects *O, Object *o)
{
    if (o->airborne) { drop_floor(O, o); return; }
    if (o->v2a < 3 && iabs(I(P_->o.x - o->x)) < 0x60 && iabs(I(P_->o.z - o->z)) < 0x40) o->v2a = 3;
    int step = 0;
    if (o->v2a == 3) {
        if (o->z < o->d[2]) { step = -8; o->z += 0x40000; }
        else o->v2a = 9;
    }
    for (int i = 0; i < o->nkids && o->list; i++) {
        Object *k = o->kid[i];
        if (k->type == 0x202b) continue;
        uint32_t e = o->list + 12u * (uint32_t)k->kidx;
        k->x = o->x + FX(pe_s16(O->pe, e)); k->y = o->y + FX(pe_s16(O->pe, e + 2)); k->z = o->z + FX(pe_s16(O->pe, e + 4));
        if (k->type == 0x4031) k->s98 = (int16_t)(step * 4);
    }
    if (offscreen(O, o, 0x200) && iabs(I(o->x) - O->camx) > 0x300) obj_kill(O, o, 0);
}

/* FUN_1003eec0: Training weight on ropes; the station's lesson (+0x2a = 3) hoists it 0x80 units, then 9.
 * As with the gates, the lesson is not ported and the weight rises when Hercules reaches it. */
static void lift_tick(Objects *O, Object *o)
{
    if (o->airborne) { drop_floor(O, o); return; }
    if (o->v2a < 3 && iabs(I(P_->o.x - o->x)) < 0xa0 && iabs(I(P_->o.z - o->z)) < 0x60) o->v2a = 3;
    if (o->v2a == 3) {
        o->y -= 0x30000;
        if (o->y <= o->d[1]) { o->y = o->d[1]; o->v2a = 9; }
    }
    for (int i = 0; i < o->nkids && o->list; i++) {
        Object *k = o->kid[i];
        uint32_t e = o->list + 12u * (uint32_t)k->kidx;
        k->x = o->x + FX(pe_s16(O->pe, e)); k->y = o->y + FX(pe_s16(O->pe, e + 2)); k->z = o->z + FX(pe_s16(O->pe, e + 4));
    }
    if (offscreen(O, o, 0x200) && iabs(I(o->x) - O->camx) > 0x300) obj_kill(O, o, 0);
}

static void rubble_tick(Objects *O, Object *o)
{
    o->rot[0] += (int16_t)o->d[0]; o->rot[1] += (int16_t)o->d[2];
    if ((phys(O, o) & 1) || --o->life <= 0) obj_kill(O, o, 1);
}

static void decor_tick(Objects *O, Object *o)                      /* FUN_10040ab0 */
{
    (void)O;
    if (o->frame == 0x36) o->scale = 0x50;      /* FUN_10040ab0 */
    o->rot[0] += (int16_t)(o->d[0] * o->s98 >> 2);
    o->rot[1] += (int16_t)(o->d[1] * o->s98 >> 2);
    o->rot[2] += (int16_t)(o->d[2] * o->s98 >> 2);
}

/* ---------------------------------------------------------------- AUDIT: Training machines, doll chunks, archery range
 * Ports of FUN_10042090 (wheel update), FUN_10042220 / FUN_10042280 (the creaking sprite child 0x202c),
 * FUN_10042390 / FUN_10042540 (0x403f: the broken-doll chunk burst and its pieces), FUN_1003f460 / FUN_1003f8d0
 * (0x4a pop-up archery targets), FUN_1003f620 / FUN_1003f720 (fence 0x49 completion), FUN_100409b0 (0x4b prize
 * model), FUN_10041da0 / FUN_10041f00 (0x4037 target debris). */
enum {
    A_CHUNKS = 0x10042390, A_CHUNK = 0x10042540, A_CREAK = 0x10042220, A_TGT = 0x1003f460, A_PRIZE = 0x100409b0,
    A_DEBRIS = 0x10041f00, A_TOPPIECE = 0x100407b0, A_PIECE26 = 0x10040c90, A_ROPE = 0x1003eac0,
};
/* fence_done: DAT_10070008, archery fences already finished */
void audobj_reset(void) { memset(fence_done, 0, sizeof fence_done); }

static int onscreen_flag(Objects *O, const Object *o)               /* +0x1f & 2: drawn this frame (screen window of FUN_1002ae50) */
{
    int sx = 160 + I(o->x) - O->camx, sy = 128 + I(o->y) - O->cam_y;
    return sx > -0x21 && sx < 0x161 && sy > -0x21 && sy < 0x111;
}

static int offscreen_xy(Objects *O, const Object *o, int rx, int ry)  /* FUN_1002aa30 */
{
    int sx = 160 + I(o->x) - O->camx, sy = 128 + I(o->y) - O->cam_y;
    return sx + rx < 0 || sx - rx > 0x13f || sy + ry < 0 || sy - ry > 0xff;
}

static void debris_burst(Objects *O, const Object *src, int n, int small)      /* FUN_10041da0 (n = rnd(8)+8) / FUN_10041c30 (rnd(4)+4) */
{
    for (int i = 0; i < n; i++) {
        Object *p = obj_alloc(O);
        if (!p) return;
        p->kind = K_SOLID; p->init_fn = A_DEBRIS; p->type = 0x4037; p->bank = 0;
        p->flags54 |= 0x10000000u;
        p->x = p->px = src->x; p->y = p->py = src->y - (int32_t)(((unsigned)rnd(O, 0x10000) & 0x1f00) * 0x100); p->z = p->pz = src->z;
        p->facing = (uint8_t)-rnd(O, 2); p->zface = (uint8_t)-rnd(O, 2); p->b95 = 0xff;
        p->v[6] = 0; p->v[4] = p->v[5] = small ? (int32_t)(((rnd(O, 0x10000) & 0x1ff) + 0x800) * 0x100) : (int32_t)(((rnd(O, 0x10000) & 0x1ff) + 0x800) * 0x100);
        p->v[7] = rnd(O, 0x2000) + (small ? 0xe000 : 0x8000);
        int32_t r = (int32_t)(((rnd(O, 0x10000) & 0x1ff) + (small ? 0x100 : 0x300)) * 0x100);
        p->v[9] = r; p->v[8] = small ? r : r >> 1; p->v[10] = rnd(O, 0x1000) + (small ? 0x3000 : 0x6000); p->v[11] = 0;
        r = (int32_t)(((rnd(O, 0x10000) & 0x1ff) + (small ? 0x100 : 0x300)) * 0x100);
        p->v[1] = r; p->v[0] = small ? r : r >> 1; p->v[2] = rnd(O, 0x1000) + (small ? 0x3000 : 0x6000); p->v[3] = 0;
        p->frame = (int16_t)(rnd(O, 0xb) + 1);
    }
}

/* Player blow / projectile on a model object (box 1), FUN_10026de0. Returns 0 none, 1 blow, 2 projectile. */
static int audit_hit(Objects *O, Object *o)
{
    int h = took_hit(O, o);
    if (h == 1) { int d = en_blow_damage(O); if (d) en_hit_sound(O, 0x10); }
    return h;
}

static void chunk_burst(Objects *O, Object *src)                    /* FUN_10042390: the pieces of the broken doll */
{
    uint32_t sc, an, mv, in; int bank = 31;
    obj_type_info(O, 0x403f, &sc, &an, &mv, &bank, &in);
    for (int i = 0; i < 32; i++) {
        uint32_t e = 0x10065b98 + 12u * (uint32_t)i;
        if (!pe_s16(O->pe, e + 6)) break;
        Object *p = obj_alloc(O);
        if (!p) return;
        p->kind = K_SOLID; p->init_fn = A_CHUNK; p->type = 0x403f; p->bank = (uint8_t)bank; p->pairs = 0;
        p->x = p->px = src->x + FX(pe_s16(O->pe, e));
        p->y = p->py = src->y + FX(pe_s16(O->pe, e + 2)) + 0x400000;
        p->z = p->pz = src->z + FX(pe_s16(O->pe, e + 4));
        p->script = pe_u32(O->pe, 0x10067070 + 4u * (uint32_t)(uint16_t)pe_s16(O->pe, e + 8));
        p->idx = (int16_t)(rnd(O, 4) + 1); p->timer = 0;
        p->facing = (uint8_t)-rnd(O, 2); p->zface = 0xff; p->b95 = 0xff;
        p->v[6] = 0x2000;
        int32_t r = (int32_t)(((rnd(O, 0x10000) & 0x7ff) + 0x400) * 0x100);
        p->v[5] = p->v[4] = r;
        p->v[7] = rnd(O, 0x2000) + 0xa000;
        p->v[10] = 0;
        r = (int32_t)(((rnd(O, 0x10000) & 0x3ff) + 0x600) * 0x100);
        p->v[9] = r; p->v[8] = r >> 1;
        p->v[11] = rnd(O, 0x6000) + 0xf000;
        p->v[2] = 0;
        r = (int32_t)(((rnd(O, 0x10000) & 0x3ff) + 0x600) * 0x100);
        p->v[1] = p->v[0] = r;
        p->v[3] = rnd(O, 0x6000) + 0xf000;
        p->cool = rnd(O, 0x1e) + 0x1e;
        p->v2a = 0;
        p->flags54 = (p->flags54 & ~0x10u) | (p->facing & 0x10);
    }
}

static void chunk_tick(Objects *O, Object *o)                       /* FUN_10042540 */
{
    int32_t *v = o->v;
    if (o->v2a == 0) {
        obj_run_words(O, o);
        if (o->b95 == 0xff) { v[4] -= v[7]; if (v[4] < 0) v[4] = 0; }
        else { v[4] += v[6]; if (v[5] < v[4]) v[4] = v[5]; }
        v[0] += v[2] - v[3]; if (v[0] < 0) v[0] = 0; if (v[1] < v[0]) v[0] = v[1];
        v[8] += v[10] - v[11]; if (v[8] < 0) v[8] = 0; if (v[9] < v[8]) v[8] = v[9];
        o->x += o->facing ? -v[0] : v[0];
        o->y += o->b95 ? -v[4] : v[4];
        o->z += o->zface ? -v[8] : v[8];
        if (v[4] == 0) { o->b95 = 0; v[5] >>= 1; }
        if (o->b95 == 0 && o->y > 0x3000000 && land(O, o)) {
            o->v2a++; v[4] = 0; v[7] = 0; v[6] = 0; o->b95 = 0xff; v[2] = 0; v[3] = 0;
        }
    } else {
        if (o->cool == 0) { obj_kill(O, o, 1); return; }
        o->cool--;
    }
    if (offscreen_xy(O, o, 0x80, 0x80)) obj_kill(O, o, 1);
}

static void a_debris_tick(Objects *O, Object *o)                    /* FUN_10041f00 */
{
    o->rot[2] += 4;
    int r = phys(O, o);
    if ((r & 2) || offscreen_xy(O, o, 0x80, 0x80)) obj_kill(O, o, 1);
}

static void creak_tick(Objects *O, Object *o)                       /* FUN_10042280 */
{
    obj_run_words(O, o);
    if (o->cool-- < 0) {
        obj_sound(O, (rnd(O, 2) + 0xc3) | 0x4000, o->x);
        o->cool = rnd(O, 0x28) + 0x14;
    }
}

static void wheel_tick(Objects *O, Object *o)                       /* FUN_10042090: the Training wheel never stops turning */
{
    int32_t *v = o->v;
    v[0] += v[2] - v[3]; if (v[0] < 0) v[0] = 0; if (v[1] < v[0]) v[0] = v[1];
    int16_t s = (int16_t)(v[0] >> 16);
    o->rot[1] += s;
    Object *c = NULL;
    for (int i = 0; i < o->nkids; i++) if (o->kid[i] && o->kid[i]->init_fn == A_CREAK) c = o->kid[i];
    if (c) switch (c->v2a) {                      /* the child's +0x2a / +0x72 / +0x74 are scratch for a speed hiccup */
    case 0:
        if (v[0] == v[1] && rnd(O, 0x14) == 1) {
            v[0] = (int32_t)((((uint32_t)rnd(O, 0x10000)) & 0x3ff) << 8);
            c->v2a = 2; c->rot[1] = (int16_t)(((v[1] - v[0]) >> 16) << 5);
        }
        break;
    case 1: if (c->rot[1] < c->rot[2]) c->rot[2] -= 8; else c->v2a = 3; break;
    case 2: if (c->rot[2] < c->rot[1]) c->rot[2] += 6; else { c->rot[1] = (int16_t)-c->rot[1]; c->v2a = 1; } break;
    case 3: if (c->rot[2] < 0) c->rot[2] += 6; else { c->rot[2] = 0; c->v2a = 0; } break;
    }
    for (int i = 0; i < o->nkids; i++) if (o->kid[i] && o->kid[i]->type == 0x4031) o->kid[i]->s98 = s;
    if (offscreen(O, o, 0x200)) obj_kill(O, o, 0);
}

static void target_tick(Objects *O, Object *o)                      /* FUN_1003f8d0: 0x4a pop-up archery target */
{
    if (o->airborne) { drop_floor(O, o); return; }
    int vis = onscreen_flag(O, o);
    if (o->v2a == 0) {
        if (o->rot[0] > 0) { o->rot[0] -= 4; if (o->rot[0] < -0x40) o->rot[0] = (int16_t)0xff00; }
        if (vis) { o->v2a = 1; obj_sound(O, 0x4056, o->x); }
    } else if (o->v2a == 1) {
        o->rot[0] += 4;
        if (o->rot[0] > 0) { o->rot[0] = 0; o->v2a = 2; }
    } else if (o->v2a == 9) {
        if (o->hp == 0) o->frame = 0; else o->hp--;
    }
    if (o->v2a > 8) return;
    if (!vis) o->v2a = 0;
    if (o->rot[0] < -0xc0) return;
    int h = audit_hit(O, o);
    if (h) {
        o->hp = (int16_t)(h == 2 ? -100 : -1);
        o->flags50 &= ~0x200u;
        obj_sound(O, 0xc05a, o->x);
        debris_burst(O, o, rnd(O, 8) + 8, 0);
        o->v2a = 9; o->frame = 0;
    }
}

static void fence_tick(Objects *O, Object *o)                       /* FUN_1003f720 */
{
    int tag = o->tag & 7;
    if (!fence_done[tag]) {
        int total = 0, done = 0;
        for (int i = 0; i < o->nkids; i++) {
            Object *k = o->kid[i];
            if (!k || !k->used || k->type != 0x4a) continue;
            if (getenv("FENCE_TEST")) { k->v2a = 9; k->frame = 0; }   /* test hook: every target beaten */
            total++; if (k->v2a == 9) done++;
        }
        if (total && total == done) {
            int n = 0; for (int i = 0; i < 5; i++) n += fence_done[i];
            for (int i = 0; i < o->nkids; i++) {
                Object *k = o->kid[i];
                if (!k || !k->used || k->type != 0x4b || k->v2a) continue;
                k->v2a = 1;                                          /* FUN_100292f0(kid, 1) */
                for (int j = 0; j < 4; j++) en_drop_pickup(O, k, (int)pe_u32(O->pe, 0x10066ea0 + 16u * (uint32_t)(n > 3 ? 3 : n) + 4u * (uint32_t)j));
            }
            fence_done[tag] = 1;
        }
    }
    if (offscreen(O, o, 0x200)) obj_kill(O, o, 0);
}

/* FUN_1002c090: direction of movement as a 0x400-per-turn angle (the table value atan(|dy| / |dx|) is 0x100 per quarter turn). */
static int move_angle(int32_t x, int32_t y, int32_t px, int32_t py)
{
    int dx = (int)(((uint32_t)(x > px ? x - px : px - x)) >> 16) & 0xffff;
    int c = px < x, dy;
    if (py < y) dy = y - py; else { dy = py - y; c += 2; }
    dy >>= 16;
    int t = dx == 0 ? 0x100 : (int)(atan2((double)dy, (double)dx) * (180.0 / 3.14159265358979) * 65536.0 / 90.0) >> 8;
    switch (c) {
    case 0: return 0xff - t;
    case 1: return t + 0x300;
    case 2: return t + 0x100;
    default: return 0x2ff - t;
    }
}

static void toppiece_tick(Objects *O, Object *o)                    /* FUN_100408b0: spinning top pieces (0x403c) */
{
    phys(O, o);
    int dx = o->facing ? -8 : 8;
    if (obj_attr_flags(O, I(o->x) + dx, I(o->y), I(o->z)) & 7) o->facing = (uint8_t)~o->facing;
    if (o->v[0] < 0x10000) {
        if (rnd(O, 0x800) < 0x100) o->scale = (uint16_t)(o->scale - 0x10);
        if ((int16_t)o->scale < 0x20) { obj_kill(O, o, 1); return; }
    } else o->rot[2] = (int16_t)(o->rot[2] + (o->facing ? -8 : 0));
}

static void piece26_tick(Objects *O, Object *o)                     /* FUN_10040de0: small tumbling bits */
{
    obj_run_words(O, o);
    if (!o->used) return;
    if ((phys(O, o) & 1) || offscreen_xy(O, o, 0x20, 0x20)) obj_kill(O, o, 1);
}

static int audit_init(Objects *O, Object *o, uint32_t init)
{
    switch (init) {
    case A_TOPPIECE:                                                /* FUN_100407b0 */
        o->facing = o->v2a == 0; o->zface = (uint8_t)-rnd(O, 2); o->b95 = 0xff;
        o->v[6] = 0; o->v[5] = o->v[4] = ((rnd(O, 0x10000) & 0x1ff) + 0x800) * 0x100;
        o->v[7] = rnd(O, 0x2000) + 0x8000;
        o->v[9] = ((rnd(O, 0x10000) & 0x7f) + 0x80) * 0x100; o->v[8] = o->v[9] >> 1;
        o->v[10] = rnd(O, 0x1000) + 0x1000; o->v[11] = 0;
        o->v[1] = ((rnd(O, 0x10000) & 0x3ff) + 0x300) * 0x100; o->v[0] = o->v[1] >> 1;
        o->v[2] = rnd(O, 0x4000) + 0x1000; o->v[3] = 0;
        o->flags54 = (o->flags54 & ~0x10u) | (o->facing & 0x10);
        return 1;
    case A_PIECE26:                                                 /* FUN_10040c90 */
        o->facing = (uint8_t)-rnd(O, 2); o->zface = (uint8_t)-rnd(O, 2); o->b95 = 0xff;
        o->v[6] = 0; o->v[5] = o->v[4] = ((rnd(O, 0x10000) & 0x1ff) + 0x800) * 0x100;
        o->scale = 0x80; o->v[7] = rnd(O, 0x2000) + 0x8000;
        o->v[9] = ((rnd(O, 0x10000) & 0x1ff) + 0x300) * 0x100; o->v[8] = o->v[9] >> 1;
        o->v[10] = rnd(O, 0x1000) + 0x6000; o->v[11] = 0;
        o->v[1] = ((rnd(O, 0x10000) & 0x1ff) + 0x300) * 0x100; o->v[0] = o->v[1] >> 1;
        o->v[2] = rnd(O, 0x1000) + 0x6000; o->v[3] = 0;
        o->rot[2] = (int16_t)rnd(O, 0x3ff);
        o->script = rnd(O, 2) == 0 ? 0x100670c0 : 0x100670a8;
        o->idx = (int16_t)rnd(O, 7); o->timer = 0; o->pairs = 0;
        obj_run_words(O, o);
        o->flags54 = (o->flags54 & ~0x10u) | (o->facing & 0x10);
        return 1;
    case A_CHUNKS: chunk_burst(O, o); return 0;                     /* the spawner leaves nothing behind */
    case A_CREAK:
        o->z -= 0x100000; o->flags50 = 0x10000; o->flags54 |= 0x10;
        o->bright = (int16_t)(rnd(O, 0x80) + 0x60); o->v2a = 0;
        o->idx = 0; o->timer = 0; o->pairs = 0;
        o->cool = rnd(O, 0x28) + 0x14;
        return 1;
    case A_TGT:
        if (o->type != 0x4a) return 0;
        o->flags54 |= 0x10000000u; o->flags50 |= 0x100200; o->attr = 2; o->frame = 4;
        drop_floor(O, o); o->v2a = 0; o->hp = 0; o->rot[0] = (int16_t)0xff00;
        return 1;
    case A_PRIZE:
        if (o->type != 0x4b) return 0;
        o->flags54 |= 0x10000000u; o->frame = 6;
        drop_floor(O, o);
        return 1;
    }
    return 0;
}

static int audit_tick(Objects *O, Object *o)
{
    switch (o->init_fn) {
    case A_CHUNK: chunk_tick(O, o); return 1;
    case A_DEBRIS: a_debris_tick(O, o); return 1;
    case A_CREAK: creak_tick(O, o); return 1;
    case A_TOPPIECE: toppiece_tick(O, o); return 1;
    case A_PIECE26: piece26_tick(O, o); return 1;
    case A_ROPE:                                                    /* FUN_1003eaf0: the rope end points along its movement */
        if (o->type == 0x4033) { o->rot[2] = (int16_t)move_angle(o->x, o->y, o->px, o->py); return 1; }
        return 0;
    case I_T_WHEEL: wheel_tick(O, o); return 1;
    case A_TGT: if (o->type == 0x4a) { target_tick(O, o); return 1; } return 0;
    case A_PRIZE: if (o->type == 0x4b) { if (offscreen(O, o, 0x200) && !o->parent) obj_kill(O, o, 0); return 1; } return 0;
    case I_T_FENCE: fence_tick(O, o); return 1;
    }
    return 0;
}

void solids_tick(Objects *O, Object *o)
{
    if (audit_tick(O, o)) return;            /* AUDIT */
    if (enemies3_solid_tick(O, o)) return;   /* ENEMIES3: Training lessons, gate, weight, cannon (training.c) */
    if (!o->home[1] && !o->home[0]) { o->home[0] = o->x; o->home[1] = o->y; o->home[2] = o->z; }
    switch (o->init_fn) {
    case I_T_CANNON: cannon_tick(O, o); break;
    case I_T_TILE: tile_tick(O, o); break;
    case I_T_BRIDGE: bridge_tick(O, o); break;
    case I_STONE: case I_STONE_T: stone_tick(O, o); break;
    case I_STACK: case I_STACK_T: stack_tick(O, o); break;
    case I_POT: pot_tick(O, o); break;
    case I_SHARD: shard_tick(O, o); break;
    case I_FWALL_A: case I_FWALL_B: fwall_tick(O, o); break;
    case I_BLOCK: case I_ROCK5: case I_ROCK2: block_tick(O, o); break;   /* LEFTOVERS */
    case I_RUBBLE: rubble_tick(O, o); break;
    case I_DECOR: decor_tick(O, o); break;
    case I_T_RACK: rack_tick(O, o); break;
    case I_T_LIFT: lift_tick(O, o); break;
    default:
        if (!o->parent && offscreen(O, o, 0x200) && iabs(I(o->x) - O->camx) > 0x300) obj_kill(O, o, 0);
        break;
    }
}

/* ---------------------------------------------------------------- player hooks */

static int in_list(const Object *o, uint32_t bit) { return o->used && o->kind == K_SOLID && (o->flags50 & bit) && !o->carried; }

int solids_attr(Objects *O, int x, int y, int z)                   /* FUN_1002a170 over DAT_10071b20 */
{
    for (int i = 0; i < OBJ_MAX; i++) {
        Object *o = &O->obj[i];
        if (!in_list(o, 0x100000) || (O->P && O->P->carried == o)) continue;
        int b[6];
        if (!wbox(O, o, 0, b)) continue;
        if (x >= b[0] && x <= b[3] && y >= b[1] && y <= b[4] && z >= b[2] && z <= b[5]) { O->hit_obj = o; return o->attr; }
    }
    O->hit_obj = NULL;
    return -1;
}

int solids_floor(void *ctx, int x, int y, int z, int *up, int *down) /* FUN_100296e0 over DAT_100714c0 */
{
    Objects *O = ctx;
    *up = *down = 0x7fff;
    int found = 0;
    for (int i = 0; i < OBJ_MAX; i++) {
        Object *o = &O->obj[i];
        if (!in_list(o, 0x40000) || (O->P && O->P->carried == o)) continue;
        int b[6];
        if (!wbox(O, o, 1, b)) continue;
        if (x < b[0] || x > b[3] || z < b[2] || z > b[5]) continue;
        int diff = b[1] - y;
        if (diff >= 0) { if (diff < *down) *down = diff; }
        else if (-diff < *up && -diff <= b[4] - b[1]) *up = -diff;
        found = 1;
    }
    return found;
}

void solids_pre(Objects *O, Player *P)
{
    (void)O;
    power_peak = P->state == 0x2a ? (P->charge > power_peak ? P->charge : power_peak) : 0;
}

void solids_frame(Objects *O, Player *P)
{
    if (getenv("SOLIDS_TALLY") && O->scroll_show) {
        fprintf(stderr, "scroll %d collected: bits %04x coins %d/%d %d/%d vases %x\n", O->scroll_last, O->scroll_bits,
                O->coins18, O->coins18_total, O->coins14, O->coins14_total, O->letters);
        O->scroll_show = 0;
    }
    /* FUN_10019890 & co: the free-space probe ahead of Hercules hit a solid object -> DAT_1007210c. */
    if (!P->carrying && (P->pad & (P->o.facing ? PAD_LEFT : PAD_RIGHT))) {   /* only while pushing towards it */
        int ahead = P->o.facing ? -0x24 : 0x24;     /* FUN_10019900 / FUN_10019820: idle probe distance */
        if (solids_attr(O, I(P->o.x) + ahead, I(P->o.y) - 8, I(P->o.z)) >= 0 && O->hit_obj) {
            Object *t = O->hit_obj;
            int ty = t->type;
            if (ty == 2 || ty == 4 || ty == 5) { P->touch_obj = t; P->touch_type = ty; }
        }
    }
    for (int i = 0; i < OBJ_MAX; i++) O->obj[i].carried = O->obj[i].used && P->carried == &O->obj[i];
    /* DAT_10071a20: objects with flags50 & 0x40 are the swing poles FUN_10017810 looks for */
    static int32_t poles[32][3];
    int np = 0;
    for (int i = 0; i < OBJ_MAX && np < 32; i++) {
        const Object *o = &O->obj[i];
        if (o->used && (o->flags50 & 0x40)) { poles[np][0] = o->x; poles[np][1] = o->y; poles[np][2] = o->z; np++; }
    }
    P->poles = (const int32_t (*)[3])poles; P->npoles = np;
    /* riders: platforms that moved carry Hercules with them */
    for (int i = 0; i < OBJ_MAX; i++) {
        Object *o = &O->obj[i];
        if (!in_list(o, 0x40000) || (o->x == o->px && o->y == o->py)) continue;
        int b[6];
        if (!wbox(O, o, 1, b)) continue;
        int top0 = b[1] - I(o->y - o->py);
        int X = I(P->o.x), Y = I(P->o.y), Z = I(P->o.z);
        if (X >= b[0] && X <= b[3] && Z >= b[2] && Z <= b[5] && iabs(Y - top0) <= 2) {
            P->o.x += o->x - o->px; P->o.y += o->y - o->py;
        }
    }
}

/* DEPTH audit (env SOLID_AUDIT): drawn extent of every live model object vs its collision box. */
void solids_audit(Objects *O, Renderer *R)
{
    for (int i = 0; i < OBJ_MAX; i++) {
        Object *o = &O->obj[i];
        if (!o->used || o->kind != K_SOLID || o->bank != 0 || !(o->flags54 & 0x10000000u)) continue;
        audit_mn[0] = audit_mn[1] = audit_mn[2] = 1e9f; audit_mx[0] = audit_mx[1] = audit_mx[2] = -1e9f;
        audit_on = 1; solids_draw_reset(); solids_draw(O, R, o); audit_on = 0;
        int b[6], have = wbox(O, o, 0, b);
        printf("AUDIT type %04x frame %d scale %d at %d,%d,%d flags50 %x attr %d  drawn x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f  box %s",
               o->type, o->frame, o->scale, I(o->x), I(o->y), I(o->z), o->flags50, o->attr, audit_mn[0], audit_mx[0], audit_mn[1], audit_mx[1], audit_mn[2], audit_mx[2], have ? "" : "none\n");
        if (have) printf("x %d..%d y %d..%d z %d..%d\n", b[0], b[3], b[1], b[4], b[2], b[5]);
    }
}
