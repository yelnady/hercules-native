# FXL: level backdrop / clear colour / lighting investigation (work/fx_light, from base36)

## Question: is the TRAINING sky really black in the original?

Yes. The original draws black behind the Training Ground trees. Evidence, all from the original binaries:

1. **Level table** (ENG1.DLL 0x1005df00 + 0x1c*level): TRAINING (row 1) bytes +8..+10 = 00 00 00, +0xc = 2 (isbg set).
   FOREST (row 3) = 00 40 80, HYDRA 3f 53 6f, VORTEX 00 20 00; THEBES, MEDUSA, TITANS, CYCLOPS, TORMENT = 0.
   Designers did set a colour wherever they wanted a visible sky, so black here is a decision (or the default), not a loader bug.
2. **How the table becomes pixels.** ENG1 FUN_1001f320 copies +8..+10 to DAT_100734f0; FUN_10005e00 (called from the ambient
   setter FUN_10005a60) stores it as DRAWENV r0,g0,b0 (+0x19..+0x1b) and `isbg = table+0xc & 2` (+0x18).
   HERCULES.EXE `DrawOTag` (0x40ac60, found by disassembly, it is not in the REA dump) tests isbg, then calls 0x406ee0, which maps
   (r,g,b) to the NEAREST ENTRY OF THE 256-COLOUR PALETTE (0x406850) and `rep stosd`s the 8-bit 320x240 frame buffer with that
   index. (r,g,b) = 0,0,0 gives palette index 0 = (0,0,0) in every level's MAP_PSX.CIF (0 and 255 are forced black/white by the
   palette setter 0x406780). So the clear is black. There is no other background fill in the exe (DAT_0096e680 DRAWENV is read only here).
3. **No sky geometry or backdrop image exists in the map.**
   - Every ENG1 map has `nlayers == 1` (no parallax layer): TRAINING 5 strips / 7 sections / 500x16 cells.
   - ZDBG probe of a black pixel between trunks (x=2000) returns no face/sprite item, so nothing is culled or dropped; the gap is empty cells.
   - Header +0x12 "scroll records" (FUN_10002d80, count 6 on TRAINING, 1 on FOREST and VORTEX) are VRAM texture scrollers, not
     backdrops: TRAINING records are 8x64 / 2x64 word regions (lesson machine ropes / conveyor), FOREST rec 0 = waterfall 64x128 at
     (192,0), VORTEX rec 0 = 128x256 at (0,256). All already ported (fx2.c `fx2_tex_scroll`).
   - Palette index 0 as transparent is not the cause: index 0 is black anyway, so opaque or transparent, the Training gaps are black.
4. FOREST's blue (visible in every shot) comes only from the same table entry + clear, confirming the clear path is the sky path.

So the screenshots (TRAINING at x = 2000 / 8000 / 14000 / 24000 / 31000) are what the PC original shows: a dark forest yard with
black behind the trunks (at 0x73 map colour the level is baked slightly dark). Not a port bug.

## What was ported (FXL)

- `src/level.c` (`level_load`, tag FXL): the clear colour is snapped to the nearest palette entry exactly as 0x406850 does
  (squared RGB distance, first minimum, exact match stops), instead of being used as raw RGB.
  Black levels: pixel-identical. Coloured skies: FOREST 0,64,128 -> idx 232 = (14,74,111) (slightly greyer, visible),
  HYDRA -> (66,82,111), VORTEX -> (0,31,0), GAUNTLET -> (63,116,129). `FXL_NOSNAP=1` restores the raw table colour.
  Applies to ENG1 and ENG3 (both go through the same DrawOTag).

## Verified (before = FXL_NOSNAP=1, after = default; 520 idle frames after HIT_TPX teleport)

| Level / x | Result |
|---|---|
| TRAINING 2000, 24000 | identical (0 px differ); black backdrop is original |
| THEBES 12000, MEDUSA 600, TITANS 3000 | identical |
| CYCLOPS, TORMENT | identical |
| FOREST 1500, 8000 | sky colour changes only (diff confined to sky/gaps) |
| HYDRA 600 | 3-unit sky change |
| VORTEX 300 | 1-unit change |
| GAUNTLET | sky 66,123,121 -> 63,116,129 |

No regression of grass / sprite order: render.c untouched, TRAINING frames byte-identical.

## Lighting status (what is and is not left)

Already ported before this pass (checked, nothing to add): map lighting ramps (level.c `ramp_r`/`cell_light`, only semi-transparent
faces honour it because the PC opaque path ignores it), level light for sprites (`R->light` = header +0x2c: TRAINING 0x73,
FOREST/HYDRA 0x80, THEBES/MEDUSA/TITANS/VORTEX 0x4d), texture scrolls.

- **Thebes "tint zones" do not exist** (type 0x2029 has no ramp writer; confirmed in aud_render notes). Nothing to port.
- **Not ported: Hydra light zones** (the only per-level dynamic light). FUN_10026730 (level 6 branch) converges the current light
  DAT_1005a7b8 toward the stage preset (table 0x100589f8, 6 bytes ambient rgb + light rgb: 112, 90, 70, 50, 30, 20, 0)
  by half the difference per tick, or averages pushed zone presets (0x10058a20 + 6*idx, list DAT_10071ae0/ae4, pushed by the head-hit
  code at 0x1001ecb0..), then calls FUN_10005a60. Result is level = ambient + light * hdr/128 for sprites only. The stage counter
  DAT_1006f4bc (0..5, advanced by DAT_10071ecc countdown in 0x1001ed30) has no counterpart in the port's boss code, so it needs the
  bosses agent; left untouched to avoid guessing.
- ENG1 per-level OFY (Hydra 0xa0, Medusa 0x60) still not per-level (camera agent, see aud_render notes).

## Test hooks
`FXL_NOSNAP=1` (level.c). Screenshot helper used: HIT_TPX=<x> --frames 520 with SDL dummy drivers.
