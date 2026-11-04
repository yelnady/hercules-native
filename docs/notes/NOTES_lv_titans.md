# lv_titans: TITANS (level 9, Pegasus flight) audit

Tree `work/lv_titans` (start `work/base34`). Edits are tagged `LV_TITANS`.

## Audited
Every spawn record type in the level (OBJ_DUMP): 0x28-0x2b Titans, 0x2c-0x30 gods, 0x35, 0x37, 0x3b, 0x3e, 0x42, 0x43, 0x44, 0x45 (Zeus), 0x46/0x48/0x4b markers, 0x51 ice generator, 0x5d spy, 0x61 harpy waves,
pickups 0x200b 0x2012 0x2013 0x2015 0x2016 0x2018 0x201b-d, checkpoints 0x400a-c; plus the children they spawn (0x31-0x34, 0x39, 0x3a, 0x3d, 0x3f, 0x40, 0x41, 0x52/53, 0x56, 0x57, 0x5b, 0x5c, 0x62-0x68).
Method: port code vs decompile / capstone disassembly of ENG1.DLL (`FUN_10038090 10038100 10037fe0 10036d10 10036d70 10036e10 10036e90 100389c0 10038880 1003aa70 10039d40 100393b0 100399c0 100399f0 10039a70 1003a980 1003a700 10038b30 10039f20 1003a040 1003a4a0 1003a340/390/420 1003c130 1003c170 10012ef0 10003870`).
Result: every handler above matches the original (init values, movement, hit/touch tests, damage levels, drops, spawn tables). The level-9 handler table 0x10061f80 also has 0x50, 0x54, 0x55, 0x58, 0x59, 0x5a, 0x5e-0x60 handlers; nothing in the level (map, code, spawn tables) ever spawns them, so nothing is missing.

## Found and fixed
1. Tornado wind loop never stopped (`titans_ai.c` `tornado_titan`). `FUN_10039f20` starts the looping sound 0x256 (owner 0). When the tornado leaves (x < -0x280000) `FUN_1003a040` sets DAT_10064474 = -1; the level's wind object 0x38 (`FUN_1003a4a0`) then calls `FUN_10007f8e(0x256, 0)`. The port had no 0x38 object, so the loop played for ever after the tornado had gone. Now the stop is issued at that point (also in the `titan_gone` path).
2. Restart (`titans.c` `titans_start`): after a death the Titans are removed with the objects, but their 0x246 (lava) / 0x256 (tornado) loops kept playing. Both are stopped on (re)start.

## Test hooks added (verification only)
- `LV_TP=x,y` (titans.c): teleports Pegasus (anchor follows) at tick 20.
- `LV_LOG=1` (objects.c): prints every pickup collected on any level (`obj_collect`) and, on level 9, pickup boxes vs Pegasus' body box.

## Verified headless
- `TITANS_AUTO=1 TITANS_GOD=1 LEVEL_DONE_LOG=1 --level TITANS --frames 20000 --script "."` -> `LEVEL DONE at t=7867 next=TORMENT`; all spawn types spawn (SPAWN_LOG).
- TITANS_LOG of that run: harpies 0x5c and spy 0x5d are hit and die (spy hp 4 -> 0, drops, scroll speed restored), rocks 0x41, ice 0x52/53, 0x5b are hit; 0x34, 0x3d, 0x56, 0x5c hurt Pegasus.
- Pickups collected by teleport (`LV_TP`, `LV_LOG`): 0x2012 life, 0x2013 health, 0x2015 / 0x2016 gifts (+0x80 ammo), 0x2018 coin, 0x201b/c/d letters; boxes overlap Pegasus' body box for all of them. Gods 0x2c and 0x2d touched -> sound 0x627b, fly off dropping gifts.
- `--sfxlog`: before the fix 0x256 started and never stopped; now `sfx stop id=0x0256` appears when the tornado leaves, and 0x246 stops as before.
- Death / game over / checkpoint restart loop runs (no pilot, 9000 frames).

## Not verified / remaining
- No real audio device (dummy driver): the loop stop is checked in the sfx log only.
- Wind object 0x38 itself is not ported: a 224-streak PSX line particle emitter (`FUN_10002500` with table 0x100644d0, per-frame `FUN_1003a390`/`FUN_10002920`, gust target from table 0x10064478) that is created once at level start by `FUN_1003c130`. Visual only; it has no gameplay effect.
- Non-god `TITANS_AUTO` pilot does not finish the level (it dies repeatedly; same with base34); god run finishes.
- Sky is black in the port (documented in earlier notes: CLUT fades have no PC effect); translucent diagonal light bands drawn over the scene were not investigated.
