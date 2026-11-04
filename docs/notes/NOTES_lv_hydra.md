# lv_hydra: HYDRA (level 6) audit

Tree `work/lv_hydra` (start `work/base34`). Edits tagged `LV_HYDRA`. Files: `src/bosses_hydra.inc`, `src/bosses.c` (`boss_drop`).

## Spawn records / object types in the level
Map has only 2 records, both 0x31 (init 1001dd00: invisible arena-wall models, frame 9/10; update FUN_10022180 is a bare `ret`), which the
port correctly drops (the arena walls come from the level map; Hercules stops at x 926 / 3294). Everything else is spawned by the level start
FUN_1001ac10 (body 0x2b, floor 0x202a, necks 0x29, segments 0x4028, heads 0x2d, stump 0x30, rocks 0x33/0x2f, boulders 0x202e), all already
ported with their handlers (FUN_1001d0b0 / 1001b7f0 / 1001b370 ...). Audit by decompile + disassembly (capstone, ENG1.DLL) found these gaps:

## Fixed
1. **0x34 / 0x35 sprites never spawned** (FUN_1001ac10 ends with them; FUN_1001dac0/1001daf0 and 1001db40/1001db70, disassembled): bank 79
   sprites at (0x860 / 0x8e0, 0x3ee, 0x280), frames 0x50..0x5d / 0x60..0x67 advance every tick, z -= 7 per tick, removed below z 64.
   Now spawned in `hydra_start`, ticked in `hy_fx_tick` variant 7.
2. **First cut of neck 0/2/4/6 always drops id 9 (password vase)** (FUN_1001b370 mask DAT_1006f3d4); the port always used the random drop list.
   `H.cutmask` in `hy_head_hits`.
3. **`boss_drop` (FUN_100131c0 id rules) only mapped 9 -> coin.** Added: 4 health (first only; level 9 -> 3), 10 max-health vase -> 3 at 0xff,
   9 -> next password vase 5,6,7,8 in order then coins, a vase already held -> coin, coins14/18 totals. Affects Hydra, Medusa, Hades drops.
4. **Camera shake** (FUN_1001e140 -> DAT_10070cbc): 0x30000 alternating sign while the cliffs come down, 0x90000 for one tick when a quake rock
   (0x2f, FUN_1001e3f0) lands; written to `O->cam_dy` (same path Medusa's quake uses). Also the per-rock 0x202d dust puff of the quake.
5. Debug hooks: `HYDRA_LOG=1` (head cuts), `DROP_LOG=1` (drop id -> pickup type), `HYDRA_TESTDROP=1` (6 id-9 drops at start).

## Verified headless
- Build clean. `BOSS_WEAK=1`, script `S....S....S.....`: level completes (`LEVEL DONE at t=567 next=MEDUSA`), no crash during boulders / quake.
- Normal mode (8 heads) 5000 ticks with hits: heads are cut (7 -> regrow 3,1,5 -> ...), drops spawn, heads_left counts down.
- `HYDRA_TESTDROP=1 DROP_LOG=1`: ids give types 201a, 201b, 201c, 201d, then 2018 coins (original order).
- `--sfxlog`: no looping voices, no missing sound ids (`SFX_MISS`) in a full weak run.
- Screenshots: boulders on the arena after the win, 0x34/0x35 tiny sprites at start.

## Not done / remains
- **0x202c ambience/stage object** (FUN_1001eba0 init, 1001ebf0 / 1001ed30 updates): thunder sounds (0x18b / 0x190 / 0x19c random), per-cut
  lighting stage presets (tables 0x100588e0, 0x100589b8, 0x100589f8, 0x10058a20; DAT_1006fea0, lighting zones DAT_10071ae4, the model's +0x21 /
  +0x40 interpolation through FUN_10002c20 / 10002b30) are not ported (stage timer set to 0x3c on each cut is not stored). Visual/ambient only.
- Hit-test boxes remain the earlier world-space approximation of the screen-space boxes.
- Not run: unmodified full 8-head kill without BOSS_WEAK (the scripted player has no aiming), real audio/window.
