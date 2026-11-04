# aud_player: player / physics / camera / input audit (ENG1 + ENG3 pilot)

Files changed (vs the snapshot): src/camera.c (new), src/game.c, src/game.h, src/player.c, src/player.h,
src/objects.c (obj_collect only), src/solids.c (1 line), src/training.c (1 accessor), src/main.c (hooks + 3 lines).
Search `PLAYER:` in the sources for every edit.

## Coverage matrix (original -> port)

| Original | Port | Status |
|---|---|---|
| FUN_10015780 per-tick (hitflags, invuln x2/tick, weapon timers, flicker, shake, fly cheat) | player.c player_tick | ported (fly cheat 0x1b / Pegasus 0x28 not reachable by design) |
| state table 0,1,2/4/5/0x13/0x17,3,6,7,8,9,0xb,0xc,0xe,0x10,0x11,0x12,0x14,0x16,0x18,0x19,0x1c,0x29,0x2a,0x2b-0x2f | player.c st_* | ported, all reachable |
| state 0xa (level start hold, FUN_10019440) | ST_START | **was missing**, fixed |
| state 0xd Nessus ride | player.c / nessus.c | ported by NESSUS |
| script ops FUN_10015d00 (0x00-0x33) | run_script | all ported; 0x02 (carry swing, DAT_1006ef9c), 0x07 water hide, 0x16 stop-sound 0x15 were missing, fixed; 0x24 (DAT_1007183c) only matters in Training/Pegasus, see camera |
| FUN_10016xxx entry/transition helpers | go_* try_* | ported; carried-object probe offset (+28 while carrying, FUN_10019900/19820/19890/197b0) was missing, fixed |
| FUN_100174e0 air control | air_move | climb (0x13) facing flip and swing-jump (kind 5) braking were missing, fixed |
| FUN_10018c50 damage / FUN_10018f60 death | take_damage / go_die | matches (checked branch by branch); lives cheat 8 added |
| FUN_10012ef0 pickups | objects.c obj_collect | **health, extra life, max health, all four Gift ammo pickups never reached the player**: fixed |
| FUN_10012040 weapon select | player_tick | partial: the original only accepts a change when the HUD wheel is aligned ((DAT_10070820>>16)%12==5), so holding R1 steps every ~24 ticks; port steps on every press |
| FUN_1002ff50 special attributes (flags & 0x8000 -> handler) | plane_zone | Thebes attr 25 (FUN_100117b0: x <= 0x29fe0000) was missing, fixed; no other special attr occurs in any level |
| FUN_10030010 / FUN_1002a170 attribute test, flags table @0x10068af0 | solid(), level.c | ported (only mask 1 and 0x3000 are used by the player; 0x10/0x20 doorways via plane_zone) |
| FUN_10029c20 ground follow, 10029da0, 10029e20, 10019a10, 10019aa0, 10019d70, 10019c30, 10019d30 | ground_follow, rise_snap, land_test, move_x, vertical, clamp_* | ported (x/y clamps match; the strip-window clamp (DAT_10070e2c) is irrelevant because the port holds whole maps) |
| FUN_100296e0/10029b70 platform following, FUN_10019970 object push-out | solids.c (obj_floor/obj_attr) | ported by SOLIDS |
| FUN_1002fb70 camera follow + FUN_10026730 bias | camera.c | **was an approximation (0.15 lerp, +-24 look-ahead, y = py-52)**: ported exactly for the side-scrollers (see below). Ring arenas (Hydra/Medusa) and Titans keep the old follow |
| level table row start (x-0x20, y+0x40), camera at (x, y) | game.c place_start / table_row | **was raw row x/y**: fixed; checkpoint restart now uses the table row of the checkpoint id, not the checkpoint object position |
| level reload on death (FUN_1001f320) | game.c place_start (g_respawn) | Training/Forest/Thebes now reload the object list on restart (Titans already did); coins/letters/scrolls/checkpoint kept |
| in-game fade (DAT_1006ee84 +8 at start, op 0x2a -8 on death) | game.c fade | **was missing**, added (interactive play only; headless shots unaffected unless FADE_TEST=1) |
| input: pad masks, pressed/held, Up/Down | input.c, game.c to_pad | checked, matches (sword=SQUARE, jump=CROSS, punch=CIRCLE, power=TRIANGLE, R1 next, L2/R2 cheat) |
| ENG3 pilot (e3game.c) | | not changed (verified: Cyclops god-mode finish still `LEVEL DONE ... next=TITANS`) |

