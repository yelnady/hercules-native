# Levels: ENG3 maps, Titans, level flow (work/levels)

## Game order and engine per level

TITLE.DLL `0x10022288[level]` = engine path per level (ENGINE1 / ENGINE3); names at TITLE 0x21b60.
Next level: TITLE `FUN_10012070` (1→2→3→5→6→7→8→9→10→11→12 = finished; 4 is skipped).

| # | Name (TITLE) | Map dir | Engine |
|---|---|---|---|
| 1 | Training Ground | TRAINING | ENG1 |
| 2 | Training Gauntlet | GAUNTLET | ENG3 |
| 3 | Forest of Centaurs | FOREST | ENG1 |
| 4 | Nessus Battle | (none in HERCULES.FS; ENG1 table entry O:\NESSUS\ exists) | ENG1, never entered by the level order |
| 5 | Visit to Thebes | THEBES | ENG1 |
| 6 | Hydra Battle | HYDRA | ENG1 |
| 7 | Medusa Battle | MEDUSA | ENG1 |
| 8 | Cyclops Chase | CYCLOPS | ENG3 |
| 9 | Battle of Titans | TITANS | ENG1 (Pegasus) |
| 10 | Passageways of Torment | TORMENT | ENG3 |
| 11 | Vortex of Souls | VORTEX | ENG1 |

## ENG3 'i1.a' maps (src/eng3.c)

Header (FUN_1001af10): +8 nblocks, +0xa ngrids, +0xc npaths, +0x18/+0x1c VRAM images (same blits as ENG1
+0x14/+0x18), +0x2c texture table, +0x30 path table, +0x34 bg, +0x38 grid offsets (each `{u16 w, u16 rows}`
then row-major u32 cells), then 4 self-relative words (+4 = attribute table, +8 = light table) and the
block table (same mesh/heightfield format as ENG1).

Cells: bit0 = empty run, bits 1-10 block, 11-23 attribute, bit 24 flip, bits 24-31 (char & ~1) * -2 = height.

Path record (16 B): +0 parent (0x7f none), +1 parent row (0xfe/0xff = last row), +4 u16 heading offset,
+6 rel → {n, n × (grid, yoff)} (layers, y = yoff*32), +8 rel → connected paths (0xff end), +10 rel → per-row
signed turn byte.

Row records (FUN_10022790 / 10022690 / 10022710), ported literally: anchors in 4096 units, heading h
(4096/turn). Straight rows: world = anchor + (sin h·X − cos h·Z, cos h·X + sin h·Z), Z = (row − base)*128 +
2vz. Bent rows (turn byte repeated): bend around a centre 0x518 (1304) to the side of the turn
(FUN_10021200), 0x40 angle units per row, vertex angle + vz (FUN_10021a10). Vertex X = col*128 − 0x2c0 ∓ 2vx.

Floor (FUN_1001b2e0): column = (L + 0x300) >> 7 with L = X + 64, row = depth >> 7, heights from the ENG1
heightfield code, surface = yoff*32 − 2h − 2*(cell height & ~1). Minimum non-negative distance over layers.

Camera (FUN_10022070 / FUN_1001c720): along the path heading at the player, 320 behind, screen centre y =
0xe8 − 0x8a. Starts: ENG3 0x10038534 + level*0x18 → records {u16 depth, u16 y, u8 lateral, u8 path}
(read from ENG3.DLL at run time). Fog/bg: ENG3 0x10039860 + level*0x20 (+0x10 bg, +0x1c max, +0x1d shift).
Hero bank: P:\SRC3\HERO\HERCULES (50-61 run away, 184-195 run toward the camera, 9 stand, 82 jump).

## Titans (src/titans.c)

FUN_10015090 → FUN_100374c0 on level 9: player = Pegasus (bank 48, anims 0x10063530, state 0x28 handler
FUN_100379e0), wing/rider children 0x4028/0x4029 (2-byte scripts 0x10063318/0x10063338). Flight physics
FUN_100377f0, ground clamp FUN_10037770, scroll anchor actor 0x36 (FUN_1003aef0/1003af20, 4.0/tick, player kept
in [anchor−0x70, anchor+0xa0]). End: Zeus actor 0x45 (FUN_1003adc0) – scroll decelerates within 0xc8 of him,
touching him → 0x78 ticks → state 0x31 (level complete).

## Level completion

- ENG1: pickup-pool type 0x200a update FUN_10013880 (level-end object: Training x=31681, Thebes x=18912) →
  FUN_10019240 (victory flag) → victory script → op 0x2b → game state 0x31. Ported as an overlap test against
  the spawn record (objects.c hook) + `player_win`; `game_level_done` reads `P.signal == 0x2b`.
- Titans: Zeus as above. Forest: no 0x200a and no win call found yet (Nessus fight) → fallback: end of map.
- ENG3: the end event was not found in the REA output (probably in the ENG3 hero script interpreter);
  fallback: last floor row of a path with no continuation, or of a path whose continuation starts with a gap
  (Cyclops arena).

Not done: Hydra/Medusa arena floors (moved to the bosses agent), ENG3 objects/enemies/hazards, ENG3 hero
script/animation port, Titan/god/harpy AI, Nessus.

## ENG3GAME (work/eng3game): ENG3 gameplay, Pegasus, Nessus findings

**ENG3 player** (`src/e3game.c`, replaces `eng3play.c`):
- Port of the state table DAT_10041c80, filled by FUN_100178a0:
  - 0 stand FUN_10018600, 1 run FUN_10018950, 2 jump FUN_10018eb0, 3 fall FUN_10019030, 5 hit FUN_100195d0
  - 6 die FUN_10019b70, 8 power-up run FUN_10018880, 9 won FUN_10019cd0, 10 end run FUN_10018c90
