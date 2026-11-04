# FXB: flyers hurting Hercules late / not at all (work/fx_birds, from base36)

Bug report: "the birds never touch Hercules easily, or the hit comes about a second after he has been hit".

## Root cause (all levels, every enemy / projectile that calls player_damage)
**A damage request was never dropped.**  `player_damage` (FUN_100266a0) stores the request in DAT_1006feb4 (`P->hit_req`).
`FUN_10018c50` consumes it only when Hercules is not invulnerable and not in the hurt reaction (states 0x11/0x16/0x19/0x1b);
otherwise it returns with the request untouched.  In the original that is harmless because `FUN_100156a0` (the player tick,
called from the main loop FUN_10020300 as FUN_100156a0 -> FUN_1001fd90 -> FUN_10025d00 = object updates) executes
`DAT_1006feb4 = 0` right after `FUN_10015780` EVERY frame.  The port only cleared `hit_req` in the hurt/dying states, so a
bird (or thrown rock, snake, harpy ...) that overlapped Hercules during his 27-tick (0.9 s at 30 Hz) hit invulnerability, or
during the hurt reaction, left its request pending and it was applied the very tick the invulnerability ended: a second hit
"about a second after" the first, from an enemy that had long since moved on.  (analysis/combat_notes.md says "the request
stays pending": it only meant "not applied this tick".)

Fix: `player.c player_tick` clears `P->hit_req` at the end of every tick (after `take_damage` and the state handler),
exactly where FUN_100156a0 does.  Requests made by objects after the player tick (objects_tick runs after player_tick, same
order as the original) still survive until the next player tick, as in the original (1 tick latency, also in the original).

## Other differences fixed
1. **Training bird 0x38 hit order** (fx_birds.c): FUN_10043440 tests "touching Hercules" (FUN_1002a710) FIRST and only if not
   touching asks "was I hit" (FUN_10026de0); the floor test (FUN_10029e20, not in state 0x26) runs afterwards in all cases.
   Forest bird 0x2e (FUN_1000b3f0) is the other way round (FUN_10026e90 first, then touching); both orders are now per bird.
2. **Titans: hurt applied in the frame of contact** (titans.c `titans_apply_hits`, called from game.c after objects_tick).
   The Titans actors call FUN_10037e30 directly inside the object update; the port routed it through `hit_req` and only
   applied it at the start of the NEXT Pegasus tick.  Verified: `[366] actor hurts Pegasus` / `[366] pegasus hit` (was 367).
3. Forest/Training bird touch levels confirmed against the decompile: Forest FUN_100266a0(o, 0x12) (0x14 only in v2a == 5, which
   the bird never has), Training 0x11 (+ DAT_10070ef0 += 2).  The 0x10 bit means "no hurt reaction": a bird hit takes health
   and gives 27 ticks of invulnerability but does NOT put Hercules in the hurt state (that is original behaviour).

## Audited and found faithful to the decompile (no change)
- Hit box: every flyer uses frame box 0 == box 1 (FUN_10030e40 treats k = 0 and 1 the same) against the player body box
  (+0x88, box 1 of the player frame, built in FUN_10025d00 after the player update).  Box z = z0>>1 / z1>>1 of the s8 record, flip
  with flags54 & 0x10 (x) / & 0x20 (y), scale +0x3a applied to origin and extent separately: objects.c `obj_box` matches
  FUN_1002a880 + FUN_10030e40 (the flags54 & 0x80 separate y scale is only used by the Forest rock, not by flyers).
- All bird frames (STYBIRD bank, anims 2/3/5/7/9) have a box; no NOBOX frames in 900-tick runs, so there is no dormant-frame gap.
- Plane alignment: bird boxes are z +-12, player +-10 (+-23 in some turn frames); birds keep Hercules' plane (state 0x22 only
  from the same plane, 0x2b/0x2c shift z by 128 and snap with `(z + 0x400000) & 0xff80ffff`); matches FUN_1000b3f0 / FUN_1000af20.
