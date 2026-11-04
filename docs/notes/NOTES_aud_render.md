# NOTES_aud_render: rendering fidelity audit (work/aud_render)

Files changed: `src/render.c`, `src/render.h`, `src/level.c`, `src/level.h`, `src/game.c` (2 lines), `src/solids.c` (1 line), `src/bosses_models.c` (1 line), `src/objects.c` (draw call), `src/main.c` (debug hooks only).
Every change is marked `RENDERFIX`.

## 1. The foreground-grass bug: root cause and exact rule

The player is drawn behind grass in front of him. The previous `SPRITE_OT_BIAS = 32` was a trial constant based on a wrong assumption (the cell origin supposedly sits half a depth plane nearer). The real rule, all from the decompile:

| Primitive | OT slot | Source |
|---|---|---|
| map face | `0x521 - cellz - otb` | FUN_1002df20. `cellz` = SZ of the cell origin >> 2; `otb` = face byte +0xf |
| object sprite | `0x4e1 - depth` | FUN_10004010 / 10004330 / 10004650 / 100048b0 / 10030790. `depth` = obj+0x10 |
| model face | `0x4e1 - (avg SZ of the face vertices >> 2)`, clamped to 0x4bf | FUN_10031540 |
| draw mode 6 (screen-space) | the same, `depth` = obj+0x3e | FUN_10004c10 copies +0x3e to +0x10 |

- Low slots are drawn first. Within one slot the LAST primitive inserted is drawn FIRST (head insertion `*prim = *slot; *slot = prim`).
- Both depths are the same quantity.
  - Cell origin SZ = `2*(strip_z - ca8)`: FUN_1002da90 sets obj+0x38 = `strip_z - d1c` and TR z = `d1c - ca8`, and `strip_z` is the cumulative sum from FUN_10020830.
  - Object SZ = `2*(objz - ca8)`: FUN_10032300.
  - Camera `ca8` = plane*128 - 320, the port's `pz - 320`.
  - Cell z and object depth are therefore both `zc/2` in port units.
- In face-key units (`key = 0x521 - slot`, larger = farther = drawn first) an object at depth d has key `d + 0x40`. A face has key `cellz + otb`.
- Check on the data: the map's `otb` byte is exactly 64 + (face's mean depth offset from its cell origin, in slots). Run `OTB_CORR=1`: error peaks at 0 for straight segments, otb range 32..95 for every strip of TRAINING, FOREST and THEBES.
  - So `otb` is the designer's "face depth relative to a sprite standing on the strip plane", and the constant is exactly 0x40. No tuning constant is needed.
- Fixes in `render.c`:
  - `SPRITE_OT_BIAS` is now the derived 64.
  - Depth is integer (`floor(zc/2)`).
  - Sort is (key desc, insertion seq desc), the head-insertion rule, instead of unstable `qsort` ties.
  - `render_sprite2d` (Titans) also gets +0x40.
  - Callers' historical `depth_bias` values (-3.9 .. -9.5, player -8) are mapped to a fraction of a slot, `(bias+8)/32`, clamped to ±0.45. They only order sprites that share one slot (parent / child / shadow). They can no longer reorder a sprite against map faces or other objects.
- ENG3 (FUN_100206f0 / FUN_100231a0): face slot index = `clamp(0x7ff - z, 0x80, 0x7fe) - otb`; sprite slot = `0x7fe - (depth & ~3)`, i.e. face key `(depth & ~3) + 1`. The port's bias of 0 was already right. The `&~3` and +1 are now applied, and ENG3 faces get the minimum shade 1 (`clamp(..,1,max)`), which only matters on CYCLOPS (the only level whose CIF shade rows are not identity).
- `level.c`: the face `org` is the cell ORIGIN (the edge where x = 0, lx = 64 in our mirrored axis), not the cell centre. This only matters on angled and ring segments.
- Model objects (`solids.c`, `bosses_models.c`): faces use the sprite base, and `render_model_begin` gives the model's shade as `depth>>5` clamped 1..0x1d (FUN_10031540), whatever the level's fog. All CIFs except CYCLOPS have identity shade tables, so this is invisible there.

