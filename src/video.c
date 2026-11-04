#include "video.h"
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static uint16_t crc16(const uint8_t *d, uint32_t n)
{
    uint16_t c = 0;
    for (uint32_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1;
    }
    return c;
}

/* ---- RNC method 2 (HERCULES.EXE unpacker) ---- */
typedef struct { const uint8_t *p, *end; int buf, cnt; int bad; } Bits;

static int rbit(Bits *b)
{
    if (!b->cnt) {
        if (b->p >= b->end) { b->bad = 1; return 0; }
        b->buf = *b->p++; b->cnt = 8;
    }
    int r = (b->buf >> 7) & 1;
    b->buf = (b->buf << 1) & 0xff; b->cnt--;
    return r;
}
static int rbyte(Bits *b) { if (b->p >= b->end) { b->bad = 1; return 0; } return *b->p++; }
static int rlen(Bits *b)
{
    int a = rbit(b);
    if (!rbit(b)) return a + 4;
    int c = rbit(b);
    return c - 2 + (a + 4) * 2;
}
static int rdist(Bits *b)
{
    int x = 0;
    if (rbit(b)) {
        x = rbit(b);
        if (!rbit(b)) { if (!x) x = rbit(b) + 2; }
        else {
            x = (rbit(b) + x * 2) | 4;
            if (!rbit(b)) x = rbit(b) + x * 2;
        }
    }
    return x * 256 + 1 + rbyte(b);
}

/* Returns unpacked length or -1. */
static int rnc2(const uint8_t *src, uint32_t avail, uint8_t *out, uint32_t cap)
{
    if (avail < 18 || memcmp(src, "RNC\2", 4)) return -1;
    uint32_t ulen = be32(src + 4), plen = be32(src + 8);
    uint16_t ucrc = (uint16_t)(src[12] << 8 | src[13]);
    if (ulen > cap || 18 + plen > avail) return -1;
    Bits b = {src + 18, src + 18 + plen, 0, 0, 0};
    uint32_t o = 0;
#define COPY(n, d) do { int n_ = (n), d_ = (d); if ((uint32_t)d_ > o) return -1; \
        for (int i_ = 0; i_ < n_ && o < ulen; i_++, o++) out[o] = out[o - d_]; } while (0)
    rbit(&b); rbit(&b);
    while (o < ulen && !b.bad) {
        if (!rbit(&b)) { out[o++] = (uint8_t)rbyte(&b); continue; }
        if (!rbit(&b)) {
            int n = rlen(&b);
            if (n == 9) {
                int k = rbit(&b) << 3; k |= rbit(&b) << 2; k |= rbit(&b) << 1; k |= rbit(&b);
                for (int i = 0; i < k * 4 + 12 && o < ulen; i++) out[o++] = (uint8_t)rbyte(&b);
            } else COPY(n, rdist(&b));
            continue;
        }
        if (!rbit(&b)) { COPY(2, rbyte(&b) + 1); continue; }
        if (!rbit(&b)) { COPY(3, rdist(&b)); continue; }
        int n = rbyte(&b) + 8;
        if (n == 8) { rbit(&b); continue; }
        COPY(n, rdist(&b));
    }
#undef COPY
    if (b.bad || o != ulen) return -1;
    if (crc16(out, ulen) != ucrc) return -1;
    return (int)ulen;
}

static uint32_t msrand(uint32_t *s) { *s = *s * 0x343FD + 0x269EC3; return (*s >> 16) & 0x7fff; }