- Hercules auto-runs at 10.0/tick (DAT_10038038). Up = 16.0, Down = -4.0 (5.0 near the camera). Left/right steer at 4.0.
- Jump: vy 0xba000, gravity 0x6000, rise decay 0xa000; releasing jump takes off 1.0/tick.
- Hero scripts read from ENG3 0x10037c38.. by FUN_10018370.
- Floor and landing: FUN_1001b2e0, FUN_1001e8e0, FUN_1001eaf0, FUN_1001ea60. The surface is 0x40 above object y.
- Attributes FUN_1001b4a0 (shape table = blocks + 4*(n14+n12+n10+n8+nblocks)). Effects FUN_10017ae0:
  - 0x52/0x56 side walls: push and hit kind 0x10/0x20
  - 0x5d/0x5e steering locks
  - 0x73 hit
  - 100/0x65 slow zone
  - 0x76 end of level
- Path changes: attribute flag table (level entry +0x44); bit 0x100 means switch to path (low byte), with depth -= parent_row*128 (FUN_10022b30 / FUN_10022620).
- Damage: FUN_10019410 table 0x100380a0 + difficulty*0x20. Invulnerability 0x40 ticks. Death FUN_10019a30.
- **Original level end:** attribute 0x76 spawns end object type 7 (FUN_10016240 / FUN_10016320). It slows Hercules (state 10) and the camera object decelerates. When depth passes 0x3c00 (Gauntlet), 0x7780 (Cyclops) or 0x7400 (Torment), FUN_10019c20 runs: victory script 0x10037ea0, then op -3, then done.

**ENG3 objects** (`src/e3world.h`, `src/e3types.c`):
- Per-level tables: 0x1003b900 / b948 / b990, 0x48 bytes each, same layout as ENG1 0x10068af0.
- Spawns per path: path record +0xc count, +0xe rel to 8-byte records {depth, y, lat, template}. Templates are 10 bytes at level entry +0x40 (FUN_1001c010).
- Activation: player's path window (FUN_1001c290). Sprite banks: table 0x10039548 (0x14 per entry, 1-9). Bank 0 = map models.
- Model format (FUN_10023ef0):
  - Record = mdl_tab + 4m + rel, with mdl_tab = blocks + 4*(nblocks-1). Fields: {s16 poly index (rel hdr+0x28, *4), u8 nbox, u8 radius, s32 offset, boxes 8 B, vertex groups {s16 n, n x u32 (x=s16 mid, y=s8 hi, z=s8 lo)}}.
  - Faces are 8 bytes, listed cyclically (GPU order v0 v1 v3 v2). Scale 0x100 = x2.
- Ported types:
  - 6 pickups (FUN_10015440/680, all 16 variants)
  - 7 end object
  - Gauntlet: 0x2e spiked turnstiles (1000f460/7b0), 0x2f rolling ram (1000f980/fab0), 0x3e swinging boulder chain (10012500/820/aa0), 0x40 swinging targets (1000ebe0/ec60)
  - Cyclops: Cyclops chase 0x29. Spawn FUN_10003290 (0x680 ahead), init FUN_10003200, per frame FUN_10003610 states 0x41-0x48 driven by attributes 0x53/0x54/0x57/0x5b, z scale 0x7fa.
  - Torment: 0x30 falling rocks (100266a0/770), 0x33 dropping souls (10026f00/fe0)
  - Every other type: generic. Animation list 0 plays, and the type hurts on frame-box contact using its hit kind extracted from its handler. Bank-0 model types without a port are invisible.

**Titans** (`src/titans.c`):
- Pegasus damage FUN_10037e30 (table 0x10055348 [level + diff*8]). Hurt state 0x24 (anim 1, then 15 ticks invulnerable, FUN_10037230). Death FUN_10037080 (anim 8, lives - 1, respawn). Enemy contact damage arrives via player_damage → P.hit_req.
- Sword (anim 2, state 0x48) exposes the Pegasus frame box 2 to enemies through a `Player.ext_box` hook.
- Zeus: exact FUN_1003adc0 test, Pegasus box 1 vs Zeus box 1, then sound 0x627b and 0x78 ticks.
- Titan attack AI not ported: 0x28 rock FUN_10038090/10038100/10037fe0 with rock 0x39 (10036d10/d70) and debris 0x3a; 0x29-0x2b; projectiles 0x31-0x34 (FUN_10039530 / FUN_100398b0 call FUN_10037e30). The titans are camera-relative background actors: draw callback +0x23 = 6, x = (DAT_100707f4 - camera)/6 + 0x190.

**Nessus** (found, not ported):
- Forest actor 0x2f, bank 68 (SRC1\ALIEN2\NESSUS\Basic; banks 69-75 Punch / Kick / Look / Ride / Reject / Finish / Defeat). Init FUN_1000bb10, update FUN_1000bc50 (475 lines).
- Trigger: Forest actor 0x53 at x=18520 z=512 (init FUN_1000c780, update FUN_1000c7e0). When Hercules passes it, it spawns Nessus 0x140 ahead (FUN_1000c730) once DAT_1006ef14 / DAT_1006ef38 == 5; those flags are set by the Meg sequence (writers near decomp lines 6128-6280).
- Fight: walk / punch (0x16) / kick (0x46). The grab-and-throw sequence is 0x44 → 0x45 → 0x41 → 0x42 (FUN_1000bb90 carries Hercules) → 0x47. When DAT_10070832 < 1 he is dazed (0x43). A power punch with charge >= 0x65 then defeats him (0x23), and FUN_10019240 = player_win is the real Forest end.

## ENG3B (work/eng3b): original camera, more ENG3 object types, platforms, flow fixes

