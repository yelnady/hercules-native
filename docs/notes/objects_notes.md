# ENG1 level objects: reverse-engineering notes

All addresses are ENG1.DLL virtual addresses. A full Ghidra decompilation dump (all 1000 functions) was
written to `/tmp/objects_out/eng1_all.c` (scratch, regenerate with `tools/DecompAt.java`-style DumpAll).

## Where spawn data lives

The spawn records are in the **map**, not in the DLL:

- Map header `+0x30` has `nlayers` (u16 at +8) self-relative pointers to layer records
  (`DAT_10070d5c[]`, set up in FUN_100208f0). Layer record: `+0` u8 nstrips, `+2` u8 nsections,
  `+4` u16 ncols, `+6` u16 nrows, **`+8` u16 spawn count, `+10` s16 offset (relative to +10) to the
  spawn records**, then `+12` strip depths.
- Spawn record: 8 bytes `{u16 x, u16 y, u16 z, u16 template}` (vertex units; z = 0/128/256/384 = depth
  plane).
- Template table (per level, ENG1 level-object entry `+0x40`, `DAT_10071398`): 10 bytes
  `{s16 dx, s16 dy, s16 dz, u16 flags, u16 type}`. Position = record + d*.
  For MEDUSA (0x1006fe60), VORTEX (0x100702a8) and level 0 the template pointer is in .bss
  (built at run time; not decoded yet).
- FUN_10025d20-ish block at 0x10025xxx (decomp line "DAT_100713ae == 0") expands them into per-layer
  arrays `DAT_1006fee8[layer*2]` (0x14-byte entries `{x,y,z 16.16, u16 flags, u16 0, u16 type}`, count
  in `DAT_1006feec`). Counts types 0x2018 / 0x2019 into gamestruct +0x1a / +0x16 (collectible totals).

Spawn flags (`entry+0xc`): 0x800 = spawn at level start (FUN_10026480), 0x40 = wide activation window
(±500 instead of ±0x100 around the camera), 0x1000 = start facing left, 0x8000 = currently live,
0x4000 = gone, 0x200 / 0x80 / low bits = per-type variant (stored to obj+0x2a by some types).

Activation (FUN_100262a0 at start, FUN_100265xx every frame): entry x − (DAT_10070ca0 − DAT_10070cdc)
inside the window and just off-screen; only the current layer (`DAT_10070cfc._2_1_`).

## Type classes and pools

`type & 0x7000`:

