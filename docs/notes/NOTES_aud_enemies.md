# aud_enemies: enemies / bosses / combat / training targets audit (from base = live tree, 2026-11-03)

## Fixes (root causes)

1. **Phil and the Training strength meters never ran: AI id collision (the "gate/doll never goes down" report).**
   `nessus.c` used `AIN_FOLLOW/TRIGGER/NESSUS = 300..302`, `training.c` used `AI3_BASE = 300` (`AI3_PHIL = 301`, `AI3_METER = 302`,
   `AI3_TGT30 = 303` ...). `enemy_tick` dispatches `nessus_tick` before `enemies3_tick`, so in TRAINING Phil was run as the Forest
   Nessus trigger and every strength meter (0x2f) as the Nessus object: Phil never moved (stuck at x 1831, anim 0), `T.f0c` (station
   under Hercules) stayed -1, no lesson speech, no route ops 5/6, so the gate (0x3f) and weights (0x44/0x45) were never opened, however many
   targets were beaten. Fix: nessus AI ids moved to 250..252 (`nessus.c`). All other ai ranges checked: enemies.c 1..7, enemies2 101..113,
   nessus 250..252, training 301..311, enemies3 401..409, leftovers 501..515, birds 610, bosses/titans use `kind` dispatch (no clash).
   Evidence (headless, real sword hits, no hooks): after the fix Phil walks to the lesson (route 8 -> 9, anim 7/8), `player st 1 / phil st 1 /
   lesson 1`, targets 0x30 beaten by real blows (`took_hit 1`, `target 0030 hit dmg 4 hp -1`, station 1 counts 1/8 .. 5/8); with all 8 beaten
   and Hercules jumping into the meter prize: `prize 1`, `gate v2a 3 -> 9`, gate z 128 -> 352 (slides into the scenery), Phil moves on to
   the next lesson (`lesson 2`). Before: `phil x 1831 ... route 0` forever and `gate v2a 0` at t=1200.
   Kill/hit counting itself was already right: every target type goes through `hit_damage -> en_took_hit -> en_blow_damage` and sets
   `v2a = 9` (`tgt_beaten`), `count_done` (FUN_1003eb30) reads exactly that; verified real hits on 0x30 (station 1/2), 0x2a (one sword hit,
   hp 4, anim 11, v2a 9), 0x3a and 0x4d stations (counts rise with sword spam). `O->t_hits` is the *damage Hercules took* (DAT_10070ef0), not kills.
2. **Thebes earthquake rocks (0x402a) did nothing.** The THEQ girls (0x2b, FUN_100343e0) spawn 0x402a every stomp frame; its handler
   (init FUN_10034650, update FUN_10034750) was not ported, so no falling boulders and no damage. Ported as `eqrock_init/eqrock_tick`
   (`enemies2.c`, `AI2_EQROCK`): model from table 0x1005ef60, falls from 300 up, tumbles, hurts (level 1), bounces, shatters on first landing
   when big (sound 0xd150, `rock_shatter` = FUN_10024980). Verified by `HIT_SPAWN=402a,-20,0,0`: falls 948 -> 1238 and shatters.

## Verified OK (no change)

* dolls fix and combatfix merged correctly (hits register on sacks 0x30/0x2a/0x3a/0x4d, drums hurt, took_hit box path).
* Enemy attack behaviour, headless, Hercules placed 50-110 units in front with no input (`tools/enemy_probe.py`, `tools/enemy_hit.py`;
  the probe first runs a pass to learn where the enemy really stands after dropping to the floor, then teleports Hercules next to it):
  Forest thug A/B swing and hurt (anim 0xc, level 0x23 strong, hp 128 -> 98 -> 68 ..), Thebes thugs on the z=0/128 walkway also swing
  (the earlier "cannot provoke a swing" was a test placement problem: the spawn y is not the floor y, Hercules fell into a pit),
  skeletons rise (anim 0x22 -> 0x1f) and strike, wrestling centaur 0x29, griffons 0x46/0x47, thug B throws 0x4027 (101 damage calls),
  harpies 0x38 run the full 13 state machine (perch, hunt, dive state 0xb, carry state 10), minotaur only touches/stomps (its anims have no
  movement ops, so a fixed position is correct), rock centaurs throw 0x4a rocks only when they stand 1..3 depth planes behind Hercules
  (FUN_100086f0 state 0xb) and are not hittable themselves (no FUN_10026de0 in their handler): matches the decompile.
  Bosses: Hydra, Medusa, Vortex, Titans enemies damage an idle Hercules (255/48/524/138 `player_damage` calls in 2400 ticks);
  ENG3 Gauntlet/Cyclops/Torment damage events with `E3_AUTO`. `LEVEL_DONE_LOG` finishes: Hydra (t=559), Vortex (t=2041),
  Titans god-mode (7867), unchanged vs the merge build for Medusa/Cyclops.