- Movement: Forest choose FUN_1000af20 (vertical drift v[4] = 0x20000 - k*0x100, upward when within 16 px above the feet, +4 px when
  more than 128 px above), turn / attack distances 0x3aaaaa / 0xb00000, swoop velocity ramps (idx 10-15 / 16-20) and the Training
  bobbing (+-0x60000 keeping the bird 64..128 px above Hercules' feet) all match the decompile line by line.  Consequence of the
  original numbers (not a port bug): Training birds hover 110-128 px above the feet and swoop down only ~36 px, so they reach a
  standing Hercules hardly at all; Forest birds cruise 16..128 px above him and connect on a swoop or when he jumps.
- Thebes harpy dive (FUN_100111a0: y += 16/tick, x += dx/dy16, box 1 vs player, level 3 then re-target), the harpy 0x47 guard,
  Titans actors (touch_peg box 1 vs pbody), ENG3 levels (e3game.c `e3_hurt` applies at once with the 0xec shield, nothing queued).
- Hurt timing: hit applied on the first player tick after the overlap tick (objects run after the player, as in the original);
  hurt script starts that tick; invulnerability 0x3c counted down twice per tick = 27 ticks until the flash table ends it.

## Debug tooling added
- `FXB_LOG=1`: per bird tick one line (`FXB t= post bird .. st anim frm idx vy pos box pbox OVERLAP hit_req inv hp`), plus
  `FXB pt=` lines: every `en_hits_player` overlap, every `player_damage` request and every `take_damage` as APPLIED or DROPPED.
  (`pt` = player tick counter.)  Scripts: /tmp/fxb.sh style run, e.g.
  `FXB_LOG=1 HIT_SPAWN=2e,150 ./build/hercules --data HERCULES.FS --level FOREST --frames 140 --script "" --shot x.bmp`.

## Verification (headless)
- FOREST, `HIT_SPAWN=2e,150` (bird swoops through Hercules): OVERLAP at player tick 64, damage requested the same tick, applied
  at 65 (hp 128 -> 103, no hurt reaction), further overlap ticks 65..67 are DROPPED.  Before (base36 binary): hp 103 at t=90, then
  hp 78 at t=120 with the hurt state 17, i.e. a second hit 27 ticks later although the bird had left at tick 28 of its dive.
  After: hp stays 103.
- FOREST `HIT_TPX=2130 HIT_TPZ=128` (standing under a perched bird, 900 ticks): hits APPLIED at pt 122, 149, 245, 272, 393, 537 only when
  the invulnerability is 0 and the bird overlaps that tick; every request during invulnerability is DROPPED.
- MEDUSA / HYDRA / THEBES / TRAINING / CYCLOPS / TITANS / FOREST start 300 ticks: no crash; `H` key damage hook still applies.
- TITANS 1500 ticks: hits at 366, 414, 462, ...; the first hit is now in the tick of contact.
- Training bird `HIT_SPAWN=38,150`: confirmed the hover/swoop geometry numbers above (no overlap with a standing Hercules).

## Not verified headless
- Thebes harpy / Titans harpy waves / Medusa snakes / thrown thug projectiles: checked by reading the shared hit path
  (hits_player + player_damage + take_damage) and the decompiles; I could not drive a scripted overlap for them, but they all go
  through the request path that is now cleared per tick.
- Real-time feel (30 Hz play) was not play-tested; no audio/video check of the hurt flash.

## Changed files
- src/player.c (clear `hit_req` per tick; FXB_LOG `take_damage` / `player_damage` lines; `fxb_player_ticks`)
- src/fx_birds.c (Training touch-first order, FXB_LOG)
- src/enemies.c (`en_hits_player` FXB_LOG line)
- src/titans.c (`titans_apply_hits`), src/game.c (call it after `objects_tick`), src/game.h (declaration)