| class  | pool            | spawn fn      | type info (level entry) | handlers (level entry) |
|--------|-----------------|---------------|-------------------------|------------------------|
| 0      | DAT_10072860, 0x134 B (player is #0) | FUN_100270c0 | +0x18 (`DAT_10071388`), 16 B `{script, anims(+0x110), movetab(+0x118), u8 bank}` | +0x1c (`DAT_10071370`), 12 B `{init, update, update2}` |
| 0x4000 | DAT_10073494, 0xec B | FUN_10027720 | +0x20 (`DAT_10071390`), 8 B `{script, u16, u8 bank@6}` | +0x24 (`DAT_1007137c`), 8 B `{init, update}` |
| 0x2000 | DAT_100734e0, 0x94 B | FUN_10027c40 | +0x28 (`DAT_100713a4`), 8 B | +0x2c (`DAT_1007139c`), 8 B |
| 0x1000 | (trigger)       | FUN_10028290 → FUN_10003870/10003520/10012910/100128a0 (bank/sound/music loads by 0x400/0x800 bits) | | |

Index = `type & 0x1ff`. Level-object entry (0x10068af0, 0x48 B, keyed by level number, 999 ends):
+0 level, +2/+4/+6 pool sizes, +8/+a/+c first free index, +e/+10/+12 counts, +0x18..+0x2c tables
above, +0x30 `DAT_100713b0`, +0x34 script callback table (`DAT_10071384`, used by script op −10/−11),
+0x38 attribute 0x8000 callbacks (`DAT_100713c0`), +0x40 templates, +0x44 attribute flag table.

Per-frame update FUN_100256e0 / FUN_100258e0: objects with flags50 bit 0x100000 go into the solid list
`DAT_10071b20` (checked by FUN_1002a170 before the map attributes), 0x40000 / 0x200 / 0x800000 /
0x400000 / 0x80 into other lists (targets, hittables, ...). Banks whose state byte is 4 are skipped.

Sprite bank table: `0x1005cee4 + 0x14*b`: u16 frame count, path pointer at −4 (append `\ANIMPSX.BIN`).

## Object fields used (0x134 layout; the smaller pools share the first 0x94 bytes)

+0/+4/+8 x,y,z 16.16; +0x22 bank; +0x2a variant/state; +0x2e type; +0x30 timer; +0x32 script index;
+0x34 frame (bank 0: model index into map group B); +0x38 layer; +0x3a scale; +0x50 flags; +0x54 draw
flags (0x10 mirror, 0x10000000 = 3D model); +0x58 spawn entry offset; +0x64 script; +0x88 hitbox;
+0x94 facing; +0x96 depth facing; +0x9c..+0xa4 previous position; +0xd8 collision attribute (solid
objects) / per-type data; +0xe0 target z; +0xf0 movement table; +0xf4 table index; +0x110 animation
list; +0x114 animation index; +0x118 default movement table; +0x11e move enable (>= 0); +0x120 child
list (FUN_1002b130); +0x124 callback id; +0x126 AI counter.

## Scripts

Two formats:
- Actors (class 0): 4-byte `{s16 op, s16 arg}`, interpreter **FUN_100283e0**. op >= 0: frame, lasts arg+1
  ticks. Opcodes: −1 restart (returns 1), −2 flip facing, −3 toggle mirror, −4 next animation in the list
  (FUN_10029270) then restart, −6/−7 remove, −8 FUN_10027450, −10 callback[obj+0x124], −11
  callback[arg], −12 switch animation (FUN_100292f0), −13 stop moving, −14 start moving, −15 toggle,
  −16 movement index, −18 bank, −0x14..−0x17 draw-flag ops, −0x18 obj+0x36, −0x19 back to spawn point,
  −0x1a/−0x1b/−0x1c move x (facing-signed)/y/z, −0x1d/−0x1e flags50 or/and-not, −0x1f spawn object,
  −0x20 obj+0x4c, −0x27/−0x28 sound, −0x29 obj+0x2a, −0x2b/−0x2c/−0x2d velocities, −0x2f child frame,
  −0x30 clear 0x800000.
- Pickups/effects (0x2000/0x4000): s16 words, interpreter **FUN_10028820**; word 0 = default frame time;
  ops take their argument in the next word (−9 time, −0x10, −0x11, −0x12 bank, −0x1f, −0x27/−0x28,
  −0x29); −1 loops to word 1.
- Animation list (+0x110): array of script pointers; a 0 entry is followed by the index to jump to.
- Movement table (+0xf0): 6-byte `{dx, dy, dz}` per index; FUN_100291d0 applies dx signed by facing,
  dz signed by +0x96 each tick while +0x11e >= 0. The player uses the same mechanism for doorways
  (table DAT_10054da0, index per level from 0x10055240).

## Collision boxes

FUN_10030e40: sprite frame header (as decoded by sprite.c) `+1` s8 box count, boxes at `+2`, 6 signed
bytes `{x0, y0, z0*2, x1, y1, z1*2}`; FUN_1002a880 mirrors x when +0x54 & 0x10. Bank-0 (model)
objects take the box from the map model record (FUN_10031ec0, DAT_10070d2c = map group B).
FUN_1002a710 / FUN_1002a4d0 = box overlap with the player box (+0x88).

## Depth planes (what blocks THEBES at column 26)

Not an object: the level is several 128-unit depth planes. Attribute flags (level table +0x44)
0x10 / 0x20 mark doorways to the plane behind / in front. FUN_1002ff50 (called from FUN_10015780 each
tick with the attribute under the player) widens `DAT_10071c5c..DAT_10071bc4` to include the
neighbouring plane and counts `DAT_10071840`. FUN_10017d00 (called from the idle/skid/turn/land/0x11
states) starts state 0x1c (FUN_10017d60) on UP/DOWN; FUN_10017eb0 walks along z with the per-level
speed until the bound, snapping to the plane when released, then FUN_10016cf0 (state 3, turn-back
script 0x10053bc8 / 0x10053bb0). In THEBES the first doorway is at x≈1120-1300 (stairs up to plane
128); the second at x≈3040-3170 in plane 128.

## Thebes types (level 5)

0x24 −, 0x25 GENERIC1 crowd (FUN_1000f430/1000f4f0, AI callback 3 = FUN_1000f770), 0x26/0x27 THUGS,
0x28 THFLASH, 0x2a THSNOW, 0x2b THEQ, 0x2c THFLOOD, 0x2d/0x3f/0x40 THCHAR, 0x30-0x32 FANGIRLS,
0x33 THOLD, 0x34 THEWGUY, 0x37 MINOTAUR, 0x38/0x47 HARPIES, 0x3e SKELETON, 0x44 THFAT, 0x46/0x48/0x49
model groups (crates; children 0x202b/0x202d), 0x4 solid model (attr 7). Pickups (handler
FUN_10012c00, update FUN_10012d80, effect FUN_10012ef0): 0x2002 max-health +0x20, 0x2012 life,
0x2013 health +0x20, 0x2014-0x2017 power-ups, 0x2018/0x2019 coins (gamestruct +0x18/+0x14, max 1000),
0x201a-0x201d letters (+0x11 bits), 0x200b story scrolls. 0x400a-0x400f checkpoints (FUN_10013990 /
FUN_100139f0; id = (type+6)&15 in gamestruct +2; sound 0x28). Sparkle on pickup = type 0x2011.

## Reference check

github.com/obaidr1/hercules-recompiled (PS1 decomp, read only) documents the map header, tiles and
models (group B = models, consistent with bank-0 objects using +0x34 as a model index) but has no
notes on the object/spawn system or frame hit boxes, so nothing above could be cross-checked there.

## Enemies (work/enemies, src/enemies.c)

Actor handler table entry (level +0x1c, 12 bytes): `{init, update, update2}`. The per-frame loop
FUN_100256e0 calls `update` when flags50 & 1 and **`update2` when flags50 & 2**; every enemy uses
update2 (`update` is mostly the no-op 0x1000c720).

Shared toolkit (ported in enemies.c / objects.c):
- FUN_1002a9e0 off-screen test (screen x ± range) → FUN_10027260 despawn (spawn record goes dormant).
- FUN_10029160 4-byte movement table `{dx, dy}` per index (+0xf0/+0xf4), dx signed by facing.
- FUN_10026de0 "who hit me": frame box 1 vs (a) projectile objects in DAT_100714c0/DAT_10071900, (b) the
  player attack box DAT_10071440 (valid flag DAT_1006fec4) → returns 0x80. Then FUN_10019b40(~DAT_10072168 & 0x5f)
  gives the damage (`player_hit_damage`), FUN_1001ab50/1001aba0 play the hit sound (0x10 / 0x27a for sword,
  else 0x24/0x25). Projectiles: FUN_10030290 (−100 hp).
- FUN_1002a880(o, box, k) frame box k in world space (mirrored by +0x54 & 0x10), FUN_1002a7a0 box overlap,
  FUN_1002a710 touch with box 0; FUN_100266a0(o, level, 0) = `player_damage`.
- FUN_1002ac10 distance record `{|dx|,|dy|,|dz|,approx dist, …, direction code}`.
- FUN_100131c0(x, y, z, id) spawns a bouncing pickup 0x4012 (id 1 = coin 0x16, 2 = coin 0x1a, 3/4 health …).
- +0xee = hit points, +0xec = hit-invulnerability countdown, +0x124 = AI callback id, +0x97 = group tag.
- Groups (FUN_10029670 / FUN_10029470): enemies with the same tag are chained via +0x12c/+0x130; the AI counts
  members with v2a == 4 ("attacking") to limit how many attack at once.

| Type (lvl) | init / update2 | AI callback | Notes |
|---|---|---|---|
| 0x26 thug (3,5) | 10001040 / 100010f0 | 7 = 100015e0 | 9 hp, box 2 hurts (level 3, 0x23 on anim 0xc) |
| 0x27 thug (3,5) | 10001310 / 100013c0 | 8 = 10001910 | 5 hp, v2a 5 throws 0x4027, v2a 9 picks open side |
| 0x3e skeleton (5) | 10035200 / 100352f0 | 4 = 10035590 | buried (anim 0x22) until Hercules is within 0x100 on the same level; 3 hp; death anim 0x15 → v2a 8 → bone pieces table 0x1005ff60 + drop |
| 0x36 training target (1) | 1003e6d0 / 1003e770 | – | floats; breaks on any blow (anim 0xb), drops coin, rises, sound 0x4044 |
| 0x37 minotaur (5) | 10024270 / 10024320 | 6 = 100244d0 | not ported (generic) |
| 0x38 harpies (5) | 10010540 / 100105a0 | 5 = 10011450 | 13-state flyer, not ported (generic, death anim 0x24) |

Ported faithfully: thugs (both), skeletons, training targets. Everything whose bank is hostile (ROCKC,
WRESTLEC, Griffon, HARPIES, MINOTAUR, RockT, IceT, LavaT, TornadoT, Harp, Spy, SHARK, TOPS) gets a
generic behaviour: plays its animation list, 3 hp, hurt by frame boxes 2/3, puff (0x2027) on death.
Known limitation: rock centaurs (Forest 0x28, FUN_100086f0) sit in their dormant "rock" frame whose box 1
lies below the floor, so the generic path can't hit them; they need their own handler.

## Bosses and level scripts (not map objects)

Level start / per-frame / per-frame-late hooks: FUN_1001fd20 / FUN_1001fd90 / FUN_1001fdf0 switch on the level:

| Level | start | per frame | late |
|---|---|---|---|
| 1 Training | 1003ca10 | 1003ca50 | – |
| 3 Forest | 1000e3e0 | – | – |
| 5 Thebes | 10033700 | 10033720 | 100337b0 |
| 6 Hydra | 1001ac10 | 1001ae00 | 1001abf0 (spawns 0x2c) |
| 7 Medusa | 10021050 | 10021210 | – |
| 9 Titans | 1003c170 | – | 1003c130 |
| 11 Vortex | 10044120 | 10044240 | – |

- Hydra: FUN_1001ac10 spawns actors 0x34/0x35 (HYDRA bank 79) and objects 0x202c/0x202a; the map has two 0x31
  records (init 1001dd00, update 10022180). ~40 functions / 2000 decompiled lines (0x1001ab00-0x1001f000).
- Medusa: FUN_10021050 places Hercules at (0x3b8, ?, 0x4c), spawns 0x2e (Medusa, kept in DAT_1006fe24,
  anim list 0x10059398), 0x2f, 0x202d; difficulty tables 0x10058b08/ba0/c40. ~35 functions / 1300 lines.
- Vortex: FUN_10044120 places Hercules at (0x135, 0x220, 0x80), spawns actor 0x28 and 30 × pickup-pool 0x2b
  (the vortex debris); FUN_10044240 drives the fight. 11 functions / 720 lines.
- **The Medusa/Vortex "runtime template tables" (0x1006fe60 / 0x100702a8) are not built at run time**: they are
  zero-filled .bss and nothing writes them (the only reference in the DLL is the level table). Medusa's 6 map
  spawn records therefore resolve to type 0 (no-op) in the original too; Vortex has none. Their content comes
  entirely from the start functions above.
- Titans (Pegasus flight): 73 functions / 2300 lines in 0x10036d00-0x1003c300; titans 0x28-0x2b and gods 0x2c-0x30,
  0x45 Zeus, 0x61 harpies, 0x5d spy. Only the generic behaviour is applied.

## Solid objects, models, groups (work/solids, src/solids.c)

**3D map models** (bank-0 objects with flags54 & 0x10000000; obj+0x34 = model index):
- Table: DAT_10070d2c = map block table + 4*(hdr u16 +0xc) - 4 (FUN_10020670); entry i (1..hdr u16 +0xe) is a
  self-relative pointer to the model record.
- Record: +0 s16 face-list index (faces at rel(hdr+0x24) + 4*idx), +2 u8 box count, +3 u8 cull radius,
  +4 u32 offset {x s8, y s16, z s8} (added after rotation), +8 boxes (8 bytes: z0 s8, x0 s16, y0 s8, depth u8,
  width u16, height u8 - FUN_10031ec0), then vertex groups `{s16 n, n x u32}` (stride 4n+4; vertex = z s8 |
  x s16 << 8 | y s8 << 24, FUN_100318d0).
- Face groups (FUN_10031540): `{u16 nfaces, u8 more, u8 carry}` + 8-byte faces (4 x 6-bit vertex index, top
  byte flags: 0x40 = either half may face the camera, 0x08|0x01 = never culled, 0x08|0x04 = skipped; second
  word = offset into the texture table rel(hdr+0x28), or flat colour when its top byte is non-zero), then
  `carry` vertex bytes, aligned to 4 -> next group. GPU quad order is v0 v1 v3 v2; OT depth = (sz v0 + sz v2)/8.
- Rotation (FUN_10032810): +0x70/+0x72/+0x74, 0x400 = full turn, scale +0x3a (0x100 = 1, +0x3c/+0x3e with
  flags54 & 0x80). Model x runs along the path (verified on the Training statue / Thebes column).
- Per-type vertex callbacks PTR_FUN_1005cec0[obj+0x23] (0 = rigid); only the rigid one is ported.

**Lists**: flags50 0x100000 -> solid list DAT_10071b20 (FUN_1002a170 returns obj+0xd8 as the attribute id when a
point is inside model box 0); 0x40000 (and 0x240000) -> platform list DAT_100714c0 (FUN_100296e0: distance to the
top of box 1 = floor, checked with the map in FUN_10029c20 / FUN_10029e20); 0x40 -> swing poles DAT_10071a20
(FUN_10017810 tests the hand box 3 against the object position); 0x200 hittable; 0x800000 / 0x400000 others.
The player's carry target DAT_1007210c is the solid object found by the idle free-space probes FUN_10019900 /
FUN_10019820 (x +-0x24, y-8); FUN_10018b10 lifts actor types 2, 4 and 5.

**Groups** (FUN_1002b130): type info +8 (obj+0x118) = list of 12-byte entries `{s16 dx, dy, dz, u16 type
(of THIS entry is at +6), s16 frame, s16 +0x2a}`, the next type at +0x12; children go to obj+0x120 and usually
point back through +0x5c. +0x2a is stored before the child's init runs.

**Physics** FUN_1002bd20 / FUN_1002bc50: v[+0xa8..+0xd4] = {vx, vxmax, xacc, xdec, vy, vymax, yacc, ydec, vz,
vzmax, zacc, zdec}; +0x95 = moving up, +0x96 = z direction; on landing vy halves and bounces.

| Type (level) | init / update | Port |
|---|---|---|
| 0x49 (Thebes) / 0x4e (Training) stone stacks | 10036150 / 100361a0, 10042710 / 10042770 | breakable column: 50 / 35 hits (0x23); full power punch topples it (FUN_10036990), others wobble (FUN_100367b0); rubble 0x4028 models 9/10 |
| 0x402d / 0x4040 stack stones | 100363e0 / 100365f0, 100429c0 / 10042bd0 | solid (attr 0x1a / 0x1b), fall and tumble when toppled |
| 0x04 (Thebes) column drum | 10014790 / 10014850 | solid + platform (attr 7); lift (pose table 0x10052e70, 10 bytes {x, y, rx, ry, rz}, y - 48), throw (pose 0x2f forward, 0x30 up) |
| 0x4c / 0x4e (Forest) rock piles | 10009fb0 / 1000a010, 1000a420 / 1000a490 | solid (attr 3); 50 blows or one full power punch; shake table DAT_1004fc18; sounds 0xce / 0xcf |
| 0x40 / 0x46 / 0x4f stone slabs | 1000f910 / 1000fa00 | platform; ground pound (hit 0x20) on it breaks it into its +0x118 pieces (0x2027, 0x4023 1000fd00/1000fe60) |
| 0x41 / 0x47 (Training) rope bridge | 10041820 / 100418a0 + 0x4035/0x403e 10041a50 / 10041ad0 / 10041b70 | 30 ticks after Hercules steps on it the planks give way one after another (6-tick fuse each) |
| 0x37 (Training) archery targets | 1003f140 / 1003f1c0 | model 0x41 lying flat (rx -0x100); 3 hp; raising / spinning / firing 0x4028 driven by the station is not ported |
| 0x3f (Training) gate, 0x44/0x45 weight, 0x46 wheel, 0x49 fence, stations 0x32 0x33 0x35 0x39 0x3e | 1003ebc0, 1003edf0, 10042020, 1003f620, 1003ce10 ... | models + children; gate slides back (z += 4 to +0xe0) / weight rises when +0x2a = 3. **Stand-in:** Phil's lessons (station updates) are not ported, so they open when Hercules reaches them |
| 0x202b (Thebes) / 0x2030 (Training) swing poles | 100360a0 / 100422d0 | flags50 0x41 -> P->poles |
| 0x4031 / 0x4034 / 0x4039 / 0x202f / 0x4032 0x4033 0x403a 0x403b | 10040a50, 1003f040, 1003f070, 1003eaa0 ... | decoration / solid post / markers |

The full-charge test of the original (DAT_1007082c + 2 >= 200) is approximated by the peak of the meter during the
current punch, because the port's state 0x2a drains its copy before objects update.

Not done: per-type vertex callbacks (1..7), triggers (0x1000 records: bank / sound / music loads - the port loads
everything up front, so they have no visible effect), story scrolls 0x200b, the training lessons and their target
actors (0x2f/0x30/0x2a/0x3a/0x4d/0x43), Training 0x2029/0x202b effects, real AI for minotaur (10024270, a stomp /
thrown-object mini boss), harpies (10010540), Forest wrestler / rock centaurs (10009190 / 10008660), griffons
(1000d760 / 1000ddd0), Training shark (100410b0) and Pegasus (10041f50), Phil (1002cc50). Rock centaurs in their
dormant rock frame now use a fallback body box so they can be hit.

### Story scrolls and tally counters (work/solids)
- 0x200b (FUN_10012c00 / FUN_10012d80 / FUN_10012ef0): id = spawn flags & 0x3f (stored in +0x2a), script =
  PTR_DAT_100537e0[id]; collecting sets gamestruct +0x1c |= 1 << id, DAT_10072104 = id, DAT_10072174 = 1 (show
  the story), sound 0xc03c. The script's bank DAT_10053800[id] is loaded on demand in the original.
- Objects fields: `scroll_bits` (+0x1c), `scroll_last` / `scroll_show` (DAT_10072104 / DAT_10072174),
  `coins18` / `coins18_total` (+0x18 / +0x1a, type 0x2018), `coins14` / `coins14_total` (+0x14 / +0x16, 0x2019),
  `letters` (+0x11, password vases 0x201a-0x201d, bit per vase), `lives`, `health`, `health_max`.
- Weapon effects 0x4001 / 0x4005 / 0x4006 have no ported init handler in the 0x4000 tables, so obj_spawn refused
  them and they never existed; enemies.c now creates them directly (weapon_obj).

## Enemies, part 2 (work/enemies2, src/enemies2.c)

Dispatch: `enemy_init` / `enemy_tick` / `enemy_callback` (enemies.c) call `enemies2_init/tick/callback` first;
`spawn_type` (objects.c) also offers non-actor (0x2000 / 0x4000-pool) types to `enemies2_init`; `objects_tick`
calls `enemies2_frame` (level per-frame hooks). Shared toolkit wrappers `en_*` are appended to enemies.c.

| Type (level) | Handlers | Behaviour ported |
|---|---|---|
| 0x37 minotaur (5) | 10024270 / 10024320, AI cb 6 = 100244d0 | Mini-boss. Not hurt by blows. Boss bar starts when Hercules enters the arena (Thebes hook 10033720: x 0x1510..0x1780, y > 0x440). Attack pattern table 0x1005a060 (FUN_10024680; 0 = jump). Ground pounds (hitflags 0x20) during the fight call FUN_10024c60: every 2-3 (easy) / 3-5 (medium) stomps a boulder 0x4029 (10024f50) falls above Hercules, plus 10-16 rubble 0x4028. Boulder landing on it: -2 hp, bar damage (0x100/(diff*2+6))*2+3; thrown objects (flags50 0x800000, DAT_10071be0): -1 hp. Bar empty -> anim 0x1d, v2a 8 (walks off into the back plane) and attribute flags 0xb/0xc become 0x10/0x20 (doorways open). |
| 0x41 minotaur rock (5) | 100246c0 / 10024780, shatter 10024980 | Held at the frame attach point (FUN_10031000: the 2 bytes after the boxes +4) while v2a 2/4/5, thrown at v2a 6 (vx = cos(atan(dy/dx)) << 4 from FUN_1002bf30/1002bef0), shatters on landing; a blow on it breaks it and drops a pickup (ids 2, 3, 4, then 1). |
| 0x28 rock centaur (3) | 10008660 / 100086f0 | Patrols (v2a 10/9, footsteps 0xe0dd/0xe0de above water), turns (0xb), lifts (0xd) and throws (0xf) a rock 0x4a at Hercules when he is 1-3 depth rows in front and within 0xf0. Never hurt by blows: only its own rock knocked back onto it (v2a 0x23, then sinks 0x25 below the water line). |
| 0x4a centaur rock (3) | 100099e0 / 10009a20, fragments 1000cfb0 | Flies toward the camera (z -= 6/tick), bounces (0xd0d0), splashes (0x49, sound 0xcb) below the water line. A sword blow (hitflags 0x08) breaks it (8 fragments 0x4030-0x4037, table 0x100510e0); crouch / power / air-sword blows (0x13) knock it back (vz -0x58000, vx/vy reversed); falling onto a rock centaur (FUN_100083e0, dz +-0x20, dx +-0x50) beats it. Touching Hercules: damage 3 (kind 0x66), shake. |
| 0x29 wrestling centaur (3) | 10009190 / 100092c0, hurt 100091f0 | Decide (v2a 1): walk home if >0x140 from the spawn point or Hercules has no health; turn (9); charge (10, 3/tick) or attack (0x15/0x16 anim 6 / 0x17 anim 0x10) within 0x69; box 2 hurts (4, kind 0x67/0x66), body contact pushes back (0x22). 7 hp; hurt (0x24, sound 0x60df), beaten (0x23, sound 0x60e0, 3 pickups). |
| 0x46 griffon / 0x47 small griffon (3) | 1000d760 / 1000d7d0 (decide 1000d560, hurt 1000d490); 1000ddd0 / 1000de50 (decide 1000dd10, hurt 1000dcb0) | Big: run (10, 5/tick), turn (9), close attack (0x3a anim 5), crouch (0x3f, 0x68e1) and leap (0x3b, vy -0x58000, g 0x8800, 0x68e2/3), wander (0x40 / 0xb); 6 hp, hurt 0x24 (0xe0e6), beaten 0x23 (0xe0e7, 3 pickups). Small: hops at Hercules (0x3b) within 0xb4 on the same row, 1 hp. Box 2 hurts (4), body 0x22. |
| 0x3c / 0x3d sharks (1) | 100410b0 / 100410e0, 10041460 / 10041490 | A: leaps straight up out of the water every 30 ticks (FUN_1002b4c0 velocity, vy 15, decel 0x9000, toward the camera). B: leaps across from the side Hercules faces away from (x +-0xbc), 5 hp, can be knocked out of the air (v2a 0x14 / 0x15). Splashes 0x202e / 0x202d (sounds 0x405b / 0x405c) at the water line (Training DAT_100718e0 = 0x360). Touch: damage 0x13, lesson hit counter DAT_10070ef0 (`Objects.t_hits`) += 1 / 3. |
| 0x42 Pegasus (1) | 10041f50 / 10041f80 | Ambient: drops to the floor, plays its animation. |
| 0x31 water (3) | 1000d2c0 / 1000d350 | Placement table 0x100511b0 (0x18 bytes keyed by spawn x: dx, dy, dz, rotY, scale), loop sound 0x8ca, vertex callback 5 (ripple; drawn rigid). 0x4b (1000d460) only rumbles once per frame counter change; not ported. |
| 0x2029 / 0x202b Phil's boards (1) | 1002d9b0 / 1002d9e0, 1002da10 / 1002da40 | Fixed at y 0x320, z 0x100, frames 0x66 / 0x83 (DAT_10070ef4 / DAT_10070f00 defaults; the lessons that change them are not ported). |

Other fixes:
- **Fireball stream (0x4001, FUN_1001a440 / FUN_1001a5a0).** The shot angle (table 0x10054c98 +4, obj +0x74) is in sine-table
  units (0x400 per turn, table 0x1005a848): dx = -sin a, dy = cos a (0x300 = ahead, 0x200 = up). enemies.c treated it as
  0x1000 per turn, so the stream went up at ~67 degrees and missed. Segment hit box is x +-8, y -8..+0x18, z +-0x10.
  Verified: from 94 units, holding the weapon button kills a Thebes thug (hp 9 -> -91) with no sword swing.
- **Sprite boxes are scaled by obj +0x3a** (FUN_10030e40); obj_box now applies `scale` (rock centaurs and their rocks are 0x200).
- The renderer still draws sprites at scale 1 (render_sprite has no scale), so scaled actors (rock centaur 0x200) look small.

Not ported (remaining):
- Thebes harpies 0x38 / 0x47 (10010540 / 100105a0, AI cb 5 = 10011450, 13 states 100107c0..100113c0): they grab townsfolk
  (group tag 0x55, victim link +0xe4/+0xe8) and fly off; they also feed the boss bar (DAT_10072184 = 0x100/(diff*6+7)+1 per harpy).
  Still the generic placeholder.
- Phil 0x31 (1002cc50 / 1002cd30, movement 1002d440, station change 1002d3f0, speech FUN_1002c4c0 = voice clip id from tables
  0x1005c3b8..0x1005c400) and the five lessons (station updates 1003cf90 / 1003cb90 / 1003d930 / 1003d440 / 1003dcd0, station table
  0x10065678 12-byte entries: +4 target state, +6 progress, +0xa gate state) with their target actors 0x2f / 0x30 / 0x2a / 0x3a /
  0x4d / 0x43 and the weight (1003eec0: drops when Hercules passes x+0xc0, not hoisted) and gate (1003ecb0). Gates/weights still use
  the solids stand-in. Archery target raise / spin / fire 0x4028 is driven from the stations, so also not ported.
- Per-type vertex callbacks 1-7 (table 0x1005cec0: 4 = flame stream, 5 = water ripple) need sprite-mesh drawing in render.c.

## Boss fights (work/bosses: src/bosses.c, bosses_medusa.inc, bosses_hydra.inc, bosses_models.c)

Full decompiles of the boss code (Ghidra BFS from the level handler tables): `/tmp/bosses_w/dec*/`.
Hooks: start = FUN_1001fd20 → per level (6 1001ac10, 7 10021050, 11 10044120); per frame FUN_1001fd90.
All three set DAT_10072188 = 5 at start (boss bar fades in) and write DAT_10072184 (bar damage) per hit;
they finish through FUN_10019240 (`player_win`, DAT_10072170).

**Vortex / Hades** (bank 80 `BOSS\HADES`). Hades = actor 0x28 (init 100445a0, update 100447c0). His own
int-pair script format `{frame, ticks}`, ops 0 end (state := next), -1 sound, -2 overlay frame (hair flame,
child 0x91+), -3 spawn the homing fireball (actor 0x2a, 10045e20/10045ef0), -4 palette flash (table
0x100693e8, 32 colours × n). States: 0 choose (idle 0x10069be8/ca0, laugh e88/a010, fire a198/a408 when
Hercules within 0x180, turn d58/df0 → state 2 flips), 1 run script, 2 flip. Flies a Catmull-Rom path
(FUN_10044740 / FUN_10045010, 20 points at 0x100696e8, 16-byte {x,y,z,steps}, forward differences),
x clamped by height bands; paused while attacking. 200 hp; hit → hurt script 0x10069988/ab8, 8 ticks
invulnerable, drop from the difficulty list (0x10069180 / 220 / 2b8, walker FUN_10044f80). Meteors
(actor 0x29, 10045a50/a80) every 15..105 ticks near Hercules' x (6 ranges 0x10069358/70), land into flames
0x202a (10045cc0/d50). Power-ups thrown in every 240..480 ticks (FUN_100131c0 ids 0xb..0xe). 30 drifting
souls 0x202b (10046230/320; state table 0x10069388). Death: FUN_10045780 builds a swoop path over
Hercules, sparks 0x202d + shrinking auras 0x2029, 300 ticks, then `player_win`. Damage to Hercules only on
1 tick in 8 (FUN_10045bf0). Taunts are XA speech (FUN_1002c4c0 → EXE callback): not available.

**Medusa** (bank 77 `BOSS\MEDUSA`, skeletons bank 78). Medusa 0x2e sits at the ring centre (z 0x280,
scale 0x200) and follows Hercules' x. Anim list 0x10059398 with callbacks (table 0x10059fa0): 1 maybe
gaze (FUN_10021810), 2 next attack from the difficulty pattern (0x10058ce0/d10/d38 → anim 2 roar, 6
snakes, 5 gaze), 3 roar + rock rain (FUN_10021a40/10021aa0, rocks = models 1/2/3/0xc with shadow sprite
0x202f), 4 gaze start (FUN_10021f60, disassembled by hand: Ghidra can't make a function there), 5 after the
gaze (FUN_100220c0: lose a life if hurt; 0 lives → dying Medusa 0x31), 6 two snakes (0x33, FUN_10023460:
arc in, then slither along at one of three heights). Gaze (FUN_10022280/10022440): two beam heads (rig
0x2f, 6 × model 0x2032) advance 0x20/tick toward Hercules; on a column top (0x30 model 0x10, state 0)
they climb it and break it into 4 pieces → its mirror shield (0x2d, model 0xf) falls; on a fallen shield
(state 6) the gaze is reflected (0x1e ticks) → Medusa hurt; on a skeleton → petrified; on Hercules (on the
floor, y > 0x2cf) → damage 3. Column state table DAT_1006fd00 (7 ints / column). 8 lives (bar 0x20 each).
Skeleton warriors 0x32 (10023a50/10023b30, int-pair scripts 0x10058d88..0x10059140) up to 3 at a time.

**Hydra** (bank 79 + map models). Body 0x2b (model 0x22, scale 0x200), floor 0x202a (model 3, ×4 at the
ring centre), necks 0x29 = groups of 10 × 0x4028 segments (model 4), heads 0x2d (morphing models 11..17).
Neck shape FUN_1001c720 from the per-head table 0x100574d8 (0x54 bytes), lengths 0x100578b8, growth
thresholds 0x10057ab8/ae8, curve sets PTR 0x10057498 (rows of 11 angle steps) blended by the 6-byte curve
scripts 0x10057450 (FUN_1001dd50). Neck states 0 grow, 1 cut, 2 regrow (FUN_1001c3f0: 7 → 3,1,5 → +2,+4,+0,+6),
3 idle (picks a bite when Hercules is in the head's x range +0x40/+0x44), 4 bite (FUN_1001d360). A sword
blow (hit flags 0x18 only) while the bite is stretched out (frames 0x26..0x38) cuts the head
(FUN_1001b370 → FUN_1001d6c0). 8 cuts (DAT_1006efb8) → five boulders 0x202e land on her (FUN_1001ae00
states 1..5) → win. Falling rocks 0x33 (FUN_1001e500/600) near Hercules all fight.

Port notes / simplifications:
- 3D parts use a weak copy of the SOLIDS model renderer (bosses_models.c), changed to place models rigidly
  (FUN_10032130: origin at the path position, turned to the path) instead of bending vertices along the
  path; big faces split 4×4. This is what makes the ring-centre floors draw (Medusa model 0xe, Hydra
  model 3). Mirrored x needs the corners swapped to keep the winding for back-face culling.
- Not ported: vertex sharing between model parts (transforms 2/6: Medusa's gaze rays are drawn as a line
  of additive sparkles, Hydra neck segments are separate rigid models), model morphing (transform 3: the
  head/body show the nearer key model), Hades' palette flash, camera shake/offset (DAT_10070cbc,
  DAT_10071848), XA speech.
- Hydra hit tests are world-space with no depth test and the player's boxes extended 64 up, emulating the
  original's screen-space boxes (DAT_10070840).

## Enemies, part 3 (work/training: src/training.c, src/enemies3.c)

Hooks: `enemies3_init/tick` + `enemies4_init/tick/callback` are tried first in enemies.c (`enemy_init`, `enemy_tick`,
`enemy_callback`) and in objects.c `spawn_type` for 0x2000/0x4000-pool types; `enemies3_solid_tick` runs first in
`solids_tick` (Training stations, gate, weight, cannon); `enemies3_reset` in `objects_init`; `enemies3_frame` /
`enemies4_frame` after `enemies2_frame`. Script op -0x18 now stores +0x36 (`Object.s36`). All edits are marked ENEMIES3.

**Training lessons (training.c).** The station table 0x10065678 (12-byte entries: x end, +2 beaten-target bits,
+4 meter state, +6 meter y, +8 stage y, +0xa gate/weight state) is written at run time by the original, so the port
keeps a mutable copy (`training_tbl`, also read by solids.c's gate / weight / station inits).
- Station updates 1003cf90 (0x32 stomp stage: ground-pounding on it breaks 2-4 bags, FUN_1002c460(3)+2, the stage
  sinks with progress), 1003cb90 (0x33), 1003d930 (0x35, walking 0x2a targets), 1003d440 (0x39 carousel: 0x3a riders
  placed on the hub 0x43's angle, table 0x1005a848, x*0xf0 / z*0x3c, whoosh 0x5867), 1003dcd0 (0x3e pendulum stage,
  sounds 0x4863/0x4864). Shared: FUN_1003eb30 counts beaten targets (+0x126) of the station's type (+0x124 total);
  the strength meter 0x2f sinks d[1]*beaten/total while Phil cranks (DAT_10070efc 1: +4/tick, 2: +2), all the way
  (+0xe0) once all are beaten, then its state |= 4 (prize ready). Touching the ready meter: anim 0xd, sound 0x406f,
  pickup id table 0x10066e88[station], dust, DAT_10070f0a = 1.
- Targets: 0x30/0x4d (1003e220 / 1003e160 / 1003e280, 3 hp, beaten -> anim 0xb, v2a 9), 0x2a (1003e370 / 1003e3d0,
  walks in depth between -0x18 and 0x130, 4 hp, touch = damage 1, hit counter +3), 0x3a (1003e580 / 1003e5d0, 5 hp).
- Phil 0x31 (1002cc50 / 1002cd30 / 1002d440 / 1002d3f0): route list = type info +8 (0x10065750), 6-byte {op, a, b}:
  1 teleport, 2 walk to x (anims 6 start / 8 stop / 7, v2a 1 / 0x15 / 0x16 from his scripts), 3 lesson station,
  4 wait until the station's targets are all beaten, 5 gate (gesture anim 0x22, at v2a 5 the gate's v2a = 3 + sound
  0x4049, advance when it reaches 9), 6 weight (anim 0x31, v2a 5 -> weight v2a 1 + 0x404a), 7 anim 0x2e, 8 set z,
  9 wait for Hercules at x (hidden meanwhile, v2a 10), 0xb follow Hercules inside [a, b], 0xc skip a entries if the
  station's meter state > 3, 0xd anim 0, 0x14 station label (FUN_1002d3f0 jumps there when Hercules enters another
  station). Speech FUN_1002c4c0 -> sfx_speech(n) (PC_<1000+n>.ESF): tables 0x1005c3b8 (beaten-count limits),
  0x1005c3d0 (well done per station), 0x1005c3f0 (got hit), 0x1005c3f8 / 0x1005c400 (idle nags), clip 0x1a (stomp).
  Boards 0x2029 / 0x202b show DAT_10070ef4 / DAT_10070f00 (Phil's +0x36 while he cranks / on v2a 2, 5).
- Gate 0x3f (1003ecb0): v2a 3 slides z += 4 to +0xe0, then 9; kids follow the +0x118 list. Weight 0x44/0x45
  (1003eec0): v2a 1 hoists it to +0xdc, when Hercules passes x + 0xc0 it drops (2, sound 0x404b, FUN_1002bc50 physics),
  lands (4, 0x404c), settles (0). The solids.c proximity stand-ins are bypassed in Training.
- Cannon 0x37 (1003f1c0): raises when on screen (0x4056), spins (+0x72 += +0xdc), fires 0x4028 (1003fa50 / 1003fb00
  cannon ball, velocity sin/cos(angle)<<5 in the x/z plane, explodes 0x4025 on walls or Hercules) when its spin angle
  is within 0xc of Hercules' direction; +0xee is both the reload timer and its hit points, as in the original.
  The angle convention of FUN_1002bfb0 is approximated with atan2.
- Spinning tops 0x2b-0x2d (TOPS bank, 1003fe40 / 1003ff30): hop (1), thrown out (2, lands -> 4, sound 0x4854),
  spin around (4/5): bounce off each other and walls, touch = damage 3, 3 hp, 4 drops from table 0x10066738,
  pieces 0x403d (1003fcf0 / 1003fe00), lid 0x4038 (10040ba0 / 10040c10). The chase step (FUN_1002b5e0 / 1002b9a0) is
  approximated by a fixed-speed step toward Hercules.

**Thebes harpies (enemies3.c).** 0x38 (10010540 / 100105a0, AI callback 5 = 10011450): perch walk (1), take off
(2 -> 10010900 spring to x -+0x180, y -0x30, z -0x100), glide (3/6), fly to a hunting spot (7, 10010cc0: x home
+-0x40, y 0x350, z from table 0x10052790 slot DAT_100721ac), hover (8, 10010e70), dive at Hercules (0xb, 100111a0:
y += 16, x += +0x9a, touch = damage 3), carry (10, 100112d0), fly home (4, 10010a40), settle (5, 10010bd0), dying
(0xe, anim 0x24; DAT_10072184 += 0x100/(diff*6+7)+1 -> `hud_boss_damage`). Mover FUN_1002b9a0 (axis springs toward
+0xd8/+0xdc/+0xe0) is ported as `spring`. 0x47 (100115f0 / 10011690) guards the way; when every 0x38 harpy is
beaten (stand-in for DAT_1006ffdc, whose setter is not in the decompiled set) it leaves and attribute 0x19's flags
are cleared. The townsfolk they snatch (group 0x55: types 0x39-0x3b THHAFOOD, inits 10011c10/c70/cd0) are never in
the spawn data and their spawner was not found, so harpies only hunt Hercules.

**Forest / Thebes actors (enemies3.c).** Fish 0x2b (10008dd0 / 10008e10, three leap kinds), fish 0x4d (1000e170 /
1000e200, climb the bank), calling birds 0x51/0x52 (1000e520, 1000e470 / 1000e560, scale 2, sounds 0x58c5-0x58c9),
Meg 0x55 (1000e3b0 / 1000c910), pole 0x2005 (10013c90, flags50 0x41), Thebes townsfolk walkers 0x2c / 0x33 / 0x44
(100348a0/10034910, 10034c80/10034d00, 100341a0/10034210), fangirls 0x30-0x32 (100342f0 / 10034350), the
earthquake man 0x2b (100343a0 / 100343e0, dust 0x402a; camera shake list DAT_10071680 not ported), the singer
0x2d (10033ee0 / 10033f30, synced to the Thebes hook timer DAT_10070802, sound 0x5148), marker 0x24 (10001010).
Their palette variant (+0x1c from tables 0x100602a8..) is not applied (the renderer has no CLUT swap).

**Pickup drops (enemies.c drop_pickup = FUN_100131c0).** id & 0xf indexes table 0x10052b90 (8 bytes: type, script):
4 -> 3 on level 9 and after DAT_10072190 drops (easy: 2, else 1); 9 -> the next password vase 0x201a..0x201d
(DAT_100721a8 sequence), already found -> coin 2; 10 -> 3 when health max is 0xff; ids 1/2 add to the coin totals.
**Sprites** are now drawn at the object's +0x3a scale (`render_sprite_scaled`).

Not ported: Forest birds 0x2e and Training birds 0x38 (STYBIRD flocks, 1000b3f0 / 10043440, ~330 lines each),
Forest 0x09 / 0x53 (Pegasus / Nessus sequence, belongs to the ENG3/Nessus work), 0x4b (FUN_1000d460 is a VRAM
texture scroll: no renderer support), Thebes 0x28 / 0x2a / 0x34 / 0x3c / 0x3d / 0x3f / 0x40 / 0x48 / 0x202d, Training
0x4c (10010380), vertex callbacks 1-7 (table 0x1005cec0), the meter rope child 0x34 and texture scrolls
(FUN_10002d80) of the lesson machines.

## Nessus fight (work/nessus, src/nessus.c)

Game level 4 has no map of its own: Nessus is fought in the Forest arena between the stone pillar (x 18452-18515, top about y 672) and the cliff (x 19136+). This fight is also the Forest's real ending.

**Objects** (ENG1 handlers; obj+0x2a is the state):
- **Type 0x09 follower.** Init FUN_10014de0, update FUN_10014e10.
  - Once Hercules is right of and below it, it sets DAT_1006ef14 = 1 (the fight is armed) and follows him.
  - When Nessus' fist hits a wall, DAT_10072178 is set. The follower then damages Hercules (player_damage 0x22 from Nessus) unless he is in state 2/4/7/9/0x10/0x13/0x16/0x17/0x19/0x1b, which are airborne, hanging or dying.
  - It also locks the camera's right edge (DAT_10070ce8 = DAT_10070ca0). This is not ported.
- **Type 0x53 trigger** at x=18520. Init FUN_1000c780, update FUN_1000c7e0.
  - Init: if Hercules is left of it, it waits (state 0x37). If the fight is already armed, it spawns Nessus at once. Otherwise it despawns.
  - State 0x37: starts the resource loader. DAT_1006ef38 = 1 → FUN_1000e670 swaps in the NESSUS SFX bank (dir table entry 17), then FUN_1000ebd0 loads MUSIC.MUS; the state reaches 5 when loaded.
  - State 0x51: once the loader reaches 5 and the fight is armed, it spawns Nessus (FUN_1000c730: type 0x2f at Hercules' x + 0x140) and sets gamestruct+2 = 5.
  - State 0x52: after 0x3c ticks it plays sound 0x100 and disappears.
- **The "Meg flags"** are really these two: DAT_1006ef14 (armed by the follower) and DAT_1006ef38 (the loader state).
- **Streaming ids.** FUN_10003520 / FUN_10003870 load and unload streamed sprite banks. Ids 0x44-0x4b are bank id - 1 for NESSUS Punch / Kick / Look / Ride / Reject / Finish / Defeat and ALIEN2\Meg; the table is at 0x1005cef0, 20 bytes per entry. Nessus only attacks once the bank's state is 3 (loaded). The port keeps every bank resident, so these checks always pass.
- **Type 0x2f Nessus.**
  - Init FUN_1000bb10: state 0x37, facing left, flags50 0x202, boss bar DAT_10072188 = 5 (fade in).
  - Update FUN_1000bc50, by state:

    | State | Anim | Behaviour |
    |---|---|---|
    | 1 | | Decide. |
    | 9 | 1 | Turn. |
    | 10 | 2 | Walk at 6 units/tick toward Hercules. A box-1 edge on attribute flag 2 (per-level table) stops him; within 0x50 units he attacks. |
    | 0x16 punch | 10 | Attack; the choice between punch and kick alternates on DAT_100721b4. |
    | 0x46 kick | 5 | Attack, as above. |
    | 0x44 | 0xb | Back off, after rnd(3)+1+difficulty wall punches, or at once if Hercules is behind him. |
    | 0x45 | 9 | Offers his back: DAT_10072120 = 1, box 3 = the back. |
    | 0x41 | 6 | Ride: x += 2 per tick, boss-bar damage 1 every other tick. |
    | 0x42 | 7 | Bucks Hercules off at script index 7 (FUN_1000bb90). |
    | 0x47 | 1 | If the bar is empty: state 0x43. |
    | 0x43 | 8 | Dazed: sound 0x610f, then 0x110 every 30-60 ticks. Only a power punch (hitflags & 2) with the meter at 0x65 or more, landing on box 0, defeats him. |
    | 0x23 | 4 | Defeat: spawns type 0x54, stops sound 0x10f. At the end of the animation FUN_10019240 (player_win) runs. |
    | 0x25 | 0xc | Idle afterwards. |

  - **Damage.** When the attack box (box 2) touches Hercules' body, it does player_damage 5. Otherwise, every 4th tick, a solid wall 0x40 ahead in the fixed table DAT_1004f1e0 (flag 2) means the fist hit it: dust effect 0x2027, screen shake, the follower's quake damage, sound 0x24.
  - **Player blows.** These only make sparks and sound (FUN_10026e90 with damage 0).
- **The wall-punch check** uses the fixed table DAT_1004f1e0, not the per-level DAT_100713bc. They agree for Forest attribute 3. The only place he can reach a wall with his fist is the left pillar: Hercules lures him there, standing on top of it.
- **Player state 0xd** (FUN_10019260, riding): x,y = Nessus, frame 0 (Nessus' Ride bank draws Hercules).
  - It is entered from the fall state (FUN_10017400) when in the Forest, DAT_10072120 == 1, and Hercules' (x,y) is inside Nessus' box 3.
  - It ends when FUN_1000bb90 places him 0x44 behind or in front, gives him vx ±8 and makes him fall (FUN_10017340).
- **Box indices.** FUN_1002a880(o, b, k) k = 0/1 are the body box and k = 2/3 are the frame boxes 2/3, the same as `wbox` in enemies2.c.

## Enemies, part 4 (work/leftovers: src/leftovers.c + small hooks marked LEFTOVERS)

Hooks: `leftovers_init/tick/callback` are tried first in enemies.c (`enemy_init`, `enemy_tick`, `enemy_callback`) and
in objects.c `spawn_type` for the 0x2000/0x4000 pools; `leftovers_frame` after `enemies4_frame`; `leftovers_reset`
in `objects_init`.

**Thebes actors (leftovers.c).**
- 0x28 THFLASH (10033fc0 / 10034000): +0x97 tag is the state; within dz < 0x10 and dx < 0x30 it opens (anim 4) and,
  the first time, showers 8 pickups from row (DAT_1006ffd8 & 7) of table 0x1005e9c0 (FUN_100131c0 at y - 0x20).
  Its script runs with +0x2a / +0x97 swapped, as the original does.
- 0x2a THSNOW (10033910 / 10033990): sneezes at Hercules within dx < 0x80 / dz < 8 (puff 0x4a, overlay 0xa001 frame
  0x25, sound 0x4012, anim 7), turns toward him while walking, wall/edge checks in state 3, sounds 0x5140 / 0x5941-2.
  Partner 0x45 (10033cc0 / 10033cf0) rides along and is shown only while the leader is in state 3.
- 0x34 THEWGUY (10034de0 / 10034e20): walks between depth planes on attribute 0x10 / 0x20, bumps Hercules
  (player_damage 1, anim 0x16), turns at attribute 4.
- 0x3f / 0x40 THCHAR walkers (10035c60 / 10035cf0 / 10035d70): walk along z, fade (brightness) close to the camera,
  wrap round, stop for the singers (DAT_10070802 = enemies3's `thebes_sync`, exported as `leftovers_sync`), touch =
  damage 2; palette from 0x100602b4; shadow child 0x202e (10035fb0 / 10035fe0) hidden while the walker stands.
- 0x48 (Thebes) / 0x4c (Training) spawner (10010380 / 100103c0): keeps one carryable rock (type +0x108 = script word 0;
  Thebes 5, Training 2) alive while the spot is near the screen edge, frame from script words 2..9, plus type +0x10c
  (Thebes 0x202c rubble shower 100253b0 / 100253d0: 0x3c ticks of 0x4028 rubble). The Thebes one goes away once the
  minotaur is beaten (DAT_1006ffd8+2 == 9). The rock types 5 (10014210) and 2 (10013ca0) use solids.c's carry block
  (I_ROCK5 / I_ROCK2 share block_tick; their own pose table 0x100530d8 is approximated by the block's 0x10052e70).
- 0x202d (10036b60 / 10036b90): camera height bias DAT_10070d04 near ledges (-0x20..-0x40), applied in game.c as
  `ty += cam_bias / 2` (the original feeds it to its camera's y follow, FUN_1002fb70).

**Harpy fight (leftovers.c + enemies3.c).**
- 0x3d controller (10011a80 / 10011ac0): Hercules inside x 0x2300-0x2a00, y > 0x400 starts the boss bar
  (DAT_10072188 = 2); when the bar (DAT_10070832, fed by the harpies' hud_boss_damage) is empty DAT_1006ffdc = 99:
  waiting harpies (state 0xf) go, the others flee (FUN_10011300, state 0xc), and enemies3's harpy 0x47 leaves
  (`enemies3_set_harpies_done` replaces the "all 0x38 dead" stand-in when a controller exists).
- 0x3c prey spawner (10011950 / 100119a0): group 0x55; while the THHAFOOD bank is loaded (DAT_100719c0[0x1e] == 3,
  always true here) it spawns 0x39-0x3b from table 0x10052100 at a rate set by how many are alive (max 20).
- Prey 0x39-0x3b (FUN_10011d30, 10011df0): walk, turn at attribute 4; carried (state 1) they hang from the harpy;
  dropped they fall (state 2, anim 8, scream +0x9a + 1) and land (state 3, anim 10); they die outside x 0x22a0-0x2a70.
- Harpy side (enemies3.c): state 8 (FUN_10010e70) now also searches the prey and swoops (state 9, FUN_10011040 /
  FUN_10011020 / FUN_1002a3d0 box test / FUN_10011290 carry start); FUN_10010b90 releases the prey at home. The
  harpies' hunting-spot arrival test (target_dist < 0x20 after FUN_1002b9a0 springs) is rarely met in the port, so
  in normal play they seldom reach state 8; `LEFT_H8=1` loosens it for testing.

**Palette variants.** ENG1 FUN_10003cd0 draws with palette +0x1c when it is not 0xff. `Object.palx` (= +0x1c + 1) and
`bank_frame_pal` (sprite.c) do this; set for townsfolk (0x100602a8 / b8 / bc, fangirls from the spawn flags), prey
(0x10052b38: 0xff or 3) and THCHAR (0x100602b4). Palette 3 of THHAFOOD is very dark: half the prey are near-black
figures, which is what the data says.

**Camera.** Earthquake man (FUN_100343e0): each stomp frame 0x23..0x49 adds (rnd(2a) - a) * 0x40 to the shake list
DAT_10071680, a = table 0x1005ef88[frame - 0x22] * max(0, 0x200 - distance); game.c averages it with the player's
shake as FUN_10026730 does. Medusa's quake (DAT_10070cbc) is now applied (`Objects.cam_dy`). Nessus' right-edge lock
DAT_10070ce8: `leftovers_cam_lock(O, follower d[0])` sets `Objects.cam_xmax`, which game.c's camera clamp honours —
the Nessus follower (nessus.c, merged separately) must call it where FUN_10014e10 does.

**Training.** Cannon aim is the original FUN_1002acf0 + FUN_1002bfb0 (binary search of the cosine half of the sine
table 0x1005aa48 for (|dx| << 15) / distance) + the quadrant fix-up of FUN_1003f1c0; the cannonball now starts as
FUN_1003fa50 leaves it (facing / zface 0xff, signed speeds as both speed and cap), so FUN_1002b4c0's clamps move it
toward Hercules. Phil's off-screen snap uses DAT_10070e6c (visible columns per strip), derived from the camera.

**Forest 0x54** (1000c8c0 / 1000c910): Nessus' defeat pose: anim list, facing left, z = Hercules' z - 0x14.

Not ported: bird flocks Training 0x38 / Forest 0x2e (10043440 / 1000b3f0 + FUN_10043d80 / FUN_100440d0), the 0x2024
shadow / water-reflection child (1000f070 / 1000f180: needs squashed sprite drawing), Thebes 0x2029 light zones
(100117d0 / 10011810: DAT_10071ca0/720/740 tint lists), vertex callbacks 1-7 (0x1005cec0: 100318d0, 10032920,
10031990, 10031a70, 10032a30, 10031a20, 10031bc0, 10032aa0), FUN_10002d80 lesson-machine ropes / texture scrolls,
Forest 0x4b texture scroll (FUN_1000d460), Hades' palette flash (script op -4 level, consumer not found) and camera
zoom, Hydra model morphing / shared neck vertices, Medusa gaze rays as geometry; object brightness / semi-transparency
(+0x4a, flags54 0x400) is stored but objects.c still draws sprites opaque.

## FX pass (work/fx, from base15)

- **Harpies reach hunting state 8.** The port's state-7 handler swapped to anim 0xe and mirrored every tick. FUN_10010dc0 only does that while the state is 3, which is never true there. Anim 0xe's script sets state 6, so harpies never arrived. Also ported FUN_1002ad70's octagonal 3D distance estimate. Result: harpies hover (8), swoop (9) and carry prey (10) without the LEFT_H8 hook.
- **Sprite colour (FUN_100311a0).**
  - The GPU colour word is 0x808080 scaled by +0x4a/128. flags54 & 4 gives code 0x2e (semi) with abr = flags54 & 3.
  - render_sprite_fx adds per-channel modulation (texel*c/128, saturating), PSX blend and a vertical scale.
  - objects_draw uses it; the port's o->semi stands for "| 5" (additive).
  - flags54 & 0x600 objects take their colour from the lighting ramps (DAT_10070d50 / DAT_1006f4e8 / DAT_10070cfc via object +0x2c). Not ported: those draw unlit.
- **Shadows.**
  - 0x2024 child (FUN_1000f070 / FUN_1000f180, sprite mode 5 FUN_10030a20). Spawned for Hercules (FUN_10015090) and for the Forest actors 10008660 / 10009850 / 100099e0 / 10009ec0 / 1000bb10 / 1000ceb0 / 1000cfb0 / 1000d190 / 1000e170.
    - It is the sprite laid flat on the floor (rot x -0xff, yaw 0x40), scale parent - dist/2, average blend at brightness 0, so a 50 % darkening.
    - Below the Forest water line 0x2e1 it turns the other way and goes additive at the parent's brightness: the reflection.
  - flags54 & 8 shadow: FUN_1000ef10 sets bit 8 when the floor is within 0x100 and stores the distance at +0x28. FUN_10003cd0 then calls FUN_10030790, which draws the pieces through the flattening matrix DAT_1005ce50 = {2,0,0; 0,0,2; 0,-2,0} in black, semi, at full size.
  - Both are drawn in fx.c as a vertically squashed billboard. Its height is the projected depth extent of the flat quad. The 0x40 yaw skew is not reproduced.
- **Stymphalian birds** (bank 46), fx_birds.c.
  - Training 0x38: init 100433d0, update 10043440, move 10043d80, choose 10043b50, killed 10043e20, feathers 10043ff0 with table 0x10068970.
  - Forest 0x2e: init 1000ae00, update 1000b3f0, move 1000b250, choose 1000af20, killed 1000b340 / 1000cdd0, perch leaves 1000d0c0 with table 0x10051140.
  - Shared states: 0x26 / 0x1b perched, 0x21 take off, 2 / 0x27 fly, 0x22 swoop, 8 / 9 turn, 0x2b / 0x2c change plane, 0x2d leave, 0x23 shot down.
  - Touch damage: Training 0x11 (and DAT_10070ef0 += 2); Forest 0x12, or 0x14 in state 5.
- **Hades' hair-flame palette.**
  - Script op -4 n copies 32 RGB triples from 0x100693e8 + 0x60*n into DAT_10070248 (FUN_100446d0).
  - FUN_100447c0 uploads them every frame into the child's CLUT slot (FUN_10005820(pal, slot, 0x20): entry 0 transparent, period 32).
  - sprite.c gained bank_frame_cpal for this.
  - There is no screen-wide flash. The camera "zoom" in FUN_10044240 is DAT_10071848, a camera depth offset that keeps Hercules and Hades on screen. Not ported.
- **Carry tables.** The Thebes 0x05 rock (FUN_100142d0) uses pose x/rotation table 0x100530d8, pose y 0x10052c0a, carry height 0x10052c0e and drop height 0x100530de. Both rock and block add the pose-0 x offset by facing, which the earlier port missed.
- **Vertex callback tables.** The sprite draw modes are at 0x1004d6f8: 0 10003f60, 1 10004010, 2 100303d0, 3 10030430, 4 100304f0, 5 10030a20 (flat / rotated), 6 10004c10, 7 1000c720, 8 100310e0. The model vertex transforms are at 0x1005cec0: 318d0, 32920, 31990, 31a70, 32a30, 31a20, 31bc0, 32aa0.
- **Not done:**
  - Thebes 0x2029 light zones and the lighting ramps.
  - Model vertex transforms 1–7, and the sprite draw modes other than 0 / 1 / 5.
  - The lesson-machine ropes and texture scrolls (FUN_10002d80), and the Forest 0x4b scroll (FUN_1000d460).
  - Hydra morphing / shared neck vertices, and Medusa's gaze rays as geometry.
  - Hades' camera offset DAT_10071848.

## FX2 pass (work/fx2, from base17)

- **Sprite palette rule (all levels).** Every in-game bank's palette 0 has entries 0x80..0xff equal to the level CIF palette, within 5-bit rounding; measured across all 80 ENG1 banks in Titans and Thebes. The lower half differs per bank. Stub banks (THUGS 27, THCHAR 29, RockT 49, Spy 64) leave 0x80..0xff zero, which drew them as black silhouettes. On PC the sprite is drawn into an 8-bit screen whose palette is the CIF, so colours 0x80..0xff always come from the level.
  - sprite.c: `sprite_set_level_palette()` (called from game_load_level) applies this to banks whose path starts with `P:` (SRC1/SRC3 game banks). Title/menu `M:` banks are untouched.
  - A palette generation counter flushes decoded frames when the level changes.
  - titans3's PFIX in titans_ai.c becomes redundant but is harmless.
- **Model vertex transforms (FUN_10031540, table 0x1005cec0)**, ported in bosses_models.c `model_draw` / `boss_model_chain`. The PSX welds projected screen vertices; the port welds world positions.
  - Morph, transform 3 (FUN_10031a70) / 6 (FUN_10031bc0): the packed components lerp toward model +0x36 by +0x4c/256, group by group.
  - Chain weld, transform 2 (FUN_10031990): pairs (a, b) from the +0x5c list; vertex b takes the previous member's vertex a.
  - Shared-table pull, transform 5 (FUN_10031a20): here the rig only seeds the chain buffer.
  - Rigs with flags54 0x20000000 only transform their vertices (FUN_10031470).
- **Hydra.**
  - Necks: the rig 0x29 (transform 5; lists 0x100562a0/70/88/58 glue its vertices to the body's exported vertices on the odd necks) plus nine 0xc028 segments (FUN_1001b230: transform 2, list 0x10056240) now draw as one welded tube.
  - Body attachment approximation: the rig seeds the chain from its own vertices, not the body's export table.
  - Head morph scripts (FUN_10028aa0): an entry is {model, start weight, ticks}. Op -0x18 is {end weight, target model}, and op -0x20 sets the weight. A negative start means step by -start per tick.
- **Medusa gaze** (FUN_10022190): rig 0x2f with six 0x2032 parts (model 6). Kids 0 and 3 start the chains; kids 1, 2, 4 and 5 use transform 2 with list 0x10058d78 (pairs (0,4) (1,5) (2,6) (3,7)). Each beam is now a welded additive tube from Medusa's eyes to the gaze head; the sparkle stand-in is gone. Model faces honour object semi-transparency (flags54 & 4, abr = flags54 & 3).
- **Hades camera** (FUN_10044240 → DAT_10071848, eased into DAT_10070d18 by FUN_10026730).
  - Offset = (margin − sep) × 2, where margin is Hercules' distance to the nearest screen edge and sep is the largest distance from Hercules to three points on Hades.
  - Limited to −0x100 and to the 0x40..0x400 range.
  - Eased each tick: by d/8 when |d| > 16, else by half. The camera is pulled back by that amount.
- **ENG3 shadows** (FUN_1001d600; ENG3 handlers 10014270/100142b0 = 0x2002, 100145a0/100145d0 = 0x2017, 10014820/10014850 = 0x2018). Requests are stored on the parent (`E3Obj.fx2_shadow`) and drawn each frame in e3game.c `fx2_e3_shadow`:
  - 0x2002: a bank-slot-1 drop-shadow sprite, frames 0xcd–0xcf, with the per-model tables for Gauntlet and Cyclops; floor within 0x200.
  - 0x2017/0x2018: a flattened 50 % copy, floor within 0x100.
  - Not drawn: the water-reflection branch of 0x2018 (DAT_100410e0).
  - Not wired: Cyclops' shadow requests, because e3_cyclops.c never called FUN_1001d600.
  - Not visually confirmed: in the frames sampled, the casters were too high above the floor. The decision path is confirmed by logs.
- **ENG3 palette override.** The ENG3 reset FUN_1001d8a0 sets +0x1c = 0xff. Crowds set +0x1c to a palette number up to 0x1d; the PEOPLE bank has 30 palettes. e3game now draws through `bank_frame_pal`, and the palette cache was widened to 32.
- **Texture scrolls** (FUN_10002d80), in fx2.c `fx2_tex_scroll`.
  - Table: blocks + 4·(c[0xc] + c[0xe] + c[0x10]), count u16 +0x12.
  - Record +6 = dst x, dst y, w, h, src x, src y (16-bit VRAM units).
  - The wrapped offset is copied in four MoveImage pieces.
  - Forest 0x4b (FUN_1000d460, upd2) scrolls record 0 by −16 each tick: the waterfall animates (Forest record 0: dst 192,0, 64×128, src 0,0).
  - Training target 0x2a (FUN_1003e3d0) scrolls record +0x97 by its move-table z step (the conveyor).
- **Not done.** Recipes:
  - Lighting ramps (flags54 & 0x600) and Thebes 0x2029 tint zones:
    - FUN_10005a60(ambient r,g,b, light r,g,b) builds 8 ramps × 64 colour words at DAT_1006f4e8. Ramp k lights channels by bits of k: R=1, G=2, B=4. Fade is DAT_1006ee84.
    - Default is (0,0,0,0x80,0x80,0x80). Zones push {ambient, light} bytes into DAT_10071ae4[], averaged in FUN_10026730.
    - FUN_100311a0 picks ramp[+0x2c & 7][idx]. idx comes from the level light table DAT_10070d50 = map rel(p+8) entry (+0x2c >> 4). For mode 0x600 that entry is bilinear on the x/y fraction, scaled by +0x24 around the DAT_10070cfc base.
  - Sprite draw modes 2/3/4/6/7/8 (0x1004d6f8: 100303d0, 10030430, 100304f0, 10004c10, 1000c720, 100310e0).
  - Model transforms 1 (ring bend for flags54 0x2000000), 4 (×2 at fixed depth 0xc00) and 7 (×2).

## FX3 pass (work/fx3, from base24)

- **PC colour modulation is one level, not RGB.** HERCULES.EXE 0x40af03 loads only the colour word's first (R) byte into DAT_00a90a00. FUN_0040dd70 / FUN_0040c700 then remap each texel through CIF table +0x300+0x40000, row = level.
  - On THEBES, row k scales luminance by k/128: 0x4d gives 0.60, 0x80 is the identity, 0xc0 gives 1.43.
  - render.c `fx_tint` now uses the R byte for all channels.
- **Level light.** FUN_100311a0 starts every sprite's colour from DAT_10070cf4 = FUN_10005a60(ambient 0, light 0x80, fade 0x80) × the map header colour (ENG1 +0x2c → DAT_10070cf0; ENG3 FUN_1001af10 +0x34 → DAT_10040628).
  - Header +0x2c values:

    | Value | Levels |
    |---|---|
    | 0x73 | TRAINING |
    | 0x80 | FOREST, HYDRA, GAUNTLET, CYCLOPS |
    | 0x4d | THEBES, MEDUSA, TITANS, VORTEX |
    | 0x59 | TORMENT |

  - 0x600 objects use ramp[+0x2c & 7]. +0x2c is cleared by the projection FUN_10032300, so this is ramp 0, which is constant and equals the same colour.
  - Applied in render_flush as `R->light` (game_load_level). The final level is (light × +0x4a) >> 7 & 0xff. `FX3_NOLIGHT=1` turns it off.
  - Map faces (DAT_10073488 = fade × 0x10101 = neutral) and 3D models are not affected (models: not done).
- **Lighting zones (DAT_10071ae4).** These are Hydra-only (FUN_10026730 runs them only when level == 6).
  - Writers are Hydra code at 0x1001ecdf: presets 0x10058a20 + idx × 6 pushed by objects, stage presets 0x100589f8 via DAT_1006fea0 (0x1001ed97).
  - Not ported. There are no Thebes 0x2029 tint zones.
- **Draw modes** (+0x23, table 0x1004d6f8: 10003f60, 10004010, 100303d0, 10030430, 100304f0, 10030a20, 10004c10, 1000c720, 100310e0):
  - 7: nothing is drawn.
  - 8 (FUN_100310e0, THCHAR 0x3f/0x40 with flags54 0x105 from FUN_10035d70) is a dissolve drawn in two passes, ported in fx3.c:
    - first pass (the GPU draws it first): white CLUT DAT_100734ec (FUN_10005710: 31 × 0xffff), abr 2, level b²/128;
    - second pass: colour at abr 1, level 0x80 − (0x80 − b)²/128.
  - 4 (FUN_100304f0): sprite mesh with 16-byte records {x, y, z, frame, flags}, count +0x36. Ported generically; the Thebes sneeze puff (FUN_10033e10, mesh 0x1005e688, 16 records, flags 5) uses it.
  - 2 and 3 (group and rotated group) are only used by models in the decompiled set.
  - 6 (screen space, depth +0x3e) is Titans-only, already done in titans_ai.c.
- **Model transforms** (0x1005cec0: 100318d0, 10032920, 10031990, 10031a70, 10032a30, 10031a20, 10031bc0, 10032aa0), in bosses_models.c:
  - 7: the standard unpack × 2.
  - 4: the same × 2 at fixed OT depth 0xc00 (the depth isn't forced in this port).
  - 1 (flags54 0x2000000): ring bend, θ = bits 8–23 + 0x400, r = s8(bits 0–7) − 0x28c, x = T[θ]·r, z = T[θ − 0x400]·r.
  - No decompiled init uses 1, 4 or 7 for a model.
- **ENG3:**
  - Sprites honour +0x4a and flags54 & 4 / & 3 (FUN_10023b00).
  - Hercules gets the 0x2018 child (FUN_100178a0 → FUN_1001d600).
  - 0x2018 water branch (FUN_10014850; DAT_100410e0 from FUN_1001c720: 0x1e1 Gauntlet, 600 Cyclops / Torment):
    - with no floor it settles on water + 0x20;
    - below the water line it becomes the reflection (flat towards the camera, additive, parent brightness);
    - otherwise the shadow is at brightness 0.
  - Shadow requests were added for:
    - Cyclops 0x2d (FUN_100023a0), 0x402c (FUN_10003bf0, 0x2002), 0x3d (FUN_10004c10), 0x4032 (FUN_10004dc0);
    - Torment 0x29 (FUN_10025700), 0x31 (FUN_10026b80).
  - Not ported:
    - ENG3 per-cell lighting for 0x600 objects (FUN_10023b00 light-table branch, DAT_10040674/1004067c/10040684).
    - The submerged clip (flags 0x50000/0x30000) and DAT_10040ee4 / DAT_10040dc4 reflection scale and yaw.