- **"Near floor missing" was not a renderer bug.** Hercules stands still at the start (state 0 needs Up/Down) and the old camera sat 320 behind depth 0, where the map has no rows. It is fixed by porting the original camera.
- **Camera = a camera object** (FUN_1001c720 init, FUN_100222c0 follow, FUN_10022070 view).
  - Fields: cd/cy/clat = DAT_100405e0/e4/e8. The flags (DAT_1004063a) are 0x2a at start and 0x3e in play.
  - **Depth.** err = ((hd − cd) + 0xa0)*2 − 0x140 − cam/2; 2 iterations.
  - **y.** err = −0x40 − 0x168 + ((hy − cy) + DAT_10040620)*2; 2 iterations (Torment 1).
  - **Lateral.** Follows the hero clamped to the camera-track lane; 1 iteration.
  - **Step tables.** Each step = number of entries < |err| in u16 thresholds at 0x100399f0 / 0x10039a18 / 0x10039a40.
  - **Floor push.** cy −= FUN_1001b2e0(cd, cy−0x14).hi (the distance to the floor above).
  - **View.** Camera point (cd, clat); y = cy − 0x74; GTE offset y = DAT_10040620 − 0x8a; H = 0x140.
  - **DAT_10040620.** = 0xe8 − track[4] every frame (FUN_100180e0 → FUN_10018350); Gauntlet starts at −0x100, Torment at 0xc4.
  - **Eye placement.** The eye is put 0xa0 behind the camera point. This is inferred, not decoded: DAT_1004061c = 0xa0 offsets the follow error, and with it sprites and geometry match the 1:1 scale of the other levels.
- **Camera track** (FUN_1001b650).
  - Table: DAT_10040670 = blocks + 4·(hdr16[8]+[0xe]+[0x10]+[0x12]), count = hdr byte 0x14.
  - Each entry is self-relative → {u32 n, n×16-byte keys {u16 depth, A, B, C, D, s8 E @10, s8 F @12}}.
  - Interpolated: lane = [A−C, A+C] − 0x300, height B+D, camera height E.
  - Track index per path (FUN_100174e0): Gauntlet = path; Cyclops 0/3/6 (alt +1 via attribute 0x50, back via 0x51); Torment {4,0,1,2,3}.
- **Path change** (FUN_10022620). Also moves the camera, and subtracts parent_row·128 for every value; the previous port treated 0xff as 0, which broke the Cyclops paths 1-3.
- **Negative depth.** FUN_1001b2e0 treats depth > 0x7fff (u16) as row 1. This is now ported in eng3_floor, so landing on a new path just before its row 0 works.
- **Standable objects** (FUN_1001e8e0 / FUN_1001e540).
  - Objects with +0x50 & 0x200 (the DAT_10040d20 list) are tested before the map floor, using frame or model box 1.
  - Ported in ground() and landed(); the platform carry links (+0xe4/+0xe8) are not ported.
- **Type handler tables.** Child classes (0x40xx) are in thand4 (8 bytes {init, update}); most are no-ops. Calls `DAT_10040b98 + 8·ix` / `DAT_10040b8c + 12·ix` are those inits.
- **New types (e3types2.c):**

  | Level | Type | What | Functions |
  |---|---|---|---|
  | Gauntlet | 0x30 | ram from the right, two 0x3f posts | 1000fec0 / 1000ffd0 |
  | Gauntlet | 0x3f | ram posts | 10012e40 / 10012e80 |
  | Gauntlet | 0x48 | rotating double blade | 10012be0 / 10012c40 |
  | Gauntlet | 0x49 | spinning hazard | 10013cb0 / 1002a090 |
  | Gauntlet | 0x4b | scenery | 10013d70 / 10013e00 |
  | Torment | 0x34 | floating raft, standable | 10027130 / 10027180 |
  | Torment | 0x47 | collapsing ceiling + rumble + debris | 10029e50 / 10029ea0 |
  | Torment | 0x48 | spinning hazard | 1002a040 / 1002a090 |
  | Torment | 0x49 | scenery | 1002a180 / 10013e00 |

- **Password vases** (type 6 variant 5). The bit depends on level and path (Gauntlet by path, Cyclops by count, Torment by path), as in FUN_10015680.
- **No checkpoints in ENG3.** gamestruct+1 / +2 are only set by the demo loader (FUN_1000ae00), and death (FUN_10019a30) restarts from +2. The restart-at-start behaviour is therefore faithful.
- **Test hooks:**

  | Variable | Effect |
  |---|---|
  | E3_AUTO[=look] | test pilot |
  | E3_AUTODBG | pilot log |
  | E3_HOP | pilot jump-hold ticks |
  | E3_GOD | no damage |
  | E3_FLOORMAP / E3_FLOORROWS | ASCII floor map |
  | E3_AUDIT | spawn type list with handler addresses |
  | E3_SPAWNS=path | spawn list for one path |
  | E3_EYE | eye distance override |

- **Torment findings:**
  - The rows 30-37 zig-zag pieces meet exactly at lateral 0 (no overlap); a player hugs the centre line. The pilot gets through.
  - Paths 3/4 rows 24-31 are 128-deep stepping stones at lateral −256..−129 / 128..255 with 128-deep gaps.
  - The pilot cannot reach lateral < −116 there; the hero stops at −116, cause not found. In-game the drawn stones appear ~64 further in than the collision cells: sprite X = lat−64, faces X = col·128−0x2c0 ±2vx. This needs checking against FUN_10021a10 before trusting either side.

## TITANS2 (work/titans2): Titans actors, Pegasus weapons

All level-9 actors are ported in `src/titans_ai.c`. The hook is in `objects.c` `spawn_type`: level 9, actor pool → `titans_ai_init`, with kind `K_TITAN`, and the tick and draw are dispatched from there. The level-9 handler table is 0x10061f80, as `{init, -, update}` triples; the type info is at 0x10061840.

The actors read a shared context, `TitansCtx`, which `titans.c` fills every tick. It holds the ENG1 globals they use:

