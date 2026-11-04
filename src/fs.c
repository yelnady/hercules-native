#include "fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int fs_open(Fs *fs, const char *path)
{
    memset(fs, 0, sizeof *fs);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0x3000) { fclose(f); return -1; }
    fs->data = malloc((size_t)sz);
    if (!fs->data || fread(fs->data, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(fs->data); return -1; }
    fclose(f);
    fs->size = (size_t)sz;
    fs->ents = calloc(0x3000 / 12, sizeof(FsEntry));
    for (int i = 0; i < 0x3000 / 12; i++) {
        const uint8_t *e = fs->data + i * 12;
        uint32_t k = rd32(e);
        if (!k) break;
        uint32_t o = rd32(e + 4), s = rd32(e + 8);
        if ((size_t)o + s > fs->size) continue;
        fs->ents[fs->n++] = (FsEntry){k, o, s};
    }
    return 0;
}

void fs_close(Fs *fs)
{
    free(fs->data);
    free(fs->ents);
    memset(fs, 0, sizeof *fs);
}

/* Drive letters used by the game: O: maps, P: sprite banks, Q:/R: sound. */
static void normalize(const char *in, char *out, size_t cap)
{
    char p[512];
    size_t n = 0;
    for (; in[n] && n < sizeof p - 1; n++) p[n] = (char)toupper((unsigned char)in[n]);
    p[n] = 0;
    char tmp[512];
    if (n < 2 || p[1] != ':') {
        snprintf(tmp, sizeof tmp, "M:%s%s", p[0] == '\\' ? "" : "\\", p);
        memcpy(p, tmp, sizeof p);
    }
    const char *rest = p + 2;
    switch (p[0]) {
    case 'M': snprintf(out, cap, "%s", p); return;
    case 'O': snprintf(out, cap, "M:\\GRAFIX\\MAPS%s", rest); return;
    case 'P': snprintf(out, cap, "M:\\GRAFIX\\CHOP%s", rest); return;
    case 'Q': case 'R': snprintf(out, cap, "M:\\LANGUAGE\\SONIX%s", rest); return;
    default: {
        const char *s = strchr(rest + 1, '\\');
        snprintf(out, cap, "M:%s", s ? s : "");
    }
    }
}

uint32_t fs_hash(const char *path)
{
    char s[600];
    normalize(path, s, sizeof s);
    uint32_t h = 0;
    int sh = 0;
    size_t n = strlen(s);
    for (size_t i = 0; i < n; i++) {
        int c = (signed char)s[i];
        h += (uint32_t)c << sh;
        sh += 8;
        if (sh > 0x18) sh = 0;
    }
    return h + (uint32_t)n;
}

const uint8_t *fs_get(const Fs *fs, const char *name, uint32_t *size)
{
    uint32_t k = fs_hash(name);
    for (int i = 0; i < fs->n; i++)
        if (fs->ents[i].key == k) {
            if (size) *size = fs->ents[i].size;
            return fs->data + fs->ents[i].off;
        }
    return NULL;
}
