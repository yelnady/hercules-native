/* SOLIDS: 3D map-model objects (ENG1 bank-0 objects: crates, platforms, breakables, training equipment),
 * object groups (FUN_1002b130), the solid / platform object lists (DAT_10071b20 / DAT_100714c0), carrying
 * and poles. See analysis/objects_notes.md "Solid objects". */
#pragma once
#include "objects_int.h"

/* Map model data (map header group B, ENG1 DAT_10070d2c): record of model idx (1-based), 0 if none. */
uint32_t model_rec(const Level *L, int idx);
/* FUN_10031ec0: model collision box k (0 = first) relative to the object: {x0, y0, z0, w, h, d}. */
int model_box(const Level *L, const Object *o, int k, int out[6]);

/* Init handler port for model / group / platform types. Returns 1 if the type is handled here. */
int solids_init(Objects *O, Object *o, uint32_t init, uint32_t update);
void solids_tick(Objects *O, Object *o);
/* Draw a 3D model object (flags54 & 0x10000000, frame = model index). */
void solids_draw(Objects *O, Renderer *R, const Object *o);
void solids_draw_reset(void);
void solids_audit(Objects *O, Renderer *R);
/* Before the per-object updates: fill the player's touch / pole hooks and move riders. */
void solids_frame(Objects *O, Player *P);
void solids_pre(Objects *O, Player *P);
/* Player hooks: platform floors (FUN_100296e0) and solid attributes (FUN_1002a170). */
int solids_floor(void *ctx, int x, int y, int z, int *up, int *down);
int solids_attr(Objects *O, int x, int y, int z);
/* Spawn a group's children (FUN_1002b130). Returns 0 if the type has no child list. */
int solids_spawn_children(Objects *O, Object *o);
/* Remove an object together with its children. */
void solids_kill(Objects *O, Object *o, int gone);
