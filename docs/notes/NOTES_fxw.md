# NOTES_fxw: weapon / attack effects audit (work/fx_weapons, start snapshot work/base36)

All edits are tagged `FXW`. New file: `src/fxw.c`.

## Root cause of "the electric sword shows no effects"

Naming in the earlier notes was swapped. By sprite and pickup icon (pickup 0x2015 shows a blue lightning bolt, 0x2016 a flame):

| weapon (P->weapon) | state | original effect | was in the port |
|---|---|---|---|
| 1 electric / lightning (pickup 0x2015) | 0x2b, held sword, aimed with the pad | object 0x4001 (FUN_1001a440 / FUN_1001a5a0): a 17-segment chain of blue zig-zag sprites (bank 2 frames 5..20), additive, brightness 0xe0, via draw mode 4 (FUN_100304f0, flags54 0x20700000) | a 0x4001 object with bank 0 / no script: **nothing was drawn** (damage only) |
| 2 fireball (pickup 0x2016) | 0x2c, aimed strike | homing orb 0x4005 (FUN_10019f50 / FUN_1001a0f0) | spawned at the feet, flew straight 24 ticks, drawn but behind grass and opaque, trail puffs lived 1 tick opaque |
| 3 sonic (pickup 0x2017) | 0x2d | two stationary wave sprites 0x4006 (FUN_1001a2b0 / FUN_1001a390) | projectiles flying away at 10 px/tick, spawned once |
| 4 invulnerability | - | flicker via blend table | already ok |

## What fxw.c now does (all from the decompile, addresses in the file header)

- **Stream (0x4001).** Chain of `S.nmax` = 17 (20 on a pole) segments, 16 px each. Direction `a0 - sum(bend)`, `bend = (sin(phase)*0x24 >> 16) + dev/2`, phase starts at `tick << 7` and steps `0xbc + (sin(tick&0x3f)*0x7e>>16)` per segment, so it crackles every tick. Per-segment lag table 0x100553a8 and the 0x2004 afterimage (old chain, frames 23/25/27, 3 ticks) when the aim angle changes. Sprites are drawn additive, brightness 0xe0, each rotated along its segment. A segment that touches solid ground (attribute `& 0x201 == 1`) ends the chain and throws a 0x2010 spark. Damage: hidden 0x4001 boxes (x+-8, y-8..+0x18, z+-0x10, same as the original segment boxes) are refreshed along the real chain every second tick, so enemies, Titans and bosses (bosses.c box added) are hit where the beam is drawn.
- **Bolt (0x4005).** Spawned at the player + aim-table (dx, dy) (`Spawn.aim` added), velocity from table 0x10055448, homing exactly as FUN_1002b5e0 flags 3 / 0x40 (acceleration 0xa000 / 0x8000, decel 0xf000 / 0xa000, vx max 15 px), target search FUN_1002aea0 (|dz| < 0x30, distance < 0x140), spin -0x80 per tick, additive 0xe0, drawn at player depth - 0x10 (new `render_last_depth`, draw mode 5 with flags54 0x2000). Script frames 29..32 spawn the 0x2003 trail puff every second tick (additive 0xa0, random 45 degree rotation, frames 55..60). Terrain -> 0x4008 burst (frames 41..48), enemy hit -> 0x4009 burst (53..60), as FUN_10030290. Costs 12 charges per bolt (FUN_10019f50). Dies off screen (0x40 margin, 0x100 when homing); the old 24 tick range is gone.
- **Sonic (0x4006).** Two orbs placed at the player (-8, -48), frames 61..68 (behind, depth +0x20) and 69..76 (in front, -0x20), additive 0xa0. Script op 0x0c re-places / restarts them every time it runs (the sonic script runs it twice); both die when the script has run through. Damage = frame box 2 of the first orb only (`fxw_proj_box`, used by enemies.c and bosses.c).
- **Sparks (0x2010).** FUN_10012a10 / FUN_10012b00 (additive, rotates +4 per tick, fades 0xc per tick, jitter table). Used by the stream at walls. (The level end 0x200a sparkle also uses it in the original and still does not.)
- Sword trail / power-punch glow / release arc overlays (script ops 0x07 / 0x1d, frames 477.., 486.., 500..) were already drawn additive by game.c; checked, unchanged.

## Other changes

- `render.c/h`: `render_last_depth` (ordering-table depth override).
- `objects.c/h/_int.h`: `K_FXW` kind, `fxw_init` spawn hook (0x2003 / 0x2010 / 0x4008 / 0x4009), depth fields on Object, `fxw_frame` / `fxw_draw` calls.
- `enemies.c`: old weapon spawn code removed (now `fxw_spawns`), `proj_tick` -> `fxw_proj_tick`, `proj_hit` -> `fxw_proj_box`.
- `player.c/h`: `Spawn.aim`, 12-charge bolt cost, sonic op 0x0c always re-spawns.
- `titans.c`: `sp->aim = 0` for Pegasus' bolt.
- `game.c`: test hook `WEAPON=n` (1..4: select that gift with full charges; refilled when below 200).
- Debug env: `FXW_LOG=1` (per tick stream / bolt / orb box / enemy log), `FXW_SPARKTEST=1` (periodic sparks).

## Verified visually (headless screenshots, FOREST unless noted)

- Weapon 1 stream: horizontal, aimed up (sweeping, with afterimages), Thebes pillar (chain cut at the wall), Hydra and Medusa arenas.
- Weapon 2 bolt: flight, fire trail, burst; homing logged with FXW_LOG.
- Weapon 3 sonic: expanding ellipse around Hercules (Forest, Hydra, Medusa).
- Weapon 4: translucent flicker; sword slash arc; power punch charge glow and release arc; spark object; pickups 0x2015 / 0x2016 icons.
- Damage: FXW_LOG kill counts with weapons 1, 2, 3 on Forest enemies 0x29 / 0x46; smoke runs of TRAINING FOREST THEBES HYDRA MEDUSA TITANS VORTEX with each weapon: no crash; TITANS_AUTO/GOD still finishes at t=7867 (same as before).

## Not verified / not done

- Original is not available to compare; chain shape, speeds and colours come from the decompile, not from a side-by-side.
- Sprite 3D tilt of the sonic orbs (+0x72 yaw table 0x100554d8, FUN_10032810) is not ported (orbs are drawn flat).
- Chain truncation at the first enemy hit (FUN_1001a9e0 cuts `+0x36`) is not done (the beam passes through enemies); damage is the same.
- Pegasus (Titans) bolts / stream: logic runs through the same code and smoke runs pass, but I did not get a visual of them (the scripted Titans start does not show Pegasus).
- The wall spark was checked as an object (FXW_SPARKTEST), not in a real wall impact frame.
- Sound ids are unchanged (script ops: 0x4012 / 0x4026 loop for the stream, 0x400d / 0x4013 sonic, 0x4811 bolt crackle). The bolt burst has no own sound in the original.
