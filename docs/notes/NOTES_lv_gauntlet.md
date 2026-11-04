# lv_gauntlet: GAUNTLET (ENG3 level 2) audit

Tree: work/lv_gauntlet (start = base34). Tag LV_GAUNTLET.

## Result
No gameplay divergence found; no behaviour changed. Only test hooks added (src/e3game.c).

## Audited
`E3_AUDIT=1` lists the spawn types of the level: 0x06 (pickups), 0x29 2a 2b 2c 2d 2e 2f 30 32 35 37 3a 3c 3e 3f 40 43 44 46 47 48 49 4b.
Every one is claimed by a ported handler (e3_gauntlet.c GT[], e3types.c TYPES[], e3types2.c TYPES2[], pickup in e3types.c); none falls to generic_update.
Hit kinds are in the HURT table (e3types.c).

Script ops the port ignores (-5, -8, -10, -11, -0x1f, -0x2e, -0x2f; FUN_1001dbc0): static scan of all anim scripts of every
type in the level (`E3_OPSCAN=1 E3_AUDIT=1`) finds none; a full run logs none (`E3_OPLOG=1`). -7 is only logged.
Sounds: all e3_sound calls are tracked per owner object (AUDSHELL); the only looped ids are removed with the owner.

## Added (verification only)
- e3game.c e3_anim: `E3_OPLOG=1` prints ignored/fade script ops hit at runtime.
- e3game.c E3_AUDIT: `E3_OPSCAN=1` static scan of anim scripts for ignored ops.

## Verified headless
- E3_AUTO+E3_GOD run: LEVEL DONE at t=7335 next=FOREST (level completable).
- E3_AUTO without god: damage from types 44, 49, 2f, 37, 2b, 3e, 48, 2c, 2d all fire with the original hit kinds; the pilot dies once
  (kind 0x80 fall at path 7 d=8421), so hazards are fair and reachable.
- Screenshots at 1300 / 2500 frames render hazards (hanging ring, posts, pillars) normally.

## Not ported / remaining (unchanged, from e3_gauntlet.c header)
- 0x2002 / 0x2017 / 0x2018 shadow effects (FUN_1001d600), FUN_10008120 texture animation of 0x46, runner callbacks
  FUN_100013e0 / FUN_100013a0 (lists DAT_10040e40 / DAT_10040fa0): visual only.
- No real audio / window run; dummy drivers only.
