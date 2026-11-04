/* FX3: level light, sprite draw modes 4 / 7 / 8. See fx3.h. */
#include "fx3.h"
#include "objects_int.h"
#include <math.h>

int fx3_light_channel(int level_colour, int ambient, int light, int fade)
{
    int k = light * 2 * fade;                               /* FUN_10005a60: iVar13 = param_4 * 2 * DAT_1006ee84 */
    int v = ((k * level_colour) >> 15) + ambient;
    return v > 0xff ? 0xff : v;
}

int fx3_level_light(const Level *L)
{
    if (!L || !L->d) return 0x80;
    int c = L->eng3 ? L->d[0x34] : L->d[0x2c];             /* ENG3 FUN_1001af10: DAT_10040628 = header +0x34 */
    return fx3_light_channel(c, 0, 0x80, 0x80);
}

/* FUN_100304f0: sprite mesh. Records are 16 bytes {s32 x, y, z (16.16), s16 frame, u16 flags (low half of
 * flags54 for that sprite: semi bit 2, abr bits 0-1, mirror bit 4)}, count +0x36. Without flags54 0x200000 each
 * record is placed through the object's transform (FUN_10031dc0: here the yaw +0x72), drawn with the object's
 * bank and brightness (+0x4a). Records with +0x0f & 0x80 are line primitives (code 0x68); not used by the
 * meshes ported here. */
static void draw_mesh(Objects *O, Renderer *R, Object *o, uint32_t tint)
{
    Bank *b = obj_bank(O, o->bank);
    if (!b) return;
    int a = o->rot[1] & 0xfff;                              /* +0x72, 0x1000 = full turn */
    float c = cosf(a * (float)(2 * M_PI / 4096)), sn = sinf(a * (float)(2 * M_PI / 4096));
    float s = (o->scale ? o->scale : 0x100) / 256.0f;
    int n = o->mesh_n;
    for (int k = 0; k < n && k < 64; k++) {
        uint32_t r = o->mesh + 16u * (uint32_t)k;
        const uint8_t *p = pe_ptr(O->pe, r, 16);
        if (!p) break;
        if (p[15] & 0x80) continue;
        int fr = (int16_t)(p[12] | p[13] << 8);
        uint32_t fl = (uint32_t)(p[14] | p[15] << 8);
        if (fr <= 0) continue;
        const Image *img = bank_frame(b, fr);
        if (!img) continue;
        float ox = (int32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24) / 65536.0f;
        float oy = (int32_t)(p[4] | p[5] << 8 | p[6] << 16 | (uint32_t)p[7] << 24) / 65536.0f;
        float oz = (int32_t)(p[8] | p[9] << 8 | p[10] << 16 | (uint32_t)p[11] << 24) / 65536.0f;
        float px = o->x / 65536.0f + (ox * c - oz * sn) * s, pz = o->z / 65536.0f + (ox * sn + oz * c) * s, wx, wz;
        level_path_to_world(O->L, px, pz, &wx, &wz, NULL);
        int mode = (fl & 4) ? (int)(fl & 3) : -1;
        render_sprite_fx(R, img, wx, o->y / 65536.0f + oy * s, wz, (fl & 0x10) != 0, -4, o->scale, tint, mode, 1.0f);
    }
}

int fx3_draw_mode(Objects *O, Renderer *R, Object *o, const Image *img, float wx, float wy, float wz, uint32_t tint,
                  int mode)
{
    switch (o->b23) {
    case 4:                                                 /* FUN_100304f0 */
        if (!o->mesh || o->mesh_n <= 0) return 0;
        draw_mesh(O, R, o, tint);
        return 1;
    case 7:                                                 /* FUN_1000c720: draws nothing */
        return 1;
    case 8:                                                 /* FUN_100310e0: dissolve */
        if (!(o->flags54 & 0x100)) return 0;
        {
            int b = o->bright;
            if (b < 0) b = 0;
            if (b > 0x80) b = 0x80;
            int l1 = 0x80 - (0x80 - b) * (0x80 - b) / 128;  /* pass 1: colour, additive (abr 1) */
            int l2 = b * b / 128;                           /* pass 2: white CLUT DAT_100734ec, subtractive (abr 2) */
            int flip = (o->flags54 & 0x10) != 0;
            /* The second primitive goes into the same OT slot after the first, so the GPU draws it first: the
             * background is darkened by the silhouette, then the colour is added on top. */
            render_sprite_fx(R, img, wx, wy, wz, flip, -3.95f, o->scale, 0x2000000u | (uint32_t)l2 * 0x010101u, 2, 1.0f);
            render_sprite_fx(R, img, wx, wy, wz, flip, -4.0f, o->scale, (uint32_t)l1 * 0x010101u, 1, 1.0f);
        }
        return 1;
    }
    (void)tint; (void)mode;
    return 0;
}
