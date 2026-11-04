# Player combat: reverse-engineering notes (ENG1.DLL)

Ported in `work/combat/src/player.c`. Addresses are ENG1 virtual addresses.

## Buttons
ENG1 copies its default masks from `DAT_1004d958` into the gamestruct at +0x6c..+0x76. The table holds u16 `8000 4000 2000 1000 0400 0800`:

| gs offset | action | PSX bit |
|---|---|---|
| +0x6c | **sword** (with a Gift selected: fire it) | 0x8000 SQUARE |
| +0x6e | jump | 0x4000 CROSS |
| +0x70 | **punch** (combo; crouch + punch = uppercut) | 0x2000 CIRCLE |
| +0x72 | **power punch** (hold to charge) | 0x1000 TRIANGLE |
| +0x74 | previous weapon | 0x0400 L1 |
| +0x76 | next weapon | 0x0800 R1 |

- Directions: 0x10 up, 0x20 right, 0x40 down, 0x80 left. While the 0x8000 cheat is on, `gs+0x5d & 3` (L2/R2) triggers state 0x1b (free fly/debug).
- My earlier labels were wrong: "+0x6c punch, +0x70 sword" was a guess. The sprite frames show +0x6c is the sword (frames 186–191 with slash overlay 477–480) and +0x70 the punch (frames 32–71).

## State table (FUN_10015090 fills DAT_10072040)

| state | handler | meaning |
|---|---|---|
| 0 | 10016840 | idle |
| 1 | 10016b40 | walk |
| 2/4/5/0x13/0x17 | 100171b0 | rising air states |
| 3 | 10016da0 | skid |
| 6 | 10016f40 | turn |
| 7 | 10017400 | fall |
| 8 | 100177a0 | land |
| 9 | 100178f0 | **pole swing** |
| 0xa | 10019440 | (timer state) |
| 0xb | 10017a90 | crouch |
| 0xc | 10018240 | **sword** |
| 0xd | 10019260 | (follow DAT_10072118: Forest special) |
| 0xe | 10017cd0 | **crouch attack / uppercut** |
| 0x10 | 10018a10 | hang |
| 0x11 | 10018ed0 | **hurt** |
| 0x12 | 100193f0 | **ground-pound landing** |
| 0x14 | 10018ba0 | **lift object** |
| 0x16 | 10019140 | **dying** |
| 0x18 | 10019290 | **ground pound (M.A.G.S.)** |
| 0x19 | 100191e0 | **victory (HC_DUNIT)** |
| 0x1b | 10019480 | fly cheat |
| 0x1c | 10017eb0 | depth-plane switch |
| 0x1d | 10019c20 | (frame 0) |
| 0x28 | 100379e0 | Pegasus/riding level states |
| 0x29 | 10017c30 | **punch combo** |
| 0x2a | 10017c70 | **power punch** |
| 0x2b | 100182c0 | **fireball sword** |
| 0x2c | 10018550 | **lightning sword** |
| 0x2d | 10018770 | **sonic sword** |
| 0x2e | 10018830 | **sword recovery / combo window** |
| 0x2f | 10018c30 | **throw** |

Entry checks:
- FUN_10017ff0: sword / weapons (held button; DAT_1007211c locks the plain sword until release).
- FUN_10017ba0: punch / power punch.
- FUN_10017290: air sword / air punch. Needs the jump-script window flag DAT_1006ef7c, set by op 0x10.
- FUN_10017a20: crouch (lock DAT_1006ef80).
- FUN_10018b10: pick up.
- FUN_10018bb0: throw.
- FUN_10017810: grab a pole with frame box 3.
- FUN_10019d70 (vertical): at the apex of a standing jump with down held, enters state 0x18 (ground pound: gravity 1.0, max 32).

## Script ops added (FUN_10015d00)

| op | effect |
|---|---|
| 0x03 | carried-object pose (DAT_1006ef98) |
| 0x04 / 0x05 | pole-swing direction scripts |
| 0x06 / 0x07 / 0x1d | sword-trail overlay. This is a child object at obj+0x60, bank 1, frame = arg. Op 0x07: additive, visible 5 ticks. Op 0x1d: opaque, 2 ticks. Parent flag 0x54 bit 23 = shown. |
| 0x08 | hurt animation becomes cancellable |
| 0x0b / 0x0c | sonic objects (2 × type 0x4006) |
| 0x0d | lightning continues while the sword is held |
| 0x0e | screen shake (DAT_100551e8, 16.16) |
| 0x11 | punch finish |
| 0x12 | after a crouch attack |
| 0x13 | crouch: punch → state 0xe. **Bug fixed:** it tests the punch mask and switches state when held. |
| 0x14 / 0x15 | sword release → state 0x2e |
| 0x16 | power-punch charge (+16 per op, max 0xff; at max plays sound 0x4029, the bell); on release → 0x10054018 |
| 0x18 | frame = DAT_1007216a (aimed pose) |
| 0x19 | loop while the sword is held |
| 0x1a / 0x1b | stop / start the fireball stream object (DAT_10072020, type 0x4001) |
| 0x1c | x −= 16 in the facing direction |
| 0x1e–0x21 | flags54 bit ops |
| 0x22 / 0x23 | spawn effect object `arg` (e.g. 0x4005 lightning bolt) |
| 0x29 | death done → restart / continue |
| 0x01 / 0x2a / 0x2b | level-flow signals (fade, level done) |

