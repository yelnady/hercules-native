# Z-order fix (work/zorder, only src/render.c changed)

Root cause: map faces and sprites go into different ordering-table bases in ENG1.
- Face (FUN_1002df20): OT slot = 0x521 - cellSZ4 - otb (otb = face word1 >> 24).
- Sprite (FUN_10030790 / FUN_10003bd0) and 3D model faces (FUN_10031540): slot = 0x4e1 - depth (obj +0x10 = SZ>>2).
The sprite base is 0x40 slots lower, i.e. sprites are 0x40 FARTHER than a face with the same cell depth + otb.
Our port used key = zc*0.5 + bias (-8 for the player) against face key cellz + otb, so sprites were ~72 too near and
every map face behind/at the player's plane with otb up to ~70 (the big foreground grass: cellz 96 + otb 78 = 174) was drawn
behind him -> "standing on top of the grass".

Fix (render.c): SPRITE_OT_BIAS added to all ENG1 sprite keys (render_sprite) and to dynamic model faces (render_face:
solids, boss models). Not applied to ENG3 levels. Value 32, not 64: with the full 0x40 the Thebes wall (cellz 160, otb 33) and
the Forest trunks (otb 55-59) at the player's plane covered Hercules, which cannot be original; constraints from the
original look: Training grass key 174 must cover him, those keys 193/219 must not -> bias window about 18..37 (player -8 included).
32 = 0x40 minus half a depth plane: the original cell origin (param_1+0x38) is evidently half a plane (0x20 slots) nearer than
our strip-plane org.  Cannot be proven from the decompile alone; the 0x40 base difference is.

Debug: env ZDBG=x,y (320x240 coords) prints sorted item keys whose bbox contains the point.
Evidence (scratchpad zo/cmp_*.png, before top / after bottom): TRAINING x=1380,1430,2800 grass now covers legs/body, targets and
wheels unchanged; FOREST 800/2500, THEBES 900/2400/3200, HYDRA, MEDUSA, TITANS, CYCLOPS unchanged or identical.
