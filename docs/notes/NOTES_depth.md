# NOTES_depth (work/depth, copy of base25)

## Root cause 1 (the "stone drum", Hercules inside it) - FIXED
solids.c I_DECOR (FUN_10040a50, types 0x4031 / 0x4034 decor pieces: gate rollers, training stand wheel/posts):
the original stores v2a bits 0x800/0x100/0x400 as a ROTATION angle (+0x74 / +0x70 = 0x300 or 0x100; model angles are
4 * value in 4096/turn) and never touches +0x3a (scale). The port set scale = 0x300, so every roller / post was drawn
3x too big (96 x 42 x 96 slabs overlapping z 50..206). Hercules stood at z=128 inside that fake volume, drawn behind
its front faces (and the Training stand looked as if it floated in front of him: "wrong plane").
Fix: rot[2]/rot[0] = 0x300|0x100 per bits, d[] words as before, no scale; decor_tick also does the frame 0x36 -> scale 0x50 line.
Before/after: NOTES_depth_before_gate.png / NOTES_depth_after_gate.png, NOTES_depth_before_stand.png / NOTES_depth_after_stand.png.

## Plane switching (Up/Down) - audited, port is faithful, one deviation fixed
Checked against the decompile: FUN_1002ff50 (flags 0x10/0x20 -> [floor128(z), +128] / [floor128(z-64), +128], 0x8000 = handler),
FUN_10017d00 (UP: z < zmax, DOWN: z > zmin), FUN_10019cf0 (clamp), FUN_10017d60 / FUN_10017eb0 (go_plane / st_plane),
callers (idle, skid, turn, land, hurt/0x11; NOT walk - original too), attribute lookup FUN_1002a170 (solid objects first,
then FUN_10020d30 with strip = (z+0x40)>>7), flags table (level +0x44).
- Training only has doorways in the obstacle course (attr 16/17 at cols 412..475, strips 1-4: x ~26400-30000). Up/Down there
  works (headless: z 128 -> 256 -> 128, taps land on plane multiples). Everywhere before that the whole level is ONE plane
  (z=128, the plane of the targets and of Hercules' start, ENG1 table z = strip<<7); Up correctly does nothing there.
- Deviation fixed: st_plane's release snap for the "front" direction (zface==0). Original: `prev==cur || prev<cur`
  (prev-cur <= 0); port had `&& prev != cur`.
- Deviation fixed: level flags table was copied for 24 entries; the original indexes memory directly (Thebes uses attr 25
  = 0x8200 handler and 26 = 0x207, Training 27 = 0x207). Now 64 entries.
- solids.c wbox: FUN_1002a880 mirrors the box y when flags54 & 0x20 (was missing).
- Start / respawn: ENG1 level table row = (x-0x20, y+0x40, strip<<7). Port uses the raw row; left as is (a few units, not a plane issue).

## Test hooks added (all env gated, shot mode only unless noted)
PLACE_Z=n (with --x), PLANE_LOG=1 (player.c, attr/flags under player), WALL_DBG=1, INSIDE_LOG=1 (player body inside solid attr /
embedded ground), SOLID_AUDIT=1 (drawn extent vs collision box of every model object), ATTR_SCAN=1 [ATTR_WHERE=a] [CELL_DUMP=col]
[BLOCK_DUMP=id] [MODEL_DUMP=idx], ATTR_Q=x,y,z, CP_LIST=1, ONLY_STRIP=n (render one depth strip).
Ghidra headless decompile used: copy of /tmp/ghproj_solids + tools/DecompAt.java (-process -noanalysis).

## Verification
LEVEL_DONE_LOG: CYCLOPS (E3_AUTO/E3_GOD) done t=10495, TITANS (TITANS_AUTO/GOD) done t=7819. Thebes doorway (x=1200, Up) z 0 -> 128.
Training doorway z 128 -> 256. INSIDE_LOG random-walk runs on TRAINING/FOREST/THEBES/HYDRA/MEDUSA: no events.
