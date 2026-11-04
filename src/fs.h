/* HERCULES.FS archive reader (ported from HERCULES.EXE FUN_00404e10 / FUN_00404fe0). */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct { uint32_t key, off, size; } FsEntry;

typedef struct {
    uint8_t *data;
    size_t size;
    FsEntry *ents;
    int n;
} Fs;

int fs_open(Fs *fs, const char *path);
void fs_close(Fs *fs);
uint32_t fs_hash(const char *path);
/* Returns a pointer into the archive (not a copy) or NULL. */
const uint8_t *fs_get(const Fs *fs, const char *name, uint32_t *size);
