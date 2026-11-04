# lv_torment audit (TORMENT, ENG3 level 10)

Tree: work/lv_torment (start = work/base34). Edits tagged `LV_TORMENT`. Changed: `src/e3_torment.c`, `src/e3game.c`, `src/e3types.c`.
(`src/e3types2.c` still holds the old 0x47 handler, now shadowed because `e3t_type_init/update` is consulted first.)

## Spawn types in the level (E3_AUDIT) and result
| type | original init/update | state |
|---|---|---|
| 0x6 pickups | 10015440 / 10015680 | shared, unchanged |
| 0x29 / 0x2a / 0x2b / 0x3f | 10025700.., 10025960.., 10025d20.., 10028a40 | compared with decompile, matches |
| 0x2e (+0x4029, 0x43) | 10025ec0 / 10026340 | matches |
| 0x2f, 0x31 | 10026550 / 10026600, 10026b80 / 10026c60 | matches |
| **0x30 rock fall** | 100266a0 / **10026770** (no decompile, read from ENG3.DLL with capstone) | **FIXED** |
| **0x33 dropping soul** | 10026f00 / **10026fe0** | **FIXED (landing)** |
| 0x34 raft | 10027130 / 10027180 | matches |
| 0x35 coffin (+0x36 0x37 0x3b 0x2016) | 100272e0.. | matches |
| 0x3d / 0x3e | 100287b0.. | matches |
| 0x40 (+0x41) | 10028a90.. | matches |
| 0x45 wheels, 0x46 boulders | 10029140.., 100297d0.. | matches |
| **0x47 collapsing ceiling** | 10029e50 / **10029ea0** | **FIXED (debris)** |
| 0x48 spin hazard, 0x49 scenery | 1002a040/1002a090, 1002a180 | disassembled, matches (0x49: variants > 2 keep model 0 here, original leaves +0x34 untouched; no such record) |

## What was wrong
1. **Per-tick previous position was never stored for actors** (global). Original `FUN_1001b9f0` writes +0x9c/+0xa0/+0xa4 = current position right before each
   actor's update. The landing test `FUN_1001eaf0` / `FUN_10001600` uses that delta as its tolerance, so every faller (rocks, souls, bones, debris, boulders, 0x2a ghosts)
   used the spawn position (or 0). Fixed in the update loop of `e3game.c` (level 10 only, honours flag +0x50 & 0x800). Test hook `E3_TRACE=<type>` prints one type per tick.
2. **0x30 rock (120 spawns)** `t30_update` (now in e3_torment.c): was missing (a) five 0x3b bones thrown out ahead once the rock is within 0x484 (+0x11e flag),
   (b) on the first bounce a 0x2016 sparkle at depth - 0x800000 and four 0x4002 debris with the 4-case direction table, (c) it used its own gravity/floor code
   instead of `FUN_10001600` (`fall(W,o,1)`), so no real bounce chain / +0x9a flag.
3. **0x33 soul** used an ad-hoc `dn==0 || up<=0x24` landing test; now `e3_landed` (= `FUN_1001eaf0`), as the original.
4. **0x47 ceiling chunk**: the 16 debris were left to run the 0x4002 init on their first tick, which discarded the original post-init tweak (+0xbc >>= 1, +0xac >>= 4) and
   gave directions to all 16 (original: only first 8). Now initialised at once with the original tweak.

## Verified headless
- `E3_PATH=1 E3_DEPTH=12000 E3_GOD=1 E3_AUTO=1 E3_TRACE=0x30`: rock falls from y -1023, bounces (44, -5, 44 ...) and settles (st 4 -> 2, frame 3); screenshots at t=42/56 show the sparkle
  and bones on landing. 0x47 (path 0 d 1277, `E3_DEPTH=700`): ceiling chunk + debris screenshot, rumble. 0x402b boulders land identically to base34 (checked via E3_OBJS at t=100).
- Hurts still arrive from 0x30 (kind 0x31), 0x46 (0x34) in `E3_HURTLOG`.
- Level can be completed: `E3_RESCUE=1 E3_AUTO=1 E3_GOD=1 LEVEL_DONE_LOG=1` -> `LEVEL DONE at t=9399 next=VORTEX` (58 rescues). GAUNTLET / CYCLOPS still start and run (code is level-10 gated).
- `--sfxlog`: one looping sample (0x2aa3 wheel roll, owner = wheel, stopped by e3_remove owner stop); no SFX_MISS.

## Why it never finishes with the test hooks (not a game bug)
`E3_AUTO` pilot falls into the gaps of the zig-zag pieces: path 0 d 4096 / 4352 / 4608, path 2 d 3072-3200, path 0 d 18432-18816 (fall, `E3_GOD` does not stop falls). The pieces
are offset sideways by ~150 lateral with a 256 deep gap; the pilot's jump rules (and the hazard dodge, which flips target left/right near skull piles) miss them. Original geometry and
physics were checked in TORMENT2 (straight running jumps along one side work). `E3_RESCUE=1` puts the hero back on the last floor and completes the level. The pilot was NOT improved.

## Not verified / remains
- No full human playthrough, no real audio/window. Rock/bone/debris visuals are by screenshot at a few frames only.
- Pilot cannot cross the zig-zag unaided (see above); a scripted `U`+`J` crossing was not built.
- Shadows 0x2017 / semi-transparency still as noted in levels_notes (TORMENT section).
