/* ENEMIES: internal helpers shared by objects.c and enemies.c (ports of the ENG1 object toolkit). */
#pragma once
#include "objects.h"

#define FX(v) ((int32_t)(v) * 65536)
#define I(v) ((v) >> 16)

enum { K_NONE, K_PICKUP, K_CHECKPOINT, K_EFFECT, K_ACTOR, K_GENERIC1, K_ENEMY, K_DEBRIS, K_SOLID /* SOLIDS */, K_BOSS /* BOSSES */, K_TITAN /* TITANS2 */, K_FXW /* FXW: weapon / hit effect objects (fxw.c) */ };

int obj_rnd(Objects *O, int n);                                  /* FUN_1002c460 */
void obj_sound(Objects *O, int id, int32_t x);                   /* FUN_10006da0 */
void obj_sound_obj(Objects *O, int id, const Object *o);         /* AUDSHELL: sound owned (positioned / tracked) by an object */
void obj_sound_none(Objects *O, int id, int32_t x);                /* FIX31: FUN_10006da0(id, 0), no owner */
void obj_sound_stop(Objects *O, int id);                         /* AUDSHELL: FUN_10007f8e(id, 0) */
void obj_set_anim(Objects *O, Object *o, int a);                 /* FUN_100292f0 */
int obj_run_script(Objects *O, Object *o);                       /* FUN_100283e0 */
void obj_kill(Objects *O, Object *o, int gone);
int obj_box(Objects *O, const Object *o, int k, int box[6]);     /* FUN_1002a880 */
int obj_ground_follow(Objects *O, Object *o);                    /* FUN_10029c20 */
void obj_drop(Objects *O, Object *o);                            /* FUN_1002a290 */
uint16_t obj_attr_flags(Objects *O, int x, int y, int z);        /* DAT_100713bc[FUN_1002a170] */
Object *obj_spawn(Objects *O, uint16_t type, int32_t x, int32_t y, int32_t z);   /* FUN_100300c0 */
int obj_boxes_overlap(const int a[6], const int b[6]);

/* enemies.c */
int enemy_init(Objects *O, Object *o, uint32_t init);            /* 1 if the init handler is ported */
void enemy_tick(Objects *O, Object *o);
void enemy_callback(Objects *O, Object *o, int id);
void debris_tick(Objects *O, Object *o);
void enemies_player_spawns(Objects *O);
void enemies_tick_proj(Objects *O, Object *o);                           /* weapon effect objects from P->spawns */

/* SOLIDS: helpers exported by objects.c for solids.c */
Object *obj_alloc(Objects *O);
int obj_run_words(Objects *O, Object *o);
int obj_type_info(Objects *O, uint16_t type, uint32_t *script, uint32_t *anims, uint32_t *list, int *bank, uint32_t *init);
Bank *obj_bank(Objects *O, int b);

/* ENEMIES2: enemies.c toolkit wrappers and the enemies2.c behaviours (minotaur, harpies, griffons, centaurs,
 * Training shark / Pegasus / Phil and the lessons). */
