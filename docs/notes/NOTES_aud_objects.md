# aud_objects: level objects, object scripts, sounds (audit of the port vs ENG1)

Base: work/base28 plus aud_render's render.c/render.h/level.*/game.c/objects.c/solids.c/main.c/bosses_models.c, 3-way merged
into this copy (no conflicts). Audit hooks (env): `SPAWN_AUDIT=1` (spawn every map record once, print handler kind),
`SPAWN_AUDIT2=1` (spawn every type index of every pool, print which fall to GENERIC / NONE), `SND_AUDIT=1` (sound ids in
object scripts that the level sound table lacks), `SFX_MISS=1` (runtime: a played id missing from the table), `FENCE_TEST=1`,
`PHIL_DBG=1`, `TRAINING_LOG=N` (now also lists targets, meter, gate). Scratch scripts: scratchpad/aud_objects/{pe.py,scan.py,handlers.py}.

## Root causes found and fixed

1. **Training lessons dead (Phil, strength meters, therefore "the thing that should go down does not")**: nessus.c
   `AIN_FOLLOW = 300` collided with training.c `AI3_BASE = 300`. `nessus_tick` runs before `enemies3_tick`, so Phil
   (AI3_PHIL = 301) ran Nessus' trigger handler and the meter (302) ran the Nessus boss handler. Phil never walked/spoke/cranked,
   the meter never sank, the prize/gate/weight never opened. Fix: Nessus ids start at 700. Verified headless with real
   sword/jump-sword kills (script `RRRjjjSSS`, teleport 1400,600,128): 8/8 dolls beaten, meter 592 -> 752 (prize ready),
   prize touched (v2a 16), gate opens (z 128 -> 352, state 9), Phil walks on to lesson 2.
2. **Pickups gave nothing to Hercules** (obj_collect changed Objects counters that nothing reads): now health +0x20 heals
   `P->health` (cap = max), max-health +0x20 grows `O->health_max` (HUD max follows; main.c), life +1 updates `P->lives`,
   weapon pickups 0x2014..0x2017 add +0x80 charges (cap 0xff) to `P->ammo[4/1/2/3]`; nothing is collected while dying
   (DAT_10072110); health chime 0xc03b only once per 50 ticks, otherwise coin sound 0xc03f (DAT_1007215c); 0x2012 spawns as
   0x2013 at 9 lives and 0x2002 at max health 0xff (FUN_10012c00). Verified: hp 103 -> 128 on touching a Training health item.
3. **Script interpreters** (FUN_100283e0 / FUN_10028820 compared opcode by opcode): added -0x2e (stop this object's sounds,
   FUN_10007ef6; voices are tagged per object, `sfx_stop_owner`), -0x11 (+0x98), word-script -10 (callback), -7 now marks the
   spawn record gone (was a plain despawn), word -8 only marks gone (it used to kill the object), -0x28 plays tagged to the
   object. Scan of every script of levels 1,3,5,6,7,9,11: the only ops the port still ignores are -0x21 / -0x2a, which the
   original interpreter does not implement either, and -0x2f (not used by any script).
4. **Objects that failed to spawn** (init handler unported, `spawn_type` returned NULL) and now exist (solids.c "AUDIT" block):
   - 0x403f broken-doll chunk burst + pieces (FUN_10042390/10042540): every beaten doll/dummy (anim 0xb) now bursts into
     12 flying chunks (Training lessons 1-5);
   - 0x4a pop-up archery targets + fence 0x49 completion (4 prize pickups from table 0x10066ea0, `DAT_10070008` memory) +
     0x4b prize model + 0x4037 debris (FUN_10041da0/10041f00). Archery range at Training x 21280..22048 was invisible before;
   - Training wheel 0x46 update (FUN_10042090: constant spin with random speed dips, drives its 0x4031 gears' rotation) and its
     creaking child 0x202c (sprite 130..141, random positional creak sounds 0x40c3/0x40c4);
   - Thebes earthquake rocks 0x402a (FUN_10034650/10034750; the earthquake man already tried to spawn them; hurt Hercules);
   - spinning-top pieces 0x403c (FUN_100407b0/100408b0, spinning sprite) and lid debris 0x4026 (FUN_10040c90/10040de0);
   - rope end 0x4033 angle (FUN_1003eaf0 via FUN_1002c090) and lightning bolt 0x4005 spin (FUN_1001a0f0, rot -0x80/tick):
     these set `rot[2]` which aud_render's draw now honours.

## Coverage matrix (types spawned by the maps; all spawn with a handler, `SPAWN_AUDIT`)

TRAINING: 0x25 crowd, 0x2b-0x2d tops, 0x31 Phil, 0x32/0x33/0x35/0x39/0x3e stations, 0x36 dummies, 0x37 cannon, 0x38 birds,
0x3c/0x3d sharks, 0x3f gate, 0x40 pots, 0x41/0x47 bridge, 0x42 pegasus, 0x44/0x45 weights, 0x46 wheel (now updates),
0x49 fence (now with 0x4a/0x4b), 0x4c rock spawner, 0x4e stone stack, 0x2030 poles, pickups 0x2002/13/14/15/18/19/1a-1d/0xb,
checkpoints 0x400a-e, end 0x200a: ported. 0x3b/0x4b morph models (FUN_10028aa0 model-script interpreter, rotation ops
-0x21..-0x26): static model only. 0x48 cannon-variant (FUN_1003f4b0) not in any map. 0x29 flame mesh (10040b00) and 0x34 meter
rope (1003e9b0) unported (not map spawned).
FOREST / THEBES: every map type has a handler (see objects_notes.md); sound ids in object scripts all resolve in the level
sound table (`SND_AUDIT`: only Training's unused thug ids and Nessus' later-loaded bank miss); `SFX_MISS` clean over 4000-frame
runs of Training, Forest, Thebes.
Trigger records (0x1xxx) only stream banks/sounds (FUN_10028290) and the port keeps everything resident: no effect.

## Remaining / not done

- Pickup glow child 0xa007 for 0x2015-0x2017 (brightness 0xa0, additive) and the checkpoint statue's 0x2010 sparkle shower +
  hover/bob (FUN_100139f0: kind 2 sparkle each tick while active, 0/1 on alternate ticks, script 0x100535a8/b0, `FUN_10012a10/12b00`):
  analysed (decompiled in scratchpad/aud_objects/dec) but not ported.
- 0x4b prize morph animation, Titans 0x59 tumbling rock (FUN_10038e30, rot +0x14/tick; titans_ai.c domain), Thebes 0x402b/0x402c
  debris (FUN_10035b20 / 10036030: spawner not found in the decompiled set), 0x2022 / 0x2023-0x2025 shadow children of dolls.
- Positional sound depth is still fixed at 160 (original uses obj+0x10 per object); the +0x68 "enabled" test of positional sounds is not modelled.
- Checkpoint re-touch chime is an earlier addition not in the original (left as is).

## Files changed
src/objects.c (interpreters, pickups, audit hooks), objects.h, objects_int.h, solids.c (AUDIT block), training.c (debug only),
nessus.c (AI id), enemies.c (bolt spin), sfx.c/sfx.h (`sfx_stop_owner`, `sfx_has`, SFX_MISS), game.c (owner tags / stop list),
main.c (HUD max health) + the aud_render merge (render.c/.h, level.c/.h, bosses_models.c).