| Field | ENG1 global |
|---|---|
| anchor | DAT_100707e4 |
| scroll speed | DAT_100707e8 |
| anchor z push | DAT_100707f8 |
| camera x | DAT_10070ca0 |
| frame counter | gamestruct +0x38 |
| Pegasus box | +0x88 |
| sword box | DAT_10071440 / DAT_1006fec4 |
| Pegasus screen position | +0xc / +0xe |

**Screen-space objects.**
- flags54 0x40000000 means the object is drawn by the FUN_10003cd0 path. Its x and y are 320x240 screen coordinates.
- Draw mode +0x23 = 6 is FUN_10004c10: a sprite scaled by +0x3a and rotated by +0x74.
- Depth comes from the ordering table at +0x3e: 0x4bb is behind everything and 4 is in front.
- New `render_sprite2d` in render.c.

**Background Titans.** They sit in screen space at x = (camera at appearance − camera)/6 + 400 (/12 for the tornado). DAT_100707f4 means only one Titan is present at a time. They cannot be damaged: no handler tests them against the player's attacks. State 0x37 is the sky-palette fade (FUN_10005c70 towards 0x100636f0 / 0x10063a78 / 0x10063ef8 / 0x10064270). The fade is not drawn, but its counter drives the state change.

| Type | Titan | Init / update | Behaviour |
|---|---|---|---|
| 0x28 | rock | 10038090 / 10038100, picker 10037fe0 | Anim 4 frame 8 throws rock 0x39 at the screen (10036d10/d70). On impact (10036e90): sound 0x252, 14 debris pieces 0x3a (10036df0/e10) around the anchor, and camera shake table 0x100638f8. |
| 0x29 | ice | 10038590 / 10038610, picker 100384c0 | Animation only. The ice falls from the 0x51 generators (see below). |
| 0x2a | lava | 10038f50 / 10039090, picker 10038ea0 | Init raises three lava pillars 0x31 (10039360/393b0). The fade-in sends 0x56 blobs racing across the foreground (10039c20/c50; FUN_1003c510 screen-box hit, damage 2). Anim 3 index 0xd sprays 4 × 0x33 (100396e0/9750). |
| 0x2b | tornado | 10039f20 / 1003a040, picker 10039dc0, wobble 10039ff0 | The fade-in drops 0x57 rocks (1003aa10/aa70) and leaving drops 0x5b rocks (1003aaf0/ab50). FUN_10039d40: a sword blow stops a rock and may drop a pickup; a touch does damage 2. |

**Other attackers.**
- **0x51 ice generator (10038b00/8b30).** Every 16 frames, while the anchor is within 8 slots, it drops a 0x52/0x53 ice block, a model (10038950 / 100389c0). A touch does damage 2 and shatters the block into 12 × 0x68 (10038880, 10038740/87f0). A sword blow also shatters it and may drop a pickup.
- **0x35 (100399c0/99f0).** Launches 0x34 tornado arms (10039870/98b0). These are in screen space, spin, and hit Pegasus' screen position within 50 px for damage 2.
- **0x37 (10039a70).** Lava ball 0x32 (10039460/9530) from below, damage 3.
- **0x3b/0x3c (10039bf0/9c00 → 10039b60).** Three lava columns 0x3d (10039ae0/9b00), damage 2.
- **0x3e (1003a610/a700).** A spinning pillar of 6 orbs 0x3f (1003a530/a550), damage 2. Its mesh 0x40 is not drawn.
- **0x44 (1003a980).** Rocks 0x41 hurled from below (1003a7a0/a810).
- **0x46/0x48/0x4b markers (1003b050/b0b0/b140).** Anchor z push +0x80 / +0x20 / −0x80, giving the camera's dolly in and out.

**Harpy waves.**
- **Wave list 0x61 (1003b9d0/ba00).** The list comes from 0x10064d28[DAT_1006fff4], which starts at 0x10065580[checkpoint].
- **Formations 0x62–0x67 (1003b7f0, 1003b530, 1003b640, 1003b730, 1003b8b0/b970; update 1003b980).** They hold the harpy 0x5c (1003b190/b310) with these helpers:
  - movement-command lists 1003c230 (`{delay, cmd, arg}`; cmd 1/2/3 velocity, 5 anim, 6/7/8 acceleration, 9 jump, 10 state)
  - physics 1003c330
  - hit/touch 1003b200
- **Rewards.** The last harpy of a formation drops the reward from 0x100655a8 (1003c460).

**Spy 0x5d (1003bc20/bde0, hit 1003bcb0).**
- It brakes the scroll to 0 at anchor + 0x110 and fights with command lists 0x100651f8 / 0x100652e8.
- 4 hp. It drops 0x241 per hit and 0x24a + 0x243 on death, then the scroll speed recovers.

**Gods 0x2c–0x30 (1003abd0/ac80).**
- Each waits for Pegasus to touch it (sound 0x627b).
- Then it flies off up and right, dropping gifts (1003c490 → 1003c410).
- Zeus 0x45 shares the init and animates; his touch test stays in `titans.c`.

**Pegasus weapons (FUN_100379e0).**
- The selected Gift is DAT_1006efb4 = `P.weapon`: 0 sword, 1 fireball, 2 lightning. L1/R1 select.
- **Sword.** Anim 2, state 0x48.
- **Fireball.**
  - Anim 3 → state 0x49 → anim 4, state 0x4a, held.
  - Stream at +0x34/−0x14, angle 0x300.
  - Ammo is spent per FUN_100371a0, and the stream stops through FUN_1001a410.
  - Anim 5 → state 0x4b.
- **Lightning.** Anim 6, state 0x4c → anim 7, state 0x4d, spawning bolt 0x4005 at +0x20/+0x14. Then anim 5.
- **Pegasus' children.** The wing and horse children are drawn every frame.

**Other changes in this pass.**
- **Respawn.** The flight restarts with fresh actors and the checkpoint is kept (`place_start`).
- **Camera.** The camera z follows the anchor z.

