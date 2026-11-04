/* BOSSES: stand-in for the SOLIDS agent's model renderer (work/solids/src/solids.c, solids_draw), used by the
 * boss fights for their 3D map-model parts (Medusa's gaze beams, columns and falling rocks; the Hydra's
 * body). Defined weak: when solids.c is merged its solids_draw() replaces this copy. The code is the
 * SOLIDS port of FUN_10031540 (vertex groups + face groups of a map group-B model record). */
#include "objects_int.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static inline uint32_t mu32(const uint8_t *d, uint32_t a) { return d[a] | d[a+1] << 8 | d[a+2] << 16 | (uint32_t)d[a+3] << 24; }
static inline uint16_t mu16(const uint8_t *d, uint32_t a) { return (uint16_t)(d[a] | d[a+1] << 8); }
static inline int16_t ms16(const uint8_t *d, uint32_t a) { return (int16_t)mu16(d, a); }

uint32_t bosses_model_rec(const Level *L, int idx)
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

#define POOL 12000
static Face pool[POOL];
static int npool;

void bosses_draw_begin(void) { npool = 0; }

static void rotmat(const int16_t rot[3], float m[3][3])
{
    float ax = rot[0] * (6.2831853f / 1024.0f), ay = rot[1] * (6.2831853f / 1024.0f), az = rot[2] * (6.2831853f / 1024.0f);
    float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay), cz = cosf(az), sz = sinf(az);
    m[0][0] = cy * cz;                m[0][1] = -cy * sz;               m[0][2] = sy;
    m[1][0] = sx * sy * cz + cx * sz; m[1][1] = -sx * sy * sz + cx * cz; m[1][2] = -sx * cy;
    m[2][0] = -cx * sy * cz + sx * sz; m[2][1] = cx * sy * sz + sx * cz; m[2][2] = cx * cy;
}

/* FX2: vertex transforms of FUN_10031540 (table 0x1005cec0):
 *   3 FUN_10031a70 / 6 FUN_10031bc0 - morph: every packed component lerps toward the matching vertex of model
 *     +0x36 (group by group) by weight +0x4c / 256;
 *   2 FUN_10031990 - chain weld: for each pair (a, b) of the list at +0x5c, vertex b takes the previous chain
 *     member's vertex a and vertex a becomes this member's own (neck segments, Medusa's gaze rays);
 *   5 FUN_10031a20 - vertices taken from the shared table (here: the chain buffer, which the rig fills).
 * The PSX version welds projected screen vertices; this port welds the world positions, same effect. */
static void model_draw(Objects *O, Renderer *R, const Object *o, int xf, uint32_t wlist, Vec3 *W, int *wvalid);

void boss_model_draw(Objects *O, Renderer *R, const Object *o)   /* MERGE: own copy (rigid placement, winding fix, big-face split) */
{
    model_draw(O, R, o, o->b23, 0, NULL, NULL);
}

void boss_model_chain(Objects *O, Renderer *R, const Object *o, int xf, uint32_t wlist, Vec3 *W, int *wvalid)   /* FX2 */
{
    model_draw(O, R, o, xf, wlist, W, wvalid);
}