## What changed in behaviour, with evidence

* **Camera (camera.c).** Original: 2 iterations per tick; x error = (px-camx+0xa0)*2-0x140 -/+ 0x3c (look-ahead 30 px, 32 in Hydra), step = index into the threshold table 0x1005e058 (err/20 for small errors, max 16), clamped to [0xa0, cols*64-0xa0] (the port clamped 64 px tighter on both sides). y error = (py-camy+ce0)*2-0x168 + DAT_10070d04, amplified up to 2x when the player is above 0x20 / below 0xd8 on screen, camy in [0x7c, rows*64-64]. **DAT_10070d04 is -0x40 in every level except Training (-2, eased to -0x40 in lessons 0xc, 0xf-0x11, FUN_1003ca50)**, so in Forest/Thebes/Vortex Hercules' feet sit about 32 px lower on screen than before (feet at y~211 of 240; the thresholds 0x20/0xd8 in the original make sense only for that framing). z follow eases the camera depth between planes (before: instant). The camera now runs before the player update, as in FUN_10020300. Nessus' cam_xmax is the original's left limit DAT_10070ce8 (ratchet), not a right limit. Screenshot: /private/tmp/.../scratchpad/aud_player/c_THEBES.png.
* **Start/restart.** Hercules starts at (row.x-32, row.y+64) in state 0xa for 2 ticks, then idles; the 20 px drop to the ground and the land animation/sound happen as in the original (before he fell ~68 px from the raw row y). Restart uses the table row of the last checkpoint id.
* **Pickups.** Hercules' health potion (+0x20, sound 0xc03b with 50-tick cooldown then 0xc03f), extra life (cap 9), max-health upgrade (+0x20, cap 0xff, HUD bar follows) and the Gift pickups 0x2014-0x2017 (+0x80 charges, cap 0xff; 0x2014 = invulnerability, 0x2015 fireball, 0x2016 lightning, 0x2017 sonic) only changed Objects.* counters nobody read. Test: `HIT_SPAWN=2013,0` heals 103 -> 128; `HIT_SPAWN=2015,0` + W selects the fireball with 128 charges.
* Water: Training water line (0x360) now known to the player (footstep splash 0x401e-0x4020, landing splash 0x4276 in Forest/Training, sword trail hidden under water). No footstep sound exists in the original except these water ones (checked: walk/run scripts contain no sound op), and the plain ground sword script has no swing sound either.
* Misc: carry-turn swing of the held object, free-space probes while carrying, climb-hop facing, swing-jump braking, hang clears the sword lock, sword-end chaining only for the plain sword, hurt state can switch plane, power-punch release stops sound 0x15, lives cheat (+1 per death) wired from the cheat screen.

## Remaining / not done

* Weapon select cadence (HUD wheel gate) not modelled.
* Camera for ring arenas (Hydra/Medusa): the original uses ce0 = 0xa0 / 0x60 as the screen centre and y bias -0x60 / -0x40 with the GTE offset set per level; the port's renderer fixes the centre at 128 and the arena start y differs from the table by about 64 (table y+0x40 puts Hercules below the Hydra floor), so I left the old follow. Experiment (not kept): the exact follow puts Hercules about 17 px lower in Hydra.
* Titans camera (anchor-based) unchanged.
* Death by falling out of the level: the original's state 0x17 loop has no visible end in the decompile (death source unknown); the port kills after the fall.
* Cheat flags 0x200-0x1000 (start ammo 0xff) are not reachable from the UI.
* Training lesson bias uses training.c's station variable; untested beyond a smoke run.