**Not ported.**
- Sky palette fades.
- The 0x40/0x42/0x43 sprite meshes (FUN_10026700, draw mode 4).
- Semi-transparency on the lava/tornado sprites (flags54 low bits).
- The 0x402a hit spark (spawned but has no handler).
- The 0x5e–0x60 actors (nothing spawns them in level 9).
- The Titan sound-bank swaps FUN_10003870/10003520.
- Pickup drops use the shared enemies.c pickup table, not the full FUN_100131c0 flag logic.
- The hit test for Pegasus' weapons against actors uses world boxes, plus a ±14 box around each fireball/lightning effect object.

## CYCLOPS (work/cyclops): the Cyclops chase completed (`src/e3_cyclops.c`)

- **No separate arena or boss fight exists in ENG3.** Path 3 of the Cyclops map has no floor, attributes or spawns. The level ends through the normal end object (attribute 0x76 → type 7, depth > 0x7780 on path 2, then FUN_10019c20). The Cyclops' state 0x48 ("defeated", anim 3) is only entered when gamestruct[3] == 2, which the Cyclops level never sets. The defeat is told by story video 19.
- **Path 1 rows 139-140 gap.** It is a real pit: attribute 0x75 covers rows 136.5-144.25. When the hero falls below y 0x1f40000 there, FUN_10017ae0 spawns type 0x41 (FUN_100284a0 / FUN_100059d0). That pins the hero and calls FUN_10019a30 (death). This is ported via `cyc_attr_effect`. Jumping at full speed clears the gap.
- **Ported types (level handler table 0x1003b948):**

  | Type | Object | Handlers |
  |---|---|---|
  | 0x2a | falling masonry; model from table 0x10034fe0 | 10004580 / 10004650 |
  | 0x2b, 0x2e | toppling 4-piece columns (0x4029); start when the Cyclops stomps | 10001aa0/1fe0 + FUN_10001e10; 10002580/2ac0 + FUN_100028f0 |
  | 0x45 | toppling columns (pieces 3/4/0x28/0x29) | 100062f0 / 100067d0 + FUN_100066c0 |
  | 0x2d | sliding cart | 100023a0 / 10002440 |
  | 0x2f | runaway team (3 × 0x4030) | 10002eb0 / 10003020 |
  | 0x30 | column the Cyclops pulls down (model 0x21) | 10003340 / 100033f0 |
  | 0x35 | people crossing (0x402c) | 10003bf0 / 10003dc0 |
  | 0x39 | collapsing floor, 18 slabs, models at 0x10034f98 | 10003ef0 / 10004100 |
  | 0x3d | swooping flyer | 10004c10 / 10004ca0 |
  | 0x3e | flyer carrying boulder 0x43 | 10004dc0 / 10005070; 0x43: 10005e50 / 10005ee0 |
  | 0x3f | crowd spawner, crowd 0x4033 in the camera-track lane (tracks 2/5/8) | 100053a0 / 100053f0; 0x4033: 100056a0 / 10005760 |
  | 0x42 | fleeing citizens | 10005a20 / 10005b40 |
  | 0x47 | drifting embers | 10006af0 / 10006b90 |
  | 0x48 | barricade (model 0x34); hurts 0x36, smashed with the speed power-up into 16 × 0x4004 | 10006c20 / 10006ca0 |
  | debris 0x4002 / 0x4003 / 0x4004 | — | 10014d90/4f00, 10014fd0/5110, 10015170/52c0 |
  | dust 0x2016 | — | 10014b30 / 10014bb0 |

- **The Cyclops (FUN_10003610)** now:
  - keeps pace with the *camera* depth delta (DAT_1003fb94 - DAT_1003fb98, ignored below -0x180);
  - moves to the next path past depth 0x7f7fffff;
  - shakes the camera on stomps (DAT_100405fc oscillation for 0x50 ticks, now `W->camshake`, added to the view y);
  - spawns its pulled column 0x30 at frame 0xa1 and the handler-less object 0x38 at frame 0x20.
- **Object gravity:** FUN_1001f690 (3-axis move), FUN_10001460 (level 8: bounce at street y 0x1c00000, 0x1400000 in path 0 depth 0x3700-0x4100) and FUN_10001600 (bounce on the floor through FUN_1001eaf0, model-box y adjust).
- **Not drawn:** ground shadows 0x2002 / 0x2017, and the colour variant field +0x1c of crowd sprites. FUN_100013a0 (per-object push callbacks via table 0x10033078) is not ported.
- **Respawn fix in e3game:** objects are cleared *before* player_place, which previously removed the freshly spawned Cyclops, and the camera is re-initialised.

## GAUNTLET (work/gauntlet): the remaining Gauntlet types, carry link, standable list, pilot

**Types ported** (`src/e3_gauntlet.c`, dispatched first from `e3_type_init` / `e3_type_update` when level == 2). Addresses are init / update.