int en_offscreen(Objects *O, const Object *o, int range);
void en_move4(Objects *O, Object *o);
int en_took_hit(Objects *O, Object *o);
int en_hits_player(Objects *O, const Object *o, int k);
void en_hurt_player(Objects *O, const Object *o, int level);
void en_hit_sound(Objects *O, int sword_id);
int en_blow_damage(Objects *O);
void en_drop_pickup(Objects *O, const Object *o, int id);
void thebes_rock_shatter(Objects *O, Object *r);
Object *thebes_loose_rock(Objects *O);            /* LV_THEBES: leftovers.c */   /* LV_THEBES: FUN_10025190 */
Object *en_drop_pickup_obj(Objects *O, const Object *o, int id);   /* TITANS3 */
void en_player_box6(const Player *P, int b[6]);
int enemies2_init(Objects *O, Object *o, uint32_t init);         /* 1 if handled */
int enemies2_tick(Objects *O, Object *o);                        /* 1 if handled */
int enemies2_callback(Objects *O, Object *o, int id);            /* 1 if handled */
void enemies2_frame(Objects *O);                                 /* level per-frame hooks */
/* BOSSES */
void obj_collect(Objects *O, uint16_t type);                   /* FUN_10012ef0: give a pickup's reward */
void bosses_start(Objects *O);                                  /* level start hook (FUN_1001fd20) */
void bosses_level_tick(Objects *O);                             /* per-frame level hook (FUN_1001fd90) */
void boss_tick(Objects *O, Object *o);
void boss_callback(Objects *O, Object *o, int id);              /* script ops -10 / -11 on boss objects */
void boss_draw(Objects *O, Renderer *R, Object *o);
void bosses_draw_begin(void);                                   /* bosses_models.c: new frame */
void solids_draw(Objects *O, Renderer *R, const Object *o);
/* NESSUS: nessus.c */
int nessus_init(Objects *O, Object *o, uint32_t init);
int nessus_tick(Objects *O, Object *o);
int nessus_callback(Objects *O, Object *o, int id);
void nessus_frame(Objects *O);
void nessus_reset(const Objects *O);
void boss_model_draw(Objects *O, Renderer *R, const Object *o);   /* MERGE: bosses_models.c */
/* ENEMIES3: training.c (Phil's lessons) and enemies3.c (harpies, Forest / Thebes actors) */
void enemies3_reset(Objects *O);
int enemies3_init(Objects *O, Object *o, uint32_t init);         /* 1 if handled */
int enemies3_callback(Objects *O, Object *o, int id);              /* FIX31: Phil's lesson callbacks 2 / 4 (script op -10) */
int enemies3_tick(Objects *O, Object *o);                        /* 1 if handled */
int enemies3_solid_tick(Objects *O, Object *o);                  /* Training stations / gate / weight / cannon */
void enemies3_frame(Objects *O);
int enemies3_phys(Objects *O, Object *o);                       /* FUN_1002bc50: velocity + floor landing (bit 0 = landed) */
uint16_t training_tbl(Objects *O, int st, int field);            /* run-time station table 0x10065678 */
void training_tbl_set(Objects *O, int st, int field, uint16_t v);   /* FIX31 */
int enemies3_counter(int which);                                 /* 0: DAT_10072190 health drops, 1: DAT_100721a8 vases */
void enemies3_counter_set(int which, int v);
void audobj_reset(void);                                         /* AUDIT: solids.c */
int enemies4_init(Objects *O, Object *o, uint32_t init);         /* enemies3.c */
int enemies4_tick(Objects *O, Object *o);
int enemies4_callback(Objects *O, Object *o, int id);
void enemies4_frame(Objects *O);
/* LEFTOVERS: objects.c export and leftovers.c (Thebes actors, harpy food and fight, bird flocks, spawners) */
void obj_move_table(Objects *O, Object *o);                      /* FUN_100291d0 */
int leftovers_init(Objects *O, Object *o, uint32_t init);        /* 1 if handled */
int leftovers_tick(Objects *O, Object *o);                       /* 1 if handled */
int leftovers_callback(Objects *O, Object *o, int id);           /* 1 if handled */
void leftovers_frame(Objects *O);
void leftovers_reset(Objects *O);
void enemies3_set_harpies_done(int done);                        /* enemies3.c: DAT_1006ffdc */
Object *leftovers_food_near(Objects *O, const Object *harpy);       /* FUN_10010e70's group 0x55 search */
void leftovers_cam_lock(Objects *O, int32_t arena_x);           /* Nessus follower 0x09 (FUN_10014e10): DAT_10070ce8 */
/* FX: fx_birds.c (Training 0x38 / Forest 0x2e Stymphalian birds) */
int fx_birds_init(Objects *O, Object *o, uint32_t init);
int fx_birds_tick(Objects *O, Object *o);

/* FX2: chain drawing (vertex transforms 2/5: welded neck segments, gaze rays) - bosses_models.c */
void boss_model_chain(Objects *O, Renderer *R, const Object *o, int xf, uint32_t wlist, Vec3 *W, int *wvalid);

/* AUDSHELL: the HERCULES letter display (scrolls.c), ENG1 side */
void obj_scrolls_trigger(Objects *O, int id);
void obj_scrolls_tick(Objects *O);
void obj_scrolls_draw(Objects *O, Renderer *R);

/* FXW: fxw.c - Gift of the Gods effects (stream, bolt, sonic orbs), sparks, trails, explosions */
int fxw_init(Objects *O, Object *o, uint32_t init);              /* 1 if the init handler is ported (0x2003 / 0x2010 / 0x4008 / 0x4009) */
void fxw_tick(Objects *O, Object *o);                            /* K_FXW update */
int fxw_proj_tick(Objects *O, Object *o);                        /* AI_PROJ player weapon objects: 1 if handled (0x4005 / 0x4006) */
void fxw_spawns(Objects *O);                                     /* P->spawns + P->flame: creates the weapon effects (replaces the old enemies.c code) */
void fxw_frame(Objects *O);                                      /* per tick: the fireball/lightning stream chain */
void fxw_draw(Objects *O, Renderer *R);                          /* stream chain and its afterimages */
float fxw_player_depth(Objects *O, Renderer *R);                 /* player sprite depth (SZ>>2) */
int fxw_proj_box(Objects *O, Object *p, int box[6]);             /* hit box of a weapon projectile (1 = valid, 0 = none) */
void fxw_reset(void);