static void model_draw(Objects *O, Renderer *R, const Object *o, int xf, uint32_t wlist, Vec3 *W, int *wvalid)
{
    const Level *L = O->L;
    const uint8_t *d = L->d;
    uint32_t r = bosses_model_rec(L, o->frame);
    if (!r) return;
    float m[3][3];
    rotmat(o->rot, m);
    float s = (o->scale ? o->scale : 0x100) / 256.0f;
    int ox = 0, oy = 0, oz = 0;
    uint32_t off = mu32(d, r + 4);
    if (off) { ox = (int8_t)(off & 0xff); oy = (int16_t)((off >> 8) & 0xffff); oz = (int8_t)(off >> 24); }
    float OX = o->x / 65536.0f, OY = o->y / 65536.0f, OZ = o->z / 65536.0f;
    /* Rigid placement (FUN_10032130): the model's origin goes to its path position and the model is turned to
     * the path direction there; its vertices are not bent along the path. This matters in the ring arenas,
     * whose floors are single models at the ring centre. */
    float WX, WZ, Rx, Rz, Fx, Fz;
    level_path_to_world(L, OX, OZ, &WX, &WZ, NULL);
    if (R) render_model_begin(R, WX, OY, WZ);   /* RENDERFIX: model shade from the object depth */
    level_path_frame(L, OX, OZ < 300 ? OZ : 300, &Rx, &Rz, &Fx, &Fz);
    uint32_t texbase = 0x28 + (mu32(d, 0x28) & ~3u);
    uint32_t facebase = 0x24 + (mu32(d, 0x24) & ~3u);
    int nbox = d[r + 2];
    uint32_t g = r + 10 + 8u * (uint32_t)nbox;
    uint32_t fd = facebase + 4u * (uint32_t)mu16(d, r);
    uint32_t tg = 0;                                        /* FX2: morph target group */
    if ((xf == 3 || xf == 6) && o->morph_w && o->morph_model > 0) {
        uint32_t tr = bosses_model_rec(L, o->morph_model);
        if (tr) tg = tr + 10 + 8u * (uint32_t)d[tr + 2];
    }
    int mw = o->morph_w;
    for (int grp = 0; grp < 32; grp++) {
        if (g + 2 > L->size || fd + 4 > L->size) return;
        int nv = ms16(d, g);
        if (nv <= 0 || nv > 64 || g + 2 + 4u * nv > L->size) return;
        Vec3 V[64];
        int tnv = tg && tg + 2 <= L->size ? ms16(d, tg) : 0;
        for (int k = 0; k < nv; k++) {
            uint32_t v = mu32(d, g + 2 + 4u * k);
            float vx = (float)(int16_t)((v >> 8) & 0xffff), vy = (float)(int8_t)(v >> 24), vz = (float)(int8_t)(v & 0xff);
            if (k < tnv && tg + 6 + 4u * k <= L->size) {   /* FX2: FUN_10031a70 lerp of the packed fields */
                uint32_t t = mu32(d, tg + 2 + 4u * k);
                int sx = (int16_t)((v >> 8) & 0xffff), sy = (int8_t)(v >> 24), sz = (int8_t)(v & 0xff);
                int ex = (int16_t)((t >> 8) & 0xffff), ey = (int8_t)(t >> 24), ez = (int8_t)(t & 0xff);
                vx = (float)(((ex - sx) * mw >> 8) + sx); vy = (float)(((ey - sy) * mw >> 8) + sy); vz = (float)(((ez - sz) * mw >> 8) + sz);
            }
            if (xf == 4 || xf == 7) { vx *= 2; vy *= 2; vz *= 2; }   /* FX3: FUN_10032a30 / FUN_10032aa0 unpack x2 */
            if (xf == 1 && (o->flags54 & 0x2000000u)) {         /* FX3: FUN_10032920 ring bend: angle = bits 8-23 + 0x400,
                                                                 * radius = (s8 bits 0-7) - 0x28c, about the ring centre */
                int pa = (int)((v >> 8) & 0xffff) + 0x400;
                float rr = (float)((int8_t)(v & 0xff) - 0x28c), an = pa * (float)(2 * M_PI / 4096);
                vx = sinf(an) * rr; vz = sinf(an - (float)(M_PI / 2)) * rr;   /* x = T[u], z = T[u - 0x400] (sine table) */
            }
            float rx = m[0][0] * vx + m[0][1] * vy + m[0][2] * vz;
            float ry = m[1][0] * vx + m[1][1] * vy + m[1][2] * vz;
            float rz = m[2][0] * vx + m[2][1] * vy + m[2][2] * vz;
            float lx = -(rx * s + ox), lz = rz * s + oz;
            V[k].x = WX + lx * Rx + lz * Fx;
            V[k].z = WZ + lx * Rz + lz * Fz;
            V[k].y = OY + ry * s + oy;
        }
        if (grp == 0 && W && wvalid) {                      /* FX2: transforms 2 / 5 */
            const uint8_t *lst = wlist ? pe_ptr(O->pe, wlist, 2) : NULL;
            int cnt = lst ? lst[0] : 0;
            lst = cnt ? pe_ptr(O->pe, wlist, 2 + 2u * (uint32_t)cnt) : NULL;
            if (xf == 2 && lst) {
                for (int q = 0; q < cnt; q++) {
                    int a2 = lst[1 + 2 * q], b2 = lst[2 + 2 * q];
                    if (a2 >= nv || b2 >= nv) continue;
                    if (*wvalid) V[b2] = W[a2];
                    W[a2] = V[a2];
                }
                *wvalid = 1;
            } else if (xf == 5 || xf == 0 || !lst) {          /* rig / chain start: this member's vertices seed the chain */
                for (int k = 0; k < nv; k++) W[k] = V[k];
                *wvalid = 1;
            }
            if (!R) return;                                  /* rig: transform only, no faces (FUN_10031470) */
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
            /* x is mirrored (model x runs against the path), so swap left/right corners to keep the winding */
            F->p[0] = V[i1]; F->p[1] = V[i0]; F->p[2] = V[i2]; F->p[3] = V[i3];
            F->org.x = (V[i0].x + V[i2].x) * 0.5f; F->org.y = (V[i0].y + V[i2].y) * 0.5f; F->org.z = (V[i0].z + V[i2].z) * 0.5f;
            F->flags = 0; F->otb = 0; F->mirror = 0;
            if (f1 & 0xff000000u) {
                for (int q = 0; q < 4; q++) { F->uv[q][0] = (uint8_t)f1; F->uv[q][1] = 0xff; }
                F->tpage = 0x9c; F->flags = 0x80;
            } else {
                uint32_t t = texbase + f1;
                if (t + 12 > L->size) continue;
                uint32_t e0 = mu32(d, t), e1 = mu32(d, t + 4), e2 = mu32(d, t + 8);
                F->uv[1][0] = e0 & 0xff; F->uv[1][1] = (e0 >> 8) & 0xff;
                F->uv[0][0] = e1 & 0xff; F->uv[0][1] = (e1 >> 8) & 0xff;
                F->uv[3][0] = e2 & 0xff; F->uv[3][1] = (e2 >> 8) & 0xff;
                F->uv[2][0] = (e2 >> 16) & 0xff; F->uv[2][1] = (e2 >> 24) & 0xff;
                F->tpage = (uint16_t)(e1 >> 16);
                if ((fb & 0x20)) F->flags = 0x20;
            }
            if (o->flags54 & 4) {                           /* FX2: object semi-transparency (FUN_100311a0: code 0x2e, abr = flags54 & 3) */
                F->flags |= 0x20; F->tpage = (uint16_t)((F->tpage & ~0x60) | ((o->flags54 & 3) << 5));
            }
            int fcull = getenv("BOSS_NOCULL") ? 0 : cull;
            float ext = fabsf(F->p[0].x - F->p[3].x) + fabsf(F->p[0].z - F->p[3].z) + fabsf(F->p[0].y - F->p[3].y);
            if (ext < 96 || npool + 17 >= POOL) { npool++; render_face(R, F, fcull); continue; }
            /* Big faces (the arena floors are models scaled x4): split 4x4 so the part in front of the camera is
             * not dropped by the renderer's near-plane rejection (the PSX version subdivides large polygons too). */
            Face Q = *F;
            for (int a = 0; a < 4; a++) for (int b2 = 0; b2 < 4; b2++) {
                Face *G = &pool[npool++];
                *G = Q;
                float u0 = a / 4.f, u1 = (a + 1) / 4.f, v0 = b2 / 4.f, v1 = (b2 + 1) / 4.f;
                float uu[4] = {u0, u1, u0, u1}, vv[4] = {v0, v0, v1, v1};
                for (int q = 0; q < 4; q++) {
                    float u = uu[q], v = vv[q];
                    #define BL(f) ((1-u)*(1-v)*f[0] + u*(1-v)*f[1] + (1-u)*v*f[2] + u*v*f[3])
                    float px[4] = {Q.p[0].x, Q.p[1].x, Q.p[2].x, Q.p[3].x}, py[4] = {Q.p[0].y, Q.p[1].y, Q.p[2].y, Q.p[3].y};
                    float pz[4] = {Q.p[0].z, Q.p[1].z, Q.p[2].z, Q.p[3].z};
                    float tu[4] = {Q.uv[0][0], Q.uv[1][0], Q.uv[2][0], Q.uv[3][0]}, tv[4] = {Q.uv[0][1], Q.uv[1][1], Q.uv[2][1], Q.uv[3][1]};
                    G->p[q].x = BL(px); G->p[q].y = BL(py); G->p[q].z = BL(pz);
                    G->uv[q][0] = (uint8_t)(BL(tu) + 0.5f); G->uv[q][1] = (uint8_t)(BL(tv) + 0.5f);
                    #undef BL
                }
                G->org.x = (G->p[0].x + G->p[3].x) * 0.5f; G->org.y = (G->p[0].y + G->p[3].y) * 0.5f; G->org.z = (G->p[0].z + G->p[3].z) * 0.5f;
                render_face(R, G, fcull);
            }
        }
        g += 4u + 4u * (uint32_t)nv;
        if ((fl & 0xffff) == 0) return;
        uint32_t p = f + (uint32_t)((fl >> 8) & 0xff);
        fd = (p + 3) & ~3u;
        if (!(fl & 0xff)) return;
    }
}