int video_open(Video *v, const char *path)
{
    memset(v, 0, sizeof *v);
    v->voice = -1;
    v->f = fopen(path, "rb");
    if (!v->f) return -1;
    uint8_t h[0x348];
    if (fread(h, 1, sizeof h, v->f) != sizeof h || rd32(h) != 0x0A565445) { video_close(v); return -1; }
    v->w = rd32(h + 4); v->h = rd32(h + 8); v->nframes = rd32(h + 12); v->fps = rd32(h + 16);
    v->channels = rd32(h + 20); v->rate = rd32(h + 24); v->bits = rd32(h + 28);
    if (!v->w || !v->h || v->w > 1024 || v->h > 1024 || !v->fps || (v->w & 15) || (v->h & 3)) { video_close(v); return -1; }
    v->chunk_size = calloc(208, sizeof(uint32_t));
    for (int i = 0x28; i + 4 <= 0x348; i += 4) {
        uint32_t s = rd32(h + i);
        if (!s) break;
        v->chunk_size[v->nchunks++] = s;
    }
    v->audio_per_chunk = (v->channels * v->bits / 8) * v->rate / v->fps * v->fps;
    v->cur = calloc(v->w * v->h, 1);
    v->ref = calloc(v->w * v->h, 1);
    uint32_t seed = 1;
    for (int i = 0; i < 47; i++) {
        int a = ((int)(msrand(&seed) % 5) - 2) * (int)v->w;
        v->offs[i] = a - 2 + (int)(msrand(&seed) % 5);
    }
    /* soundtrack: concatenate each chunk's PCM, mixed down to 16-bit mono */
    int bps = (int)(v->bits / 8) * (int)v->channels;
    if (bps > 0 && v->audio_per_chunk) {
        int per = (int)(v->audio_per_chunk / (uint32_t)bps);
        v->snd.pcm = malloc(sizeof(int16_t) * (size_t)per * (size_t)(v->nchunks ? v->nchunks : 1));
        v->snd.rate = (int)v->rate;
        uint8_t *tmp = malloc(v->audio_per_chunk);
        long off = 0x348;
        for (int c = 0; c < v->nchunks; c++) {
            fseek(v->f, off, SEEK_SET);
            size_t got = fread(tmp, 1, v->audio_per_chunk, v->f);
            int n = (int)(got / (size_t)bps);
            for (int i = 0; i < n; i++) {
                int s = 0;
                for (uint32_t ch = 0; ch < v->channels; ch++) {
                    const uint8_t *p = tmp + i * bps + ch * (v->bits / 8);
                    s += v->bits == 16 ? (int16_t)(p[0] | p[1] << 8) : (p[0] - 128) << 8;
                }
                v->snd.pcm[v->snd.n++] = (int16_t)(s / (int)v->channels);
            }
            off += v->chunk_size[c];
        }
        free(tmp);
    }
    return 0;
}

int video_open_num(Video *v, const char *fs_dir, int n)
{
    const char *env = getenv("HERCULES_ETV_DIR");
    const char *fmt[] = {"%s/%d.ETV", "%s/DATA/%d.ETV", "%s/../Hercules_AG_extracted/DATA/%d.ETV",
                         "%s/../../Hercules_AG_extracted/DATA/%d.ETV", "%s/%d.etv"};
    char p[1024];
    if (env) {
        snprintf(p, sizeof p, "%s/%d.ETV", env, n);
        if (!video_open(v, p)) return 0;
    }
    for (size_t i = 0; i < sizeof fmt / sizeof fmt[0]; i++) {
        snprintf(p, sizeof p, fmt[i], fs_dir, n);
        if (!video_open(v, p)) return 0;
    }
    memset(v, 0, sizeof *v);
    v->voice = -1;
    return -1;
}

static int load_chunk(Video *v)
{
    if (v->chunk >= v->nchunks) return -1;
    long off = 0x348;
    for (int c = 0; c < v->chunk; c++) off += v->chunk_size[c];
    uint32_t sz = v->chunk_size[v->chunk];
    free(v->chunkbuf);
    v->chunkbuf = malloc(sz);
    fseek(v->f, off, SEEK_SET);
    v->chunklen = (uint32_t)fread(v->chunkbuf, 1, sz, v->f);
    v->chunkpos = v->audio_per_chunk;
    return 0;
}

