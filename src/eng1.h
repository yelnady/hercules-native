/* Read-only access to tables inside the original ENG1.DLL (animation scripts, per-level parameters).
 * The DLL is loaded from the game data directory; nothing from it is compiled into this program. */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *data;
    size_t size;
    uint32_t base;
    int nsec;
    struct { uint32_t va, vsize, raw, rsize; } sec[16];
} PeImage;

int pe_load(PeImage *pe, const char *path);
void pe_free(PeImage *pe);
/* Pointer to the bytes at virtual address va (NULL if outside the file); n = bytes needed. */
const uint8_t *pe_ptr(const PeImage *pe, uint32_t va, size_t n);
int16_t pe_s16(const PeImage *pe, uint32_t va);
uint16_t pe_u16(const PeImage *pe, uint32_t va);
uint32_t pe_u32(const PeImage *pe, uint32_t va);
