# fix31: Training lessons play-test bugs

Work tree: `work/fix31` (start state = `work/base31`). All edits are marked `FIX31`. Decompiles used: ENG1 `FUN_1003cb90` / `1003cf90` /
`1003d440` / `1003d930` / `1003dcd0` (station updates), `1003e160` / `1003e280` / `1003e3d0` (targets), `1002cd30` / `1002d440` /
`1002d1f0` / `1002d320` (Phil, callbacks 2 and 4 of the Training callback table 0x10068138), `100291d0` / `10029160` (movement),
`10007f8e` (stop sound), `10002d80` (texture scroll), `1003ce10` (stomp stage init).

## What was wrong

1. **Rope sound forever / lowering.**
   - The crank sound (0x5f, a *looping* ESF; 0x60 from the stomp stage) was started with an owner (`snd(O, id, NULL)`) and never stopped:
     the original stations call `FUN_10007f8e(id, 0)` when the crank speed (`DAT_10070ee6`, Phil +0x4c) drops to 0 and `FUN_1003cb90` also stops it
     when the dolls station is culled. Now `station_tick` starts it with `obj_sound_none` (owner 0, as `FUN_10006da0(id, 0)`) and stops it with
     `obj_sound_stop` (the id that was started is remembered in `T.rope_snd`).
   - Phil's crank speed was forced to 0x10 (`T.ee6 = 0x10` while v2a == 7). The original keeps Phil's own +0x4c (4 / 6 from his anim 18) and only
     forces 0x10 for the "all done" drop (efe == 2). Removed (affects the crank wheel 0x4031 speed and the rope scroll).
   - The meter (girl) only dropped in one go after the last target because Phil never cranked during partial progress (see 2). It now sinks with
     the share of beaten targets (d1 * beaten / total, +2 px per crank tick) and then all the way (d2, +4 / +2 px per tick), always stopping at its
     target (no overshoot beyond the 2 px step; verified y 592 -> 624 (4/8 beaten) -> 752, the table limit d0 + d2).
   - The winch rope textures were never scrolled: `FUN_10002d80(rec, 0, n)` is now called as in each station (rec 5 for the stomp stage / pendulum,
     rec 0 for the others, pendulum rec 1 by the swing delta; `station_meter(O, st, m, rec, n1)`, `fx2_tex_scroll`).
2. **Stomp stage does nothing.** The stage (0x32) only rises while Phil cranks (`efc != 0`). Phil only cranks through his script callback
   `FUN_1002d1f0` (callback id 2 in the level's callback table, called by script op -10 with `+0x124`, set by route op 3): with `DAT_10070efe == 1`
   (a station wants to move) he turns to the player and plays anim 0x11 -> 0x12 (state 7 = crank), with `efe == 0` he stops (anim 0x13), otherwise he
   turns towards Hercules / plays random idle gestures. Callback 4 (`FUN_1002d320`, lessons 6+) is the idle part only. The port had **no** Phil
   callbacks, so `efc` stayed 0 and neither the stage nor the meters moved before every target was beaten (and the stage never, because its
   `efe` is 1, not 2). Ported as `enemies3_callback()` (training.c, hooked into `enemy_callback` in enemies.c): Phil now cranks in partial progress,
   idles with gestures. Also: `FUN_1003ce10` restores the risen stage y from the station table (entry +8) when it is recreated and keeps the base y
   in +0xa0 (`solids.c` I_ST_32 init, `training_tbl_set`); the port always respawned it at the base height.
3. **Moving boxes freeze.** The walking targets (0x2a, station 3) were moved with `FUN_10029160` semantics (`en_move4`: 4-byte {dx, dy} entries, no
   z) while the original `FUN_1003e3d0` uses `FUN_100291d0` (6-byte {dx, dy, dz} entries, z by the z-direction byte). Their table only has dz, so in
   the port the boxes never moved at all (z stayed 128 / 208 / 288 / 304) and a hit left them standing. `tgt2a_tick` now uses `obj_move_table`.
   Now they walk in depth between z -0x18 and 0x130, turn at the ends, stop for the hit anim (anim 5 / 8 with `-13`) and walk on (the null anim
   slots 6/7 and 9/10 hold the "return to anim 0 / 1" index, handled by the existing `next_anim`); a beaten box stays down.
4. Other fixes found while scanning the sequence: pendulum stage (0x3e) far pulleys (0x4031 with z > 0x120 turn with the swing, `FUN_1003dcd0`);
   `FUN_10002d80(1, 0, -(prev - a))` rope scroll of the pendulum.

## Files changed

- `src/training.c`: `station_meter` (texture scroll), `station_tick` (rope sound start/stop, texture scrolls, 0x4031 speeds), `tgt2a_tick`
  (movement), `phil_tick` (crank speed), new `enemies3_callback` / `phil_idle`, `training_tbl_set`, test hooks.
- `src/solids.c`: I_ST_32 init (stage home y, restore of the risen y).
- `src/enemies.c`: `enemy_callback` calls `enemies3_callback` first.
- `src/objects_int.h`: declarations (`obj_sound_none`, `enemies3_callback`, `training_tbl_set`).
- `src/sfx.c`: `sfx_stop` logs a line with `--sfxlog` (verification only).

## Test hooks (all env, no effect when unset; training.c `enemies3_frame`)

`TRAINING_BEAT_ST=n` (limit TRAINING_BEAT to station n), `TGT_KILL=N` (beat one more target of Hercules' station every N frames),
`TGT_POKE=N` (non-fatal blow on the first walker / drum every N frames), `TRAINING_PRIZE=1` (take ready prizes), `TP_AT="frame,x[,y];..."`
(teleport Hercules), `SP_LIST=1` (station table dump), TRAINING_LOG now also prints z / anim / moving of targets and the stage y.

## How verified (headless, `--level TRAINING`)

- Dolls (station 1) with `TGT_KILL=50`: Phil idles with gestures, cranks (anim 17 -> 18, efc 1) per beaten target, meter y 592 -> 624 -> 640 -> 648
  then 670..750 -> 752 at the end (stops, no further movement for 400 frames); `--sfxlog`: `sfx id=0x005f loop=1 ... owner=0` is followed by
  `sfx stop id=0x005f` every time the crank ends (before: no stop at all).
- Stomp stage: walk, jump on the beam, ground pound x4: beaten 2/7 -> 6/7 -> 7/7, stage y 709 -> 645 in 1 px steps (before the callbacks: stayed
  709 until all were beaten and then never rose), Hercules is carried.
- Boxes (station 3): z now cycles 304 -> -24 -> 304 (`tgt 002a` log), a poked box plays the hit anim and walks on; a real punch kills it (v2a 9).
- Whole sequence with `TRAINING_BEAT=1 TRAINING_PRIZE=1` and Hercules walking right: stations 1-5 (girl, stomp, boxes, pendulum, carousel with the
  weights / gates) progress to lesson 6 (the free-practice station, Phil follows Hercules, idle gestures with callback 4); teleports to the later
  stations 7-15 run with no stuck state or crash.
- Earlier fixes untouched (grass draw order, hit boxes, doll drop trigger dx < 0x70 / dz < 0x90, checkpoint chime): only training.c / solids.c
  I_ST_32 / enemies.c callback chain / sfx stop log changed.

## Not done / notes

- The meter's rope child 0x34 (`FUN_1003e9b0` / `1003e9d0`, +0x26 = rope length) is still not spawned; the rope the port draws is static.
- When several stations are alive the first one ticked starts the crank sound (0x60 from the stomp stage, 0x5f otherwise), as in the original.