int video_next(Video *v, uint32_t *argb)
{
    if (!v->f) return -1;
    for (;;) {
        if (!v->chunkbuf || v->chunkpos + 5 > v->chunklen) {
            if (v->chunkbuf) v->chunk++;
            if (load_chunk(v)) return -1;
            continue;
        }
        break;
    }
    if (v->frame == 0 && v->snd.n && v->voice < 0) v->voice = sound_play(&v->snd, 256, 0, 0);
    uint8_t *d = v->chunkbuf + v->chunkpos;
    uint32_t avail = v->chunklen - v->chunkpos;
    uint32_t fsz = rd32(d);
    int flag = d[4];
    uint32_t q = 5;
    if (flag && q + 0x2300 <= avail) {
        const uint8_t *raw = d + q;
        int mx = 0;
        for (int i = 0; i < 768; i++) if (raw[i] > mx) mx = raw[i];
        for (int i = 0; i < 256; i++) {
            int c[3];
            for (int k = 0; k < 3; k++) {
                int x = raw[i * 3 + k];
                c[k] = mx < 64 ? (x * 4 + (x >> 4) > 255 ? 255 : x * 4 + (x >> 4)) : x;
            }
            v->pal[i] = 0xff000000u | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | (uint32_t)c[2];
        }
        const uint8_t *mask = raw + 768;
        if (!v->lut) v->lut = malloc(65536);
        for (int row = 0; row < 256; row++)
            for (int col = 0; col < 256; col++)
                v->lut[row * 256 + col] = (uint8_t)((mask[row * 32 + (col >> 3)] >> (col & 7) & 1) ? col : row);
        q += 0x2300;
    }
    uint32_t W = v->w, H = v->h, npx = W * H;
    static uint8_t *data; static uint32_t datacap;
    if (datacap < npx + npx / 64 + 64) { free(data); datacap = npx + npx / 64 + 64; data = malloc(datacap); }
    int n = q + 4 < avail ? rnc2(d + q + 4, avail - q - 4, data, datacap) : -1;
    if (n >= (int)(npx / 64)) {
        const uint8_t *tmap = data;
        uint32_t px = npx / 64;
        for (uint32_t by = 0; by < H; by += 4)
            for (uint32_t bx = 0; bx < W; bx += 4) {
                uint32_t bi = (bx >> 4) + (W >> 4) * (by >> 2);
                int t = (tmap[bi] >> ((bx >> 1) & 6)) & 3;
                if (t == 0) {
                    if (px + 16 > (uint32_t)n) break;
                    for (int r = 0; r < 4; r++) {
                        uint32_t a = (by + r) * W + bx;
                        memcpy(v->cur + a, data + px, 4); memcpy(v->ref + a, data + px, 4); px += 4;
                    }
                } else if (t == 2) {
                    if (px >= (uint32_t)n) break;
                    uint8_t c = data[px++];
                    for (int r = 0; r < 4; r++) {
                        uint32_t a = (by + r) * W + bx;
                        memset(v->cur + a, c, 4); memset(v->ref + a, c, 4);
                    }
                }
            }
        if (v->lut)
            for (uint32_t by = 4; by + 4 < H; by += 4)
                for (uint32_t bx = 4; bx + 4 < W; bx += 4) {
                    uint32_t bi = (bx >> 4) + (W >> 4) * (by >> 2);
                    if (((tmap[bi] >> ((bx >> 1) & 6)) & 3) != 2) continue;
                    for (int r = 0; r < 4; r++)
                        for (int c = 0; c < 4; c++) {
                            if (++v->widx > 0x2e) v->widx = 0;
                            long a = (long)(by + r) * W + bx + c;
                            long nb = a + v->offs[v->widx];
                            if (nb >= 0 && nb < (long)npx) v->cur[a] = v->lut[v->cur[a] * 256 + v->ref[nb]];
                        }
                }
    }   /* a frame that fails its CRC keeps the previous picture */
    for (uint32_t i = 0; i < npx; i++) argb[i] = v->pal[v->cur[i]];
    v->chunkpos += fsz ? fsz : avail;
    v->frame++;
    return 0;
}

void video_close(Video *v)
{
    if (v->voice >= 0) sound_stop(v->voice);
    if (v->f) fclose(v->f);
    free(v->chunk_size); free(v->chunkbuf); free(v->cur); free(v->ref); free(v->lut);
    sound_free(&v->snd);
    memset(v, 0, sizeof *v);
    v->voice = -1;
}
