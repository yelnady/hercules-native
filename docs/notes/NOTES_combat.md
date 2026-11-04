# Combat / hit-box audit (work/combatfix, from base25)

## Root causes
1. **enemies.c `took_hit` replaced real boxes with a fake one (main cause of "hits do not register").**
   A SOLIDS hack substituted a default box (x +-28, y -64..0 above the hotspot) whenever the object's frame box 1 reached
   more than 16 units below its hotspot ("rock centaur's rock under the floor"). It ran for EVERY enemy/target that
   goes through `en_took_hit`. Things that legitimately hang below their hotspot (training sack targets 0x30/0x4d,
   the 0x36 dummy, anything on a rope; box y +25..+68) got a box 64 units above the sack, so the sword never touched it.
   Fix: the under-floor substitute now only applies to the rock centaur inits (0x10008660, 0x100099e0); the "no box at all"
   fallback for generic actors is unchanged. FUN_10026de0 itself just uses FUN_1002a880 box 1.
2. **bosses.c `sbox` scaled boxes twice** (obj_box already applies +0x3a). Now `sbox` = `obj_box`.
3. **objects.c `obj_box` fidelity** (FUN_10030e40 + FUN_1002a880): scale (+0x3a) is applied to origin and extent
   separately (x*s>>8, w*s>>8) instead of to the far corner; empty (w==0 && h==0) boxes return 0; flags54 & 0x20 mirrors y.

## Audited and found correct (matches the decompile)
- Frame record layout: count at f+1, boxes 6 x s8 {x0,y0,z0*2,x1,y1,z1*2}; box1 body, box2 attack, box3 hand/extra
  (player.c frame_box, objects.c frame_box, e3game.c frame_box). z is >>1.
- Facing flip: flags54 & 0x10 mirrors x about the hotspot; sprite draw uses the same flag (images checked vs boxes).
- Sprite hotspot == box origin: the image rectangle (magenta in the overlay) and the boxes share the object position.
  The player's body box is narrow (e.g. -6..17) and the lunge frames draw the body outside it; that is the original data.
- Attack timing: hitflags set by script op 0x0a, `consumed` cleared per tick (player.c:1309), one damage event per blow.
  Sword hit box is live from frame 188; the swing box (frame 189, +8..+72 wide) is the one that normally connects.
- Enemy -> player: hits_player uses box k vs player body box 1, z tolerance from the boxes (+-13 player). Verified with the
  0x4d drum (damage 18, hurt state 17, 60 ticks invulnerable).
- ENG3 levels (e3game.c frame_box / world_box) already follow FUN_10023860 / FUN_1001f1a0.

## Debug / test tooling added
- `HIT_BOXES=1` (game.c draw_hitboxes): overlay on the screenshot: white = player body, red = player attack box,
  green/red/blue = object boxes 1/2/3, magenta = player sprite image rect, yellow cross = hotspots.
  Boxes are drawn as their front face at mid z.
- `HIT_BOXES_LOG=1`: print player box 1..3 (relative) and image size each frame drawn.
- `HIT_AUDIT=1`: per object: box 1 vs drawn image rectangle (flags boxes < 60% inside the image).
- `HIT_LOG=1`: logs every attack-box/body-box test in enemies.c took_hit (with OVERLAP/miss), player blow hits, and
  every player_damage call.
- main.c test hooks: `HIT_TPX=x` (teleport at tick 20; keep plane; `HIT_TPY`, `HIT_TPZ` optional) and
  `HIT_SPAWN=type,dx[,dz[,dy]]` (spawn an object in front of Hercules at tick 40; hex type).
- `tools/hitcheck.sh LEVEL out.png <engine args>` runs headless and converts the shot to PNG.
  Example (TRAINING sack, sword from x=1448):
  `HIT_LOG=1 HIT_TPX=1420 tools/hitcheck.sh TRAINING /tmp/o.png --frames 140 --script "<50 dots>S<...>"`
- Screenshots: notes_shots/*.png.

## Verification (headless, before fix: 0 hits in all runs; after fix)
- TRAINING walking right with the sword at x=1360..1512: OVERLAP + "player hit mask=8" at every distance (before: all misses).
- TRAINING punch, crouch punch, power punch, air sword at x~1448 against sack: all register
  (masks 4, 4, power, 8).
- Dummy 0x36 spawned at dx 60 / dy -70/-90: sword hits on frame 189.
- Drum 0x4d overlapping Hercules: player_damage called, hp 128 -> 110 -> 92 -> 74, hurt state 17.
- Regression runs FOREST/THEBES/HYDRA/MEDUSA/TITANS/CYCLOPS start, 300 ticks: no crash, builds clean.

## Not changed (left to the other agents)
- player.c depth-plane / Up code, solids collision, enemies.c dummy_tick death reactions.
- Known remaining limitation: Thebes thugs live on the z=128 upper walkway; I could not drive a real thug fight from the
  headless harness (spawned thugs stay in their idle script), so enemy attack boxes were verified through the shared
  hits_player path (drum) and by overlay alignment, not against a thug swing.
