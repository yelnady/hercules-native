#include "eng1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t r32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t r16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

int pe_load(PeImage *pe, const char *path)
{
    memset(pe, 0, sizeof *pe);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0x400) { fclose(f); return -1; }
    pe->data = malloc((size_t)sz);
    if (!pe->data || fread(pe->data, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); pe_free(pe); return -1; }
    fclose(f);
    pe->size = (size_t)sz;
    uint32_t h = r32(pe->data + 0x3c);
    if (h + 0xf8 > pe->size || memcmp(pe->data + h, "PE\0\0", 4)) { pe_free(pe); return -1; }
    int n = r16(pe->data + h + 6), opt = r16(pe->data + h + 20);
    pe->base = r32(pe->data + h + 52);
    for (int i = 0; i < n && i < 16; i++) {
        const uint8_t *s = pe->data + h + 24 + opt + 40 * i;
        if (s + 40 > pe->data + pe->size) break;
        pe->sec[pe->nsec].vsize = r32(s + 8);
        pe->sec[pe->nsec].va = r32(s + 12);
        pe->sec[pe->nsec].rsize = r32(s + 16);
        pe->sec[pe->nsec].raw = r32(s + 20);
        pe->nsec++;
    }
    return 0;
}

void pe_free(PeImage *pe)
{
    free(pe->data);
    memset(pe, 0, sizeof *pe);
}

const uint8_t *pe_ptr(const PeImage *pe, uint32_t va, size_t n)
{
    if (!pe->data || va < pe->base) return NULL;
    uint32_t rva = va - pe->base;
    for (int i = 0; i < pe->nsec; i++) {
        if (rva >= pe->sec[i].va && rva + n <= pe->sec[i].va + pe->sec[i].rsize) {
            size_t off = pe->sec[i].raw + (rva - pe->sec[i].va);
            return off + n <= pe->size ? pe->data + off : NULL;
        }
    }
    return NULL;
}

int16_t pe_s16(const PeImage *pe, uint32_t va) { const uint8_t *p = pe_ptr(pe, va, 2); return p ? (int16_t)r16(p) : 0; }
uint16_t pe_u16(const PeImage *pe, uint32_t va) { const uint8_t *p = pe_ptr(pe, va, 2); return p ? r16(p) : 0; }
uint32_t pe_u32(const PeImage *pe, uint32_t va) { const uint8_t *p = pe_ptr(pe, va, 4); return p ? r32(p) : 0; }