| Type | What | Functions |
|---|---|---|
| 0x29 | crushing stack, model 2 + two 0x4029 blocks; drops when Hercules is 0x4c8 away, shakes, 3 sparkles, rises again | 1000d280 / 1000d410, child 1000f3a0 / 1000f3d0 |
| 0x2a / 0x35 | column of four stones (model 3) swaying, toppling when Hercules is within 0x280; stones fall, bounce, burst into 0x4002 debris | 1000d7d0 / 1000dc20, 1000e020 / 1000e2f0, placement 1000dae0 |
| 0x2b | stampede of N goats (spawn byte +0xc = N, model 0xd); N == 1 is the big goat that the power-up run smashes (16 × 0x4004, bonus 8) | 10010370 / 10010590, separation 10001190 |
| 0x2c | target dummy sliding across on rope posts 0x4a; touching it smashes it (8 × 0x4001 frames 0x26b..) | 1000ee40 / 1000eeb0, posts 10013d00 / 10013d30 |
| 0x2d | four dummies wandering across the path, turning at attributes 0x5d / 0x5e | 10010b10 / 10010df0 |
| 0x32 | leaping fish arcing over the path | 100113f0 / 10011470 |
| 0x37 | hanging chain (models 0xc) with a blade (model 0xb), swinging | 10011760 / 10011a80, placement 10011b90 |
| 0x3a | Phil by the path, speech chosen by Hercules' health (FUN_1001faf0 clips 0x16 / 0x19..0x1c) | 10011ef0 / 10011f90 |
| 0x3c | flaming hoop bobbing up and down: through the middle releases the prize 0x38 (bonus 8), the rim hurts (0x40) | 10012200 / 10012250, 0x38 10011d30 / 10011d80, 0x41 1000ee00 |
| 0x43 | villagers walking to and fro, cheering when Hercules passes | 10013010 / 100130e0 |
| 0x44 | dummy charging straight at Hercules | 10013330 / 100133b0 |
| 0x46 | flock of five birds (0x45) taking off, respawning | 10013840 / 10013860, 0x45 10013a40 / 10013a90 |
| 0x47 | villagers standing, shouting now and then | 10013b30 / 10013c10 |
| 8 | bonus dropped by 0x2b / 0x38: password vase (bit by path), extra life or health by its script | 10015e90 / 10015f30 |
| 0x2016, 0x4001, 0x4002, 0x4004 | sparkle and debris | 10014b30 / 10014bb0, 10014c30 / 10014d40, 10014d90 / 10014f00, 10015170 / 100152c0 |