* Shared toolkit vs decompile: FUN_10026de0 (box 1 vs fireball stream / sonic object / projectile list / player attack box 0x80), FUN_1001ab50 /
  FUN_1001aba0 hit sounds (0x10 / 0x27a with sword hitflags 0x18, else 0x24/0x25), FUN_10019b40 damage, FUN_10030290 (the hit-effect
  tables 0x10051ac0 / 0x10051ac8 are all zero, so a projectile hit just kills the projectile, as ported), FUN_10018c50 damage index
  `((level+1)-1)&7 + difficulty*8` (0x23 = level 3 + strong flag), thug/skeleton/harpy update2 code.
* Actor script ops: every op used by any actor anim list of any ENG1 level was compared with `run_pairs` (`tools/opscan.py`). The only
  op used but not executed is **-0x2e** (FUN_10007ef6: stop the sounds owned by this object; Training targets' burst anim, THSNOW/THEQ).
  Left: it needs a per-object voice owner in the sound layer (audio agent's area).

## Coverage matrix (ENG1 actors placed in maps; full list with banks and init addresses: `tools/matrix_actors.txt`)

| Handler (init / update2) | Type (level) | State | Port |
|---|---|---|---|
| 10001040/100010f0, 10001310/100013c0 + cb 7/8 | thugs 0x26/0x27 (F, Th) | ported | enemies.c thug_tick/thug_ai |
| 10035200/100352f0 + cb 4 | skeleton 0x3e (Th) | ported | enemies.c skeleton_* |
| 10024270/10024320 + cb 6, 10024f50, 10024780 | minotaur 0x37 + rocks/boulders (Th) | ported | enemies2.c mino_*, boulder, mrock |
| 10010540/100105a0 (+cb 5), 100115f0/10011690 | harpies 0x38/0x47 (Th) | ported | enemies3.c harpy_* |
| 10008660/100086f0, 100099e0/10009890 | rock centaur 0x28 + rock 0x4a (F) | ported | enemies2.c rockc/rcrock |
| 10009190/100092c0 | wrestling centaur 0x29 (F) | ported | enemies2.c wrestle |
| 1000d760/1000d7d0, 1000ddd0/1000de50 | griffons 0x46/0x47 (F) | ported | enemies2.c griffon |
| 100410b0, 10041460, 10041f50 | training sharks 0x3c/0x3d, Pegasus 0x42 | ported | enemies2.c shark/peg |
| 1000ae00/1000b3f0, 100433d0/10043440 | stymphalian birds (F, Tr) | ported | fx_birds.c |
| 1003e220/e160 (0x30,0x4d), 1003e370 (0x2a), 1003e580 (0x3a), 1003e6d0 (0x36) | training targets/dummy | ported | training.c, enemies.c dummy |
| 1002cc50/1002cd30, 10040e20 | Phil, strength meter | ported, **was dead (fix 1)** | training.c |
| 1003ce10.. 1003db90 stations, 1003ecb0 gate, 1003eec0 weight, 1003f1c0 cannon, 1003fe40 tops | lesson set | ported | training.c, solids.c |
| 10034650/10034750 | THEQ rocks 0x402a (Th) | **missing -> ported (fix 2)** | enemies2.c eqrock |
| 100343a0.. town actors, 10033fc0/10033910 flash/snow, THEWGUY | Thebes ambient | ported | enemies3.c, leftovers.c |
| 10001d50/10001d80 (0x4027 thug projectile) | thug B | ported (AI_PROJ) | enemies.c |
| Hydra 1001dd00/10022180 + 1001ab00-1001f000, Medusa 10021xxx-10023xxx, Vortex 10044120, Hades | bosses | ported | bosses*.c/.inc |
| Titans 10038090-1003bde0 | Pegasus flight enemies/gods | ported | titans_ai.c |
| ENG3 levels | Cyclops, Gauntlet, Torment | ported | e3*.c (not re-audited here) |

## Remaining / not done

* Op -0x2e (stop object sounds) ignored (see above).
* Harpy attack was checked for the state machine and dive/carry transitions only; no damage event occurred in the 1500-tick probe because
  the probe's Hercules stood on z=512 (the harpies' hunt test needs `P.z` inside the hover band, FUN_10010e70).
* Training sharks/spinning tops/cannon were not driven to a damage event in the probe (their damage code is ported; the player was not on
  their path).
* Weapons (fire/lightning/sonic) vs enemies only checked as "kills instantly" (-100 hp path); the fireball stream truncating at the
  hit segment (FUN_1001a9e0 sets stream length idx+1) was not checked.
* Test hooks added: `PHIL_LOG=1` (Phil state each tick), `EQ_LOG=1` (0x402a rock). Tools: `tools/enemy_probe.py LEVEL TYPE dx=.. dz=.. frames=..`,
  `tools/enemy_hit.py LEVEL TYPE`, `tools/lvtab.py` (handler table dump with "ported" marks), `tools/opscan.py`, `tools/animdump.py`.
  The python tools expect `sp_<LEVEL>.txt` (OBJ_DUMP output) in the scratchpad dir next to them.

## Files changed
`src/nessus.c` (AI ids), `src/training.c` (PHIL_LOG), `src/enemies2.c` (eqrock, `AI2_EQROCK`, `H_EQROCK`), `tools/*`.
Smoke: all 10 levels 600 frames, exit 0.
