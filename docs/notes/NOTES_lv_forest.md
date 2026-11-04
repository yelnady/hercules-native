# lv_forest: Forest level audit (start state = work/base34, edits tagged LV_FOREST)

## Spawn types in FOREST (283 records; OBJ_DUMP=1, SPAWN_AUDIT=1, SPAWN_AUDIT2=1)

Actors: 0x09 Nessus follower, 0x25 generic, 0x26/0x27 thugs (1 each), 0x28 rock centaur (5), 0x29 wrestling centaur (2), 0x2b fish (41),
0x2e Stymphalian birds (36), 0x31 water (11), 0x46/0x47 griffons (6), 0x4b water texture scroller (4), 0x4c/0x4e breakable rocks, 0x4d fish (4),
0x4f pot (2), 0x51/0x52 ambient bird sound emitters (19), 0x53 Nessus trigger, 0x55 Meg. Plus pickups/checkpoints/platforms/poles (0x2xxx/0x4xxx)
and the 0x1xxx load triggers (never spawned on purpose: banks are resident). SPAWN_AUDIT shows no Forest actor falling into the NONE /
GENERIC_ACTOR path (unported children 0x4028.. are Training/Thebes only).

## Fixed

1. Water (0x31) loop sound never stopped ("sound loops forever"). FUN_1000d2c0 plays 0x8ca (the only looping sample of the Forest bank besides
   the player's flame 0x26) with the water object as owner (FUN_10006da0(0x8ca, obj)). water_init (enemies2.c) called obj_sound(O, 0x8ca, o->x);
   during an init O->cur is not the new object, so the sound got the position-only owner 1. It was never tied to the water: when the water is
   culled (FUN_1000d350 -> kill) the loop kept playing, and every respawn started one more voice. Now obj_sound_obj(O, 0x8ca, o): owner = the
   object, tracked on screen and stopped by kill_obj (sfx_stop_owner_loops).
   Verified with --sfxlog: before, every 0x8ca line had owner=1; after, owners are the water object slots (3, 4, 5, 7, 11, 13, 29, 31 ...).
   (Loop stop on cull itself is by code path, not heard: dummy audio.)

## Verified headless (no change needed)

* Thug 26/27, wrestling centaur 29, griffon 47, birds 2e, breakable rock 4c (HIT_LOG solid ... OVERLAP when standing at the rock; it is about
  160 wide), drum/fish 4d: hits register, hp drops, death anims play, they hit back (thug: player hp 128 -> 98 -> 68 -> 38).
* Rock centaur 0x28: its update2 FUN_100086f0 (disassembled with capstone) calls neither FUN_10026de0 nor FUN_10026e90 and never uses hp 100:
  not hittable in the original either (walks, throws 0x4a rocks; port matches).
* Ambient birds 0x51/0x52 (FUN_1000e470/e560): sound emitters only, ported.
* Nessus: ENEMY_TELEPORT=18495,672,512 + ~600 idle frames: follower arms, trigger spawns Nessus, he walks up and punches (state 0x16).

## Not verified / remaining

* Level end (Nessus ride, power punch, player_win) not driven to LEVEL DONE: blind scripts cannot cross the wall at x 18210 (needs the plane-640
  route) and teleports leave the scroll spawner behind (camera needs about 500 ticks to catch up; idle first when testing far spawns).
* Griffon 0x46 depth-plane pairs (hostile on z 640 vs player on 384) not checked individually.
* The Ghidra export lacks 100086f0, 100092c0, 10008e10, 1000b3f0, 1000d7d0 etc.; only disassembly was used, no full line-by-line compare of those.

## Files changed

* src/enemies2.c water_init (fix above).
* src/sfx.c sfx_dump_wavs: SFX_LOOPS=1 with --dumpwav dir prints the looping samples of a level (verification only).
* src/solids.c took_hit: HIT_LOG=1 prints solid-object hit tests (verification only).