- Physics helpers: FUN_1001f690 (all three speed axes), FUN_10001600 (fall/bounce; the Gauntlet's ground is the absolute y 0x1c00000), FUN_10001460 (bounce on the map floor), FUN_10015340 (floor distance).
- FUN_1001fab0 is the host random callback (DAT_10040834); FUN_10008120 is a texture animation, not a rumble.

**Standable list (fixes ENG3B).** FUN_1001b9f0 builds DAT_10040d20 from actors with +0x50 & 0x240000 and child-class objects with +0x50 & 0x40000. Using every 0x200 object put Hercules on top of villagers and dummies.

**Carry link** (FUN_1001e8e0 head, FUN_1001e830, FUN_1001e440 / FUN_1001e4c0), in e3game.c `ground()`:
- Landing on an object links them: rider +0xe4 = platform, platform +0xe8 = rider.
- While Hercules stays over its depth span he keeps the platform's top y; otherwise the link is dropped.
- The original only follows vertically.

**Deviations**
- Separate ENG3 pools (classes 0, 0x2000, 0x4000) became one bigger pool: E3_MAXOBJ 400.
- Smashed dummies are marked gone for good. The port's spawn window would otherwise bring them straight back; the original window (DAT_1004078c rows) isn't decoded.
- Not ported:
  - shadows 0x2002 / 0x2017 / 0x2018 (FUN_1001d600)
  - 0x2d's per-kind callbacks FUN_100013e0 / 13a0
  - 0x46's texture animation
  - the hoop's player +0xd8 counter (the port uses that field for fall height)

**Pilot (E3_AUTO).** `e3g_pilot` steers by predicting where hazards will be. It times the 0x2f / 0x30 rams with a copy of their state machine, then keeps running, speeds up or hangs back.

**Test log.** `E3_HITLOG=1` logs every hit taken.
- **Reproducible completion runs (work/cyclops)** with `D=<HERCULES.FS>`:
  - God mode, from the normal start: `E3_GOD=1 E3_AUTO=1 LEVEL_DONE_LOG=1 ./build/hercules --data $D --level CYCLOPS --frames 16000 --script "." --shot x.bmp` → `LEVEL DONE at t=9925 next=TITANS`, no deaths.
  - Damage on, same without `E3_GOD`, `--frames 60000` → `LEVEL DONE at t=39090` after one game over and continue.
  - The level-8 test pilot jumps when the floor directly ahead disappears, prefers the floor nearest the street centre, and only jumps obstacles when there is floor 0x100–0x180 ahead.

## TITANS3 (work/titans3, from base14): finishing level 9

**Flight clamp (FUN_10037770).**
- The clamp was not a floor probe. It reads the map's height-limit tables (FUN_10020ea0, DAT_10070d4c).
- Location: `blocks + 4*(u16@0x12 + u16@0x10 + u16@0xe + u16@0xc)`. The table count is u16@0xa.
- Each table is a self-relative offset to `{u16 n, pad, n × 16-byte keys {u16 x, u16 centre, u16 ?, u16 height, …}}`. Keys are interpolated by x and give top = centre − height/2, bottom = centre + height/2 (on Titans the height is 0, so both are lines).
- Pegasus y is clamped to [top of table 0, bottom of table 1 − 0x40].
- FUN_1003af20 also clamps the anchor to bottom(table 1) − 0x10.
- Ported in `titans.c` (`limit_table`). Pegasus can no longer sink out of the level.

**Sky palette fades (FUN_10005c70 → palettes 0x100636f0 / 0x10063a78 / 0x10063ef8 / 0x10064270).** These have no visible effect in the PC build:
- They write one 15-bit CLUT row of the 32-row table DAT_1007348c. FUN_100058b0 uploads that table as a PSX LoadImage to VRAM (768,224) (rect at 0x1004d810).
- The PC screen is an 8-bit DirectDraw surface. Its palette is set only from the level CIF (HERCULES.EXE FUN_004068c0 / FUN_004069a0 → SetPaletteEntries). FUN_00406780 is the only per-entry setter and is called only inside the EXE.
- Sprite and texture bytes go straight to the screen, as confirmed below: sprites use the CIF palette's upper half, not their bank CLUT.
- So the fade state machine stays as a timer, and nothing is recoloured.

**Sprite colours on PC.** Every level-9 bank's colours in 0x80..0xff equal the TITANS CIF palette's upper half (Pegasus 111/111, Hercules vs THEBES 111/111). The spy bank (64, `ALIEN2\Spy`) has a 33-entry stub palette with nothing in 0x80..0xff, so drawn with its own palette it was a black silhouette. `titans_ai.c` `pal_fixed()` gives such banks Pegasus' palette, which is the level-9 sprite palette. The same applies to any level: a stub-palette bank should be drawn with the level palette; the generic fix belongs in sprite/render.

**Sprite meshes, draw mode 4** (0x40/0x42/0x43, init FUN_1003a730 / 1003a8a0 / 1003a910 + FUN_10026700).
- +0x5c points at 16-byte records `{s32 x, s32 y, s32 z, s16 frame, s16 end}` (−1 ends the list; that record is not drawn), at 0x10064510 / 0x100646e0 / 0x100647d0.
- The frames are the 8×8 star sparkles 1..5 of the Pegasus bank. They spin about y by +0x72: −0x17 / −0xf / −8 per tick (FUN_1003a770 / 1003a8e0 / 1003a950).
- 0x3e (FUN_1003a610) also spawns its 0x40 mesh.

**Semi-transparency.** FUN_100311a0 / FUN_10004010:
- flags54 bit 2 = semi-transparent prim (0x2e).
- bits 0-1 = PSX blend mode.
- bits 0x600 = light-table shading.
- +0x4a = brightness (0x80 = 1.0).

The inits set:

| Object | Init | flags54 bits |
|---|---|---|
| 0x31 | 10039360 | \|5 |
| 0x32 | 10039460 | \|5 |
| 0x33 | 100396e0 | \|7 |
| 0x34 | 10039870 | \|5 |
| lava Titan | 10038f50 | \|5 |
| tornado | 10039f20 | \|4 |
| 0x56 | 10039c20 | 0xc0080005 |

**Hit spark 0x402a** (effect pool, init FUN_1003bbe0): scale 0x180, +0x24 = 0x40. Its 2-byte script at 0x100651e0 is frames 0x3e..0x45 of the harpy bank. It is spawned at the harpy by FUN_1003b200.

**Drops.**
- FUN_1003c410: the screen y of the dropping object adds direction flags (< 0x29 → |0x20, ≥ 0xc9 → |0x240), then calls FUN_100131c0.
- That creates bouncing pickup 0x4012. Its level-9 update (FUN_10013570): x += scroll speed, catchable as soon as it falls (+0x9a = 0 while vy > 0), collected on Pegasus' +0x88 box, gone after +0x97 ticks or 0x100 off screen.
- Ported with the id rules of `drop_pickup` (enemies.c) plus this motion.

**Checkpoints.**
- FUN_100139f0 tests the player's +0x88 box. On level 9 that is Pegasus' body box, so `objects.c` now passes it on level 9 (marked TITANS3).
- On respawn, the actor context (anchor / Pegasus position) is filled before the actors spawn. Previously it was 0, which spawned the spy at z = 8, unhittable, and it held the scroll at 0 forever.

## TORMENT (work/torment): Passageways of Torment completed

### Fixes to shared ENG3 code (all ENG3 levels)

- **Attribute shapes (FUN_1001b4a0).** The stride table 0x10039a68 is indexed directly by a / b / c, as in ENG1 FUN_10020d30. The port used index − 1, which shifted every attribute shape.
- **Model collision boxes (FUN_10024870).** Box words use the same field order as the model vertices:
  - lateral = bits 8–23
  - y = bits 24–31
  - depth = bits 0–7
  - The sizes follow the same layout.

  The old decode swapped the axes. A Torment raft came out 96 units tall with its top at y −32, so it could not be landed on.
- **Lateral offset.** Map faces are drawn at X = lateral (FUN_10020ed0: col·128 − 0x2c0 ∓ 2vx), and the floor test uses (lateral + 0x300) >> 7. Objects, the hero and the camera were drawn at lateral − 64, which put sprites half a cell beside their collision. The −64 is removed in `obj_world`, the camera and `G->E3.X`.
- **Respawn.** The camera now restarts with Hercules on respawn. Before, it stayed far ahead for several seconds.

### Stepping stones, paths 3/4 rows 24–31

- **What looked like a bug is the design.** Columns 4 and 7 carry attributes 0x5e and 0x5d (FUN_10017ae0 side probe at depth + 0x80, lateral ± 0x10, Torment and Cyclops only). These lock steering outward, so the stones are not a route.
- **The route is the rafts.** The centre is the soul river, attribute 99, and the 0x34 rafts float on it.
- **Falling into the river.** Below y 0x7bffff it spawns type 0x3c. Once y > 0xa9ffff, Hercules is killed with 24 bone souls (FUN_100284f0); the case 0x77 behaves the same way.

### New handlers in e3_torment.c (Torment table 0x1003a5f8, 12-byte {init, upd, upd2})

| Type | What it is | Functions | Children / spawns |
|---|---|---|---|
| 0x29 | ghost carrier | 10025700 / 10025770 | escort 0x3f (10028a40 / 10028a80); drops up to 5 × 0x2a |
| 0x2a | falling ghost | 10025960 / 100259e0 | lands as 0x2b soul splash (10025d20 / 10025d80), hurt kind 0x30 |
| 0x2e | flock of four 0x4029 lost souls on sine paths | 10025ec0 / 10026340 | 0x43 fading trails (10025e30 / 10025e90); the flock ends at attribute 0x86 |
| 0x2f | wailing soul rising | 10026550 / 10026600 | |
| 0x31 / 0x32 | drifting souls, hurt kind 4 | 10026b80 / 10026c60, 10026d50 / 10026de0 | |
| 0x35 | coffin | 100272e0 / 10027370 | 4 × 0x2016 soul sparkles, lid 0x36 (100279f0 / 10027b50, hurts 0x33), 4 × 0x37 (10027c10 / 10027d70), 12 × 0x3b bones (10028240 / 10028390) |
| 0x3d / 0x3e | skeleton arm grabbing from the wall | 100287b0 / 10028840, 10028960 / 100289a0 | 0x3d hurts kind 2, 0x3e kind 0x2e |
| 0x40 | skull pile bursting | 10028a90 / 10028ae0 | 8 × 0x41 skull pieces (10028f20 / 10029040), 12 × 0x3b; hurt kind 0x32 |
| 0x45 | two 0x402a wheels (model 7, scale 0x200) | 10029140 / 100292f0 | bounce between the 0x5e / 0x5d walls, hurt 0x26; Hercules on the speed power-up smashes them into 16 × 0x4004 debris (10015170 / 100152c0) |
| 0x46 | rock fall: twenty 0x402b boulders (model 7) | 100297d0 / 10029a10 | screen rumble; boulders fall with bounce physics and hurt 0x34; 0x4002 debris (10014d90 / 10014f00) |

- **Physics.**
  - FUN_1001f690 is the three-axis mover.
  - FUN_10001600 is gravity with bounce; with model = 1 it adds the half-box model adjust. FUN_10001460 is the same without that adjust.
  - FUN_1001eaf0 is the landing test.
- **The 0x2b fix.** The previous port's 0x47 debris used byte 0x25 for the depth direction. It is +0x94: `*(p + 0x25)` with an int pointer.

### Decompile conventions

- The /tmp/eng3b_out exports flatten casts. `*(p + N)` is an int index (×4) for small fields (0xc, 0xd, 0x1c, 0x1d, 0x26, 0x25) and a byte offset for the rest (0x2a, 0x3a, 0x4a, 0x116, 0x11e, 0x126, 0x96).
- When in doubt, decompile again with DecompAt.java: the Ghidra output in /tmp/torment_dc keeps the casts.
- FUN_1001cb00 leaves +0x64 = 0 (no script) unless the caller sets one. The port's `e3_spawn` sets the first animation, so 0x3b and 0x41 clear it.

### Not done

- **Shadows.** The 0x2017 shadow (FUN_1001d600 / 100145a0 / 100145d0) needs a shadow draw mode in the renderer.
- **Brightness and semi-transparency** (+0x4a, +0x54 & 7). Trails, souls and ghosts draw opaque.
- **Type 8,** spawned by 0x45 when smashed on path 0 with the variant set.

## TORMENT2: the path 3/4 soul-river stones (depth 25088–26368)

**The port was already faithful here; no game-logic change was needed.** I checked these against freshly decompiled ENG3 code (`/tmp/torment2_out`) and against `analysis/ENG3/functions`. Each matches the port:

| What | Original | Port |
|---|---|---|
| Steering | FUN_10018d00 / FUN_1001a070: ±0x40000 = 4 lateral/tick, the same on the ground and in the air | same |
| Jump | FUN_10018de0: vy 0xba000, rise decel 0xa000; FUN_1001a180 sets fall gravity 0x18000 at the apex | same |
| Floor | FUN_1001b2e0 (col = (lat+0x300)>>7) and FUN_1001b100 (strides from 0x10039a68, the same table as ENG1 CT) | same; DAT_1004063e is 0x7fff, so there is no default floor |
| Landing / ground | FUN_1001eaf0 / FUN_1001e8e0 / FUN_1001ea60: a single-point probe | same |

**The layout.** Cells, path 4, rows 196–207 (path 3 is an identical copy):
- The walkable cols are 4–7 (lateral −256..255). Cols 5–6 are river block 0x184/0x185, which has no floor.
- The stones are blocks 0x0bb / 0x0a9 at the outer columns:
  - L stone at 25344–25599
  - R stone at 25600–25855
  - L stone at 25856–26111
- Full floor resumes at 26368.

**Why it seemed impossible.** The crossing was assumed to go L → R → L. That needs about 290 lateral within one jump of about 30 ticks (about 120 lateral), so it really can't be done. But the route is **straight running jumps along one side**:
- path → L stone (gap 256)
- L → L (the R stone between them, gap 256)
- L → full floor (gap 256)

A running jump at 10 per tick covers about 300 depth. The R stone, with its pickup, is an optional bonus.

Verified with a scripted run, damage off, `--script "U" + 14·"." + 30·J + 23·"." + 30·J + 22·"." + 30·J`, env `E3_GOD=1 E3_PATH=4 E3_DEPTH=24900 T2_LAT=-200`. It lands at 25352, 25865 and 26411, then runs on.

**The fix is in the test pilot (E3_AUTO, `e3game.c`, marked TORMENT2), active only on level 10.** It applies only to river (attr 99) stretches where nothing in the middle band of the path (−64..64) has floor beyond the gap, i.e. the side stepping-stone stretches. There it:
- picks the landing piece reachable in lateral;
- jumps when the predicted landing (speed × 26) falls in the near half of the piece;
- uses a slow (Down) jump for short pieces;
- steps across adjacent pieces instead of jumping;
- keeps full speed and height in the air.

The speed power-up (state 8) is handled too.

**New test-only switches:**
- `T2_LAT`: start lateral
- `T2_CELLS` / `T2_ROWS`: raw cell dump
- `T2_PROBE`: raw floor probe
- `T2_DBG`: planner log
- `RESCUE_LOG`

`E3_LOGPOS=N` now logs every N ticks.

**Still failing.** The from-start god-mode run still dies at the same pilot weak spots the base build has. They are unchanged by this work and outside this stretch:
- path 0 centre zig-zag at 4096–4608
- the path 1/2 raft area at 2816–3072
- path 0 at 18432

So no full-level finish without E3_RESCUE yet.