## Hit detection
Per-frame collision boxes come from the sprite bank.
- Frame record: `f = (entry >> 8) + 4`, with `d[f+1]` = box count, then 6 × s8 per box: `x0, y0, z0*2, x1, y1, z1*2`. This is FUN_10031080 / FUN_10030e40; FUN_1002a880 mirrors the box when `flags54 & 0x10`.
- Box 1 is the body (stored at player+0x88). Box 2 is the attack box (DAT_10071440, valid flag DAT_1006fec4; set by FUN_10025d00 each frame). Box 3 is the grab hand (poles; also present in jump frames).
- An enemy is hit when its box overlaps box 2 (FUN_10026de0 returns 0x80) and `hitflags & 0x5f & ~consumed` is non-zero. It then calls FUN_10019b40(mask) for the damage.
  - Hitflags: 1 crouch, 2 power, 4 punch, 8 sword, 0x10 air sword, 0x20 stomp, 0x40 air punch.
  - Damage tables per difficulty (gs+0xe): 0x100553e8, 0x100553f8, 0x10055408, 0x10055418, 0x10055428 are all {5, 4, 3}.
  - The power punch deals the charge clamped to [5, 15] (DAT_10055438).
  - A hit marks DAT_10072168, which is cleared from hitflags at the next tick.
- Stomp (0x20, ground-pound landing) has no box. Enemies test `hitflags & 0x20` themselves (FUN_10024320, gated by DAT_10072188).
- **Power punch smashing obstacles:** FUN_1000a010 kills a breakable instantly when `hitflags & 2` and the HUD-smoothed charge (DAT_1007082c >> 16, which follows DAT_10072194) is above 200. The state 0x2a handler drains the charge by 0x3c per tick while the blow is live. Breakables should therefore test `P->charge` at the moment of the hit; a full charge (0xff) passes the check.

## Taking damage
- Enemies call FUN_100266a0(enemy, level, kind). This keeps the highest request in DAT_1006feb4 (level + 1; 0x10 = no reaction, 0x20 = strong). Direction comes from FUN_1002ab30 into DAT_1006feb8, the kind into DAT_1006febc.
- FUN_10018c50 applies the request each tick (from FUN_10015780):
  - Ignored while obj+0xec (invulnerability) is non-zero or weapon 4 is active. The request stays pending.
  - Reaction scripts: 100546d8 (front), 10054718 (strong, if room behind per FUN_100196f0), 10054750 (from behind), 10054798 (in the air).
  - Health is gs+0x24 and starts at 128. Damage taken comes from 0x10055348[difficulty*8 + level−1]: easy 5..22, medium 12..50, hard 15..60.
  - Invulnerability: 0xec = 0x3c, decremented twice per tick. Flash levels come from 0x100552d0 (additive, then mode 0); the 0 terminator ends it after about 28 ticks.
- Health 0 sets DAT_10072110 (dying). The next go_idle calls FUN_10018f60:
  - Decrements lives (gs+0xc); game over when it reaches 0.
  - Probes walls at ±16..64 to choose the death script: 100549f0 (both sides), 10054aa0 or 10054930 (bank 4 HC_DIE).
  - The script ends with op 0x2a (fade) and op 0x29 (restart).

## Gifts of the Gods (DAT_1006efb4)
- Charges live at gs+0x24+2*w, and the HUD shows them.
  - The reload table 0x10055320 = {10, 0, 0, 1, 3}: one charge is spent each time DAT_10072140[w] wraps. That is every tick for the fireball, every 2 ticks for the sonic sword (while its objects live), and every 4 ticks for weapon 4.
  - Weapon select (FUN_10012040) skips weapons with 0 charges and plays sound 0x27.
- **Fireball (1, state 0x2b):** script 0x10054260. op 0x1b starts the stream object. The pad aims it through 0x10054c78 and the 20-entry table 0x10054c98 {angle, pose frame, dx, dy, shot angle}, one step per tick. The pose animates over 5 frames.
- **Lightning (2, 0x2c):** script 0x100542a0. The pad aims through the 6-entry table 0x10054d60. When the aim is reached, the strike script is chosen by pose frame (0xeb → 10054318, 0xf5 → 10054380, 0xfa → 100542c8). op 0x23 spawns 0x4005 (the bolt).
- **Sonic (3, 0x2d):** script 0x100543d0. op 0x0c spawns two 0x4006 objects that orbit using scripts 100554a8 / 100554c0.
- **Weapon 4:** invulnerability with flicker from 0x100552a8.

## Carrying
DAT_1007210c is the dynamic object a forward free-space test hit (FUN_100197b0 etc., via DAT_1006fedc).
- Idle with that object of type 2, 4 or 5 → state 0x14 lift (script 100544c0, bank 3 HC_THROW). DAT_1007201c = type.
- While carrying:
  - Walk script 10053b00 (half speed), stop script 10053bf0, idle script 10053a10.
  - No jumping or attacking.
  - The sword button throws (10054558), or with up held throws upward (100545e8, not for type 4).
- op 0x03 drives the held object's pose; the object code reads tables 0x10052c0e / 0x10052e76.

## Not ported (needs the object system)
- Projectile and effect objects:
  - 0x4001 fireball stream: the player publishes `P->flame` (position, angle) each tick.
  - 0x4005 / 0x4006: published through `P->spawns`.
- Picking which object is touched, and moving the held or thrown object. The player publishes `touch_*` (input) and `carried`, `carry_pose`, `throw_event`.
- Poles: register them in `P->poles` / `P->npoles`.
- Ground-pound landing on an object of type 8: should use 0x10053e80.
- Ops 0x00 (footstep variants), 0x02, 0x24.
- The depth-plane switch (state 0x1c).
- The Forest special (state 0xd).
- Pegasus states 0x28 / 0x1b.
- Stop-sound requests (FUN_10007f8e 0x15 when the power punch is released; 0x26 when the flame stops).