Evidence: `scratchpad/aud_render/v2000.png` (TRAINING x=2000, plane 128, the user's spot; he now stands inside the fern as in the original design). Also sweeps at planes 0/128/256, jumping and crouching in TRAINING, FOREST and THEBES (`s1..s4.png`).
- FOREST x=600/z=128 is hidden by a trunk face of his own plane. That is a test artefact: he cannot stand in solid there, and the face is really nearer (key 218 < 224).

## 2. Other changes made

- **6-strip window (FUN_1002da90).**
  - Only strips `((ca8+0x180)>>7)+4` down 6 are queued; the port drew up to 8.
  - Strips are now queued far to near (also the ring-arena strips), as the original inserts them.
  - Needs `cam.cam_z` (set in `update_camera`). `RENDER_ALLSTRIPS=1` restores the old window.
- **Sprite culling.** ENG1 objects are dropped when `z - ca8 < 0x20` (FUN_10032300) or depth > 0x480 (FUN_100320d0); the port used `zc <= 48`.
- **Map lighting (the "lighting ramps").**
  - Ramp table (FUN_10005a60) and per-vertex light interpolation (FUN_1002ed10, flat/smooth cell words) are ported in `level.c: cell_light`. `Face.light` = the R byte of the face colour word (vertex idx0).
  - PC rasteriser finding (HERCULES.EXE 0x40a8f0 dispatcher, 0x40d420 / 0x40cc50 / 0x40e680 / 0x40dd70):
    - OPAQUE polygons ignore the lighting level completely; only the CLUT shade row applies (and that is identity except on CYCLOPS).
    - SEMI-TRANSPARENT polygons skip the shade, and are modulated by the R level through the CIF 256x256 table.
  - So map lighting only affects semi-transparent map faces: 244 in FOREST at level >= 0x80 (invisible), 560 in TITANS at ~0x4d. Implemented in `draw_face`; `RENDER_NOLIGHT=1` disables it.
  - The levels' light-table values (TRAINING base 0x73, THEBES 0x4d, ...) are baked light, and they matter only for sprites (already done by FX3 as `R->light`).
- **Generic rotated sprite hook.** `objects_draw` now draws a sprite rotated when `obj+0x74` (rot[2]) is non-zero (FUN_10004330 / 100048b0 are chosen by that field). Nothing in the port sets rot[2] on a sprite object yet, so there is no visible change today (`ROT_LOG=1` confirms none).

## 3. Coverage matrix

| Original | Port | State |
|---|---|---|
| FUN_1002da90 cell loop (strip window, order) | render.c `render_level` | ported (now exact window and order) |
| FUN_1002df20 face to OT (slot, shade, colour) | render.c `queue_face` / `draw_face` | ported (slot exact; colour only matters for semi faces) |
| FUN_1002e3b0 / 1002e7a0 cell transform and cull (SZ <= 0xbf) | `queue_face` NEAR_Z | ported |
| FUN_1002ef60 / 1002f120 ring "big" blocks, FUN_1002f340 / 1002ea50 | level.c `build_faces` | ported |
| FUN_1002ed10 per-vertex lighting | level.c `cell_light` | ported (new) |
| FUN_10005a60 ramps, DAT_10070cf4 level colour | level.c `ramp_r`; `R->light` (FX3) | ported (sprites via FX3, map via new) |
| FUN_100311a0 sprite colour word (+0x4a, ramps 0x600, abr) | `render_flush` tint, `fx_tint` | ported; the 0x600 ramp is constant (FX3) |
| draw modes 0/1 (10003f60 / 10004010 / 10004650) | `render_sprite_fx` | ported |
| rotated modes (10004330 / 100048b0, z rotation) | `draw_image_rot` via `render_last_angle` | z rotation only; rot[0] / rot[1] 3D tilt of sprites not ported |
| modes 2/3 (group, rotated group) | n/a | used by models only in decompiled content |
| mode 4 (mesh), 7 (nothing), 8 (dissolve), 6 (screen space) | fx3.c, titans_ai.c | ported (FX3, TITANS) |
| model transforms 0..7 (0x1005cec0) | bosses_models.c, solids.c | ported (FX2 / FX3); 1/4/7 unused by decompiled inits |
| FUN_10031540 model faces (avg SZ slot, shade `depth>>5`) | `render_face`, `render_model_begin` | ported (new) |
| semi-transparency blend 0..3 (CIF tables) | `blend()` | formulas verified against the CIF tables (src,dst order; error = palette quantisation) |
| shade remaps (31 rows) and flat-colour path (DAT_0096e3e0) | `draw_face` shade lut | ported. The flag is always 1 in practice (the compare loop reads row 31 = blend-table bytes), so the flat path never runs |
| camera projection H=320, OFX=160 | GEOM_H / OFX | correct |
| OFY: 0x80 everywhere, HYDRA 0xa0 (FUN_1001ac10), MEDUSA 0x60 (FUN_10021050) | `R->ofy = 128` | NOT per level (see residuals) |
| parallax / extra map layers | n/a | none: every ENG1 map has `nlayers == 1` |
| level params (bg, mode, fog max / shift) at 0x1005df00 + 0x1c*level | level.c PARAMS | verified against ENG1.DLL |
| shadows (0x2024 child, flag 8) | fx.c (FX) | ported (FX); ordering now tie-break only |
| screen-edge clipping | raster bounds | adequate; whole-face reject if any vertex zc < 8 (the GTE clips by SZ) |
| ENG3 map / sprite ordering and fog | render.c ENG3 branch | ported (`&~3` / +1 and min shade 1 now added) |
| Hydra lighting zones (DAT_10071ae4, stage presets 0x100589f8 / 0x10058a20, FUN at 0x1001ed30) | none | MISSING (Hydra only; see residuals) |

## 4. Residual gaps and findings for other domains

1. **Camera follow equilibrium (camera/player agent).**
   - FUN_1002fb70 settles where `(anchor_y - ca4 + OFY)*2 - 0x168 + DAT_10070d04 = 0`, so the player's screen y = `OFY + (0x168 - d04)/2 - OFY` = **212** for TRAINING / FOREST / THEBES / TITANS / VORTEX (d04 = -64, OFY 128).
   - HYDRA: d04 = -96 and OFY = 160 give y = 228. MEDUSA: OFY = 96 gives y = 212.
   - The anchor screen-y bands in the code are 0x20..0xd8, consistent with this.
   - The port's `ty = py - 52` puts the feet at about 180, ~32 px higher than the decompile.
   - Likewise the horizontal lead from DAT_10070d02 = 0x3c gives ±30 px offset (port uses 24).
   - Not changed here (camera domain). If applied, also set OFY per level (Hydra 160, Medusa 96), otherwise Medusa would sit 64 px too high.
2. **Hydra lighting.**
   - Stage light 0x70 -> 0x14 as the fight progresses and head-hit flash presets darken the sprites (FUN at 0x1001ed30, not in the decompile dump; disassembly read).
   - The map is unaffected, as opaque polygons ignore lighting.
   - It is bound to the Hydra boss stage counter DAT_1006f4bc, so it should be done by the bosses agent. Recipe: `R->light` per frame = `light_r * hdr >> 7` from the stage preset `0x100589f8 + 6*stage` and the pushed `0x10058a20 + 6*idx`, averaged as FUN_10026730 does.
3. **Rotating sprite objects.**
   - The renderer is ready (see section 2), but no handler sets rot[2]. Original handlers that rotate sprites and are absent from the port: 0x1001a0f0 (rot -= 0x80), 0x10035b20, 0x10038e30 (+= 0x14), 0x100408b0, 0x10041f00 (+= 4), 0x1003eaf0 (aim angle). Route to the objects agent (these are probably the spinning projectiles and debris).
   - 3D tilt of sprites (rot[0] / rot[1] through FUN_10032810) is not ported.
4. `Face.light` / semi-face modulation are ENG1 map faces only. ENG3 maps (FUN_100206f0) use a different ramp table (DAT_10041460) and are drawn unlit, which is correct for opaque faces.
5. Face-level model splitting for big arena floors (one polygon per original, four-by-four pieces here) means the pieces have individual depths; harmless.

## 5. Verification

- Smoke run of all 10 levels, 600 frames, with a script: no crash.
- TITANS `TITANS_AUTO=1 TITANS_GOD=1 LEVEL_DONE_LOG=1`: finishes at t=7867 -> TORMENT.
- Gauntlet / Cyclops / Torment with the same hooks as the live binary: identical (neither finishes with those flags in 9000 frames, before or after). The changes are render-only.
- Debug hooks added (env, harmless): `OTB_HIST`, `OTB_CORR`, `LIGHT_DUMP`, `SEMI_STATS`, `ROT_LOG`, `RENDER_NOLIGHT`, `RENDER_ALLSTRIPS`.
