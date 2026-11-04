# lv_cyclops: Cyclops chase (ENG3 level 8) audit

Work tree `work/lv_cyclops` (start state `work/base34`). Edits tagged `LV_CYCLOPS`. Files changed: `src/e3_cyclops.c`, `src/e3game.c` (test hooks only).

## Audit result

Spawn types of the level (E3_AUDIT / E3_SPLIST=-1): 0x6 (278 pickups), 0x2a, 0x2b, 0x2d, 0x2e, 0x2f, 0x35, 0x39, 0x3d, 0x3e, 0x3f, 0x42, 0x45, 0x47, 0x48 and
the checkpoint statues 0x200b / 0x200c / 0x200d. Children / effects: 0x29 (Cyclops, spawned by the player), 0x30, 0x38, 0x43, 0x4002..4004, 0x4029, 0x402c, 0x402f,
0x4030, 0x4033, 0x2016.
Handlers compared with the decompile where one exists (FUN_10003610 Cyclops, 10001aa0 / 10002580 / 100062f0 columns, 10002eb0 team, 10003bf0 people, 10004c10 / 10004ca0
flyer, 100053f0 / 100056a0 crowd, 10006af0 / 10006b90 embers, 10006c20 barricade, 10015680 pickups, 1001eff0 overlap, 1001b920 offscreen): all match (including
original quirks such as 10002eb0 setting the animation on the parent). Hurt kinds, touch boxes and planes are as in the original; the only handler that was missing is below.

## Found and fixed: checkpoint statues never ported (every death restarted at the very beginning)

- Types 0x200b..0x2014 (init FUN_100165f0, update FUN_10016670 disassembled with capstone, word-script interpreter FUN_1001e000). Statue n = (type + 6) & 0xf.
  The port had no handler (invisible, inert) and never set `W->cp`, so the respawn always used table entry 0 = path 0 depth 0, even after finishing path 1 or 2.
  Table 0x10038534 (level 8): cp 1 = path 0 d 0, cp 2 = path 1 d 0, cp 3 = path 2 d 0.
- New in `e3_cyclops.c`: `cp_init`, `cp_update`, `wscript` (FUN_1001e000 subset: frames, -1 loop, -3 flip, -9 delay, -6/-7 remove, -0x28/-0x27 sound), dispatch in
  `cyc_type_init` / `cyc_type_update`. Behaviour as the original: inactive script 0x10037c00 until Hercules is < 0x2bc rows from it on the same path; then
  chime 0x6028 (only if its state word is non-zero), active script 0x10037ba8 (frames 0xec..0xf3), `W->cp = n`; another statue becoming current resets this one;
  y = 0x1a00000; removed when offscreen. Statue 1 (depth 507) is within 0x2bc of the start, so it is active and chimes at tick 1 (original code does the same).
- `e3game.c`: test hooks only (`LV_CYC_TRACK` frozen-object report, `LV_CYC_GHOST` objects left on another path, `LV_CYC_H2` pool 2 / 4 handler table). Env `LV_CYC_CPLOG=1`
  prints `CHECKPOINT n reached`.

## Verified headless

- `E3_AUTO=1 E3_GOD=1 LEVEL_DONE_LOG=1 --level CYCLOPS --frames 11000`: `CHECKPOINT 1 t=1, 2 t=3598 (path 1), 3 t=7034 (path 2)`, chime 0x6028 logged each time, `LEVEL DONE at t=10495 next=TITANS`.
- Respawn: `E3_AUTO=1 E3_PATH=2 E3_DEPTH=100 E3_DIFF=1` (non-god): `DIE t=2614 path 2 d=21408`, then `pos p=2 d=584` (restart at the path 2 start). Before the change this went to path 0.
- Screenshot sequence (SHOT_SEQ, frames 16..48 from the start): the blue statue at the left kerb is visible and animated.
- Frozen-object scan (`LV_CYC_TRACK`): everything that stays still is waiting for its trigger (columns, team, people, flyers at rel ~595) or landed debris/masonry.
- Hurt log (`CYC_HITLOG`, god): kinds from 0x2b, 0x2e, 0x2a, 0x43, 0x48, 0x4033, 0x42, 0x2d, 0x3d all reached.

## Not verified / remains

- Real audio (dummy driver: voices are -1, loops cannot be judged; stop-on-remove is wired through `own`/0xffff as before).
- Checkpoint sparkle shower (0x201b children, FUN_100168b0 / 100168f0, gated by `byte gs+0x38 & 1`) not ported (cosmetic).
- `e3_offscreen` (e3game.c, shared by all ENG3 levels) never culls an object of another path; the original (FUN_1001b920, table 0x1004078c) culls the old path's objects
  right after a path change. Only a few ghosts (a pickup, debris) were seen in this level; not changed because the Cyclops places objects on the next path on purpose.
- The same missing checkpoint handler affects the other ENG3 levels (Gauntlet, Torment, same types 0x200b..); only level 8 is wired here.
- Barricade 0x48 / team 0x2f / floor 0x39 updates (10006ca0, 10003020, 10004100) have no decompile; they were not re-derived from disassembly.
