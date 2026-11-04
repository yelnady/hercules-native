/* PLAYER: ENG1 camera follow (FUN_1002fb70) and its height bias easing (FUN_10026730 / FUN_1003ca50).
 * The original steps the camera towards the player twice per tick (DAT_10070cfc+1 = 2 iterations) by an
 * amount looked up from a table of error thresholds (0x1005e058 / 0x1005e080 / 0x1005e0a8, all the same):
 *   x:  err = (px - camx + 0xa0) * 2 - 0x140 -/+ look-ahead (0x3c, Hydra 0x20)   [look ahead 30 in the facing direction]
 *   y:  err = (py - camy + ce0) * 2 - 0x168 + bias (DAT_10070d04); with the player above 0x20 / below 0xd8 on screen the
 *       error is amplified up to 2x (flag 0x40); camy limited to [0x7c, rows * 64 - 64]
 *   z:  err = ((pz - camz) + dz - 0x140) * 2 (dz = Hades zoom DAT_10070d18)
 * x limits: [0xa0, columns * 64 - 0xa0] (DAT_10070ce8 / DAT_10070cec). */
#include "game.h"
#include "objects_int.h"
#include <stdlib.h>

int training_cam_station(void);     /* training.c: DAT_10070f0c, station under Hercules */

static const uint16_t STEP[17] = { 0, 20, 39, 57, 74, 90, 106, 123, 140, 158, 176, 195, 214, 234, 254, 296, 65535 };

static int step_for(int err)
{
    int a = abs(err) & 0xffff, k = 0;
    while (STEP[k] < a) k++;
    return k;
}

static int level_bias(const Level *L)       /* DAT_10070d04 at level start */
{
    if (L->index == 1) return -2;           /* FUN_1003ca10 */
    if (L->index == 6) return -0x60;        /* FUN_1001ac10 */
    return -0x40;                           /* FUN_10015090 (0xffc0) */
}

void game_camera_init(Game *G, int rowx, int rowy, int zstrip)
{
    G->camx = (float)rowx;
    G->camy = (float)rowy;
    G->camz = (float)(zstrip * 128 - 320);
    G->cam_d04 = level_bias(&G->L);
}

void game_camera_follow(Game *G)
{
    const Level *L = &G->L;
    int idx = L->index;
    int flags = (idx == 6 || idx == 7) ? 0x1f : 0x5e;
    int ce0 = idx == 7 ? 0x60 : idx == 6 ? 0xa0 : 0x80;
    int d02 = idx == 6 ? 0x20 : 0x3c;
    int d0c = idx == 6 ? 0 : 0x20, d0e = idx == 6 ? 0xe0 : 0xd8;
    const Obj *o = &G->P.o;
    int px = (int16_t)(o->x >> 16), py = (int16_t)(o->y >> 16), pz = (int16_t)(o->z >> 16);
    int camx = (int)G->camx, camy = (int)G->camy, camz = (int)G->camz;
    int ce8 = 0xa0, cec = L->ncols * 64 - 0xa0;
    int ymax = L->nrows * 64 - 64;
    int dz = G->have_objects ? G->cam_dz : 0;

    /* FUN_10026730: the Training lessons raise the camera; Medusa's 0x202d zones set the bias directly */
    if (idx == 1) {
        int st = training_cam_station();
        int tgt = (st == 0xc || (st >= 0xf && st <= 0x11)) ? -0x40 : -2;
        int d = G->cam_d04 - tgt;
        if (d < 0) G->cam_d04 = tgt;
        else if (d > 0) G->cam_d04 -= d < 9 ? 1 : 4;
    } else if (G->have_objects && G->O.cam_bias_set) G->cam_d04 = G->O.cam_bias;
    if (G->have_objects && G->O.cam_xmax) ce8 = G->O.cam_xmax > ce8 ? G->O.cam_xmax : ce8;   /* Nessus arena: DAT_10070ce8 = camera */

    for (int it = 0; it < 2; it++) {
        /* x */
        int dx = px - camx;
        if (flags & 1) dx = (int)((unsigned)dx << 20) >> 20;
        int e = (dx + 0xa0) * 2 - 0x140;
        if (flags & 1) e = (int)((unsigned)e << 19) >> 19;
        e -= o->facing ? d02 : -d02;
        int st = step_for(e);
        if (st) {
            if (e < 1) { camx -= st; if (!(flags & 1) && camx < ce8) camx = ce8; }
            else { camx += st; if (!(flags & 1) && camx > cec) camx = cec; }
        }
        /* z */
        int ez = ((pz - camz) + dz - 0x140) * 2;
        int sz = step_for(ez);
        if (sz) camz += ez < 1 ? -sz : sz;
        if (it > 0 && (sz & 1)) continue;       /* the original skips the y step on this pass */
        /* y */
        int ey = ((py - camy) + ce0) * 2 - 0x168;
        if (flags & 0x40) {
            int sy = ce0 + (py - camy);         /* the player's screen y (obj+0xe) */
            if (sy < d0c) { int k = ((d0c - sy) + 0x10) * 0x10; if (k > 0x200) k = 0x200; ey = k * ey >> 8; }
            if (d0e < sy) { int k = ((sy - d0e) + 0x10) * 0x10; if (k > 0x200) k = 0x200; ey = k * ey >> 8; }
        }
        ey += G->cam_d04;
        int sy2 = step_for(ey);
        if (sy2) {
            if (ey < 1) { camy -= sy2; if (camy < 0x7c) camy = 0x7c; }
            else { camy += sy2; if (camy > ymax) camy = ymax; }
        }
    }
    G->camx = (float)camx; G->camy = (float)camy; G->camz = (float)camz;
}
