# lv_thebes: Thebes audit (tree work/lv_thebes, start snapshot work/base34). All edits are tagged `LV_THEBES`.

Spawn types in the map (587 records): 0x04 drum, 0x24 marker, 0x25 crowd x39, 0x26/0x27 thugs, 0x28 THFLASH, 0x2a THSNOW, 0x2b THEQ,
0x2c THFLOOD, 0x2d/0x3f/0x40 THCHAR, 0x30-0x32 fan girls, 0x33 THOLD, 0x34 THEWGUY, 0x37 minotaur, 0x38/0x47 harpies, 0x3c/0x3d harpy
controllers, 0x3e skeletons x18, 0x44 THFAT, 0x46/0x49 model groups, 0x48 rock spawner, pickups, checkpoints, 0x200a end. Every one has a
ported init/update (checked against the ENG1 handler table with a script). Decompiles used: FUN_10001040/100010f0/100015e0 (thug),
100343a0/100343e0/10034570 (THEQ), 100348a0/10034910 (THFLOOD), 10024320/10024f50/10024c60/100103c0/10014210/100142d0/10025190 (minotaur
fight), 1002ae50/1002a9e0/1002aab0 (on-screen tests), 10026d50 (thrown-object list).

## What was wrong (all fixed)

1. **Earthquake girls (THEQ 0x2b) never stomped, so no quake and no 0x402a rocks in a real game.** Their AI callback 2 (`FUN_10034570`:
   stomp anims 6 / 0xb with v2a = 4, anim 0x10, return to anim 0 after the stomp) was swallowed: `enemies4_callback` returned for every
   town actor without doing anything. The earlier rock port only worked with the `HIT_SPAWN` hook. New `town_stomp_ai()` in enemies3.c
   (THFLOOD 0x2c shares callback 2 in the original; for it the animation slots that do not exist are skipped).
2. **The minotaur's throwing rock could not be picked up (spawner 0x48, rock type 5).** The init lifted the rock by the drum's constant
   DAT_10052e76 (48), the original `FUN_10014210` uses DAT_100530de (32): the rock hung 16 px in the air, Hercules walked under it, no touch,
   no lift. solids.c init now uses the rock constant (`I_ROCK5`). Same bug exists for the Forest/Training rock 0x02 (original DAT_10052c0e = 32,
   port still 48); not touched because it is not a Thebes object.
3. **Thrown rocks never hurt the minotaur.** `mino_tick` tests flags50 & 0x800000 (the thrown-object list DAT_10071be0), nothing set it.
   `block_tick` (rock only) now follows `FUN_100142d0`: throw forward sets 0x940000 (state 3), throw up (Up + sword) is state 4 / 5:
   flags 0x800000, velocities 0xc0000 / 0x40000 (the rock lobs in +z from the street plane z 0 through the minotaur's plane z 160),
   drifts +x below 0x1560, shatters on landing (`thebes_rock_shatter`, FUN_10025190; the port's rock_shatter has 8..18 pieces instead of 24..31).
   Flag is cleared in states 0 / 1.
4. **Loose rock smashed by the minotaur / boulders**: tail of `FUN_10024320` / `FUN_10024f50` (spawner's rock, Hercules not holding it,
   box overlap -> shatter + despawn). New `thebes_loose_rock()` (leftovers.c), used by `mino_tick` and `boulder_tick`.
5. Spawner 0x48 `FUN_100103c0` end-of-fight test used the player's x instead of z (`DAT_1007217c + 8 > 0x700000`).

## Verified headless (--level THEBES, ENEMIES2_GOD, ENEMY_TELEPORT, HIT_SPAWN, ENEMY_LOG, THEBES_LOG)

- THEQ: girl at x 4393 now plays anim 6 / 7 with v2a 4, 0x402a rocks spawn and fall (EQ_LOG), return to idle.
- Rock 0x05: spawn with `HIT_SPAWN=5,-50,0`, walk into it (lift, state 20), `S` throw forward (flags 940000, flies, lands), `A` (Up+sword)
  throw up: rock z 0 -> 172 on an arc, hits the minotaur (hp 8 -> 7, anim 0x1a, v2a 7, bar damage), flag 800000 -> 0.
- Minotaur defeat path via test hook `THEBES_MINO_KILL=<tick>` (bar to 0): anim 0x1d, v2a 8 walks off (z 160 -> -36), v2a 9, doorway flags 0xb / 0xc set.
- Thug 0x26: real sword hits (hp 9 -> 5 -> 1, hit anim 0xf, returns), thug attacks (earlier audit). Level end: teleport to x 18700 gives
  `LEVEL DONE next=HYDRA` (LEVEL_DONE_LOG). 3000-frame random-input run: no crash. Training / Forest start and run 600 frames.

## Test hooks added
`THEBES_LOG=1` (rock 0x05 state while thrown / carried), `THEBES_MINO_KILL=n` (kill the minotaur once it exists after n ticks).

## Not done / remaining
- THFLOOD child 0x42 (FUN_100349f0 / 10034a30, semi-transparent frame 1 sprite) and 0x35 / 0x43 (FUN_10034c00, 10025580) are not ported
  (visual children, no decompile of the update functions in the dump).
- Thug / skeleton / harpy code was re-read against the decompile (no divergence found); the full harpy fight, the doorway use after the
  minotaur and all 18 skeletons were not individually driven.
- Hit stagger frames of the thug stay on the same frame for long stretches with continuous sword hits (same script as original, not changed).
- Nothing run with real audio / window.
