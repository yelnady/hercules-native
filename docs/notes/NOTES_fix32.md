# fix32: Training station 5 (carousel) prize girl cannot be touched

Work tree: `work/fix32` (start state = `work/base32`). Edits marked `FIX32`.
Originals used: `FUN_10040e50` (meter update), `FUN_10041000`, `FUN_1002a710` / `1002a880` / `1002a7a0` / `10030e40` (touch test and box
fetch), `FUN_1003d310` (0x39 station init), `FUN_1003d800` (0x35 init), `FUN_1003d440` (0x39 update, also disassembled from ENG1.DLL with
capstone at 0x1003d640..0x1003d691).

## Real cause

The touch test is correct in the port and in the original: `FUN_1002a710(player box +0x88, obj)` compares the player's first frame box with the
object's first frame box in all three axes (z = frame bytes >> 1, same as `frame_box` / `obj_box`). Hercules is on plane z 128 (box z 118..138) and
there is no plane doorway (attr 0x10/0x20) anywhere near station 5, so he can never leave z 128 there. The meter box is only +-5 in z.

The carousel station object 0x39 sits at z 192 and its child list gives the meter dz 0, so the port spawned the meter at z 192 (gap 49). The original
init `FUN_1003d310` has an extra step the port lacked:

    if (meter type 0x2f) { if (station z != 0x800000) meter z = 0x800000; ... }

i.e. the meter is moved onto Hercules' plane (z 128). The same applies to the boxes station: `FUN_1003d800` (0x35) puts a target that is already
beaten (restored from the station bit mask when the station respawns) on z 0x800000.

## Changes

- `src/solids.c` `station_kids`: 0x39 -> meter z = 0x800000 when the station z differs; 0x35 -> restored beaten 0x2a target z = 0x800000.
  `KID_LOG=1` env prints every spawned child with its list offsets (verification only).
- `src/training.c` `station_tick` (carousel riders): the riders' z offset used `sin * 0x3c`; the original (`1003d440`, disassembly
  `lea ecx,[ecx*3]; lea ecx,[ecx*5]; shl ecx,4` for both x and z) uses `* 0xf0` for z as well (circle radius 128 around the hub, hubs at z 192 and
  64, so the riders cross Hercules' plane z 128 from both sides). With 0x3c they stayed at z 162..222 / 34..94, never reaching Hercules' plane.
- `src/training.c` debug only: meter log line also prints the player's body box; `PRIZE_LOG=1` prints the moment a prize is touched.

## Meters of all stations (only five exist: 0x33, 0x32, 0x35, 0x3e, 0x39 each have one 0x2f child; no other station / spawn record has any)

| station | parent (x, z) | meter x / z | final y | box y | reached |
|---|---|---|---|---|---|
| 1 dolls 0x33 | 1726, 256 (dz -128) | 2022 / 128 | 752 | 757..799 | walking |
| 2 stomp stage 0x32 | 2752, 132 | 2980 / 132 | 741 | 746..788 | walking |
| 3 boxes 0x35 | 3492, 128 | 3492 / 128 | 696 | 701..743 | needs a jump (standing head top is y 748, 5 px gap; data is the original's d2 = 0x60) |
| 4 pendulum 0x3e | 5888, 132 | 6116 / 132 | 719 | 724..766 | walking |
| 5 carousel 0x39 | 6840, 192 | 7000 / 192 -> **128** | 717 | 722..764 | walking (was impossible) |

Drawing order: with z 128 the girl is drawn in front of the poles (checked by screenshot, crop of the ready meter next to Hercules); at z 192 she was
behind them.

## Verified headless (`--level TRAINING`)

- Station 5, `TRAINING_BEAT=1 TRAINING_BEAT_ST=5`, script N*900 then R (no TRAINING_PRIZE hook): `PRIZE touched: meter x 7000 y 717 z 128 player x 6982 y 800 z 128`;
  before the fix, meter z 192 and no touch (box z 187..197 vs 118..138).
- Same for stations 1, 2, 4 by walking (touch logged), station 3 only with a jump (`r`).
- Whole sequence with `TRAINING_BEAT=1` only (no TRAINING_PRIZE) and the player running right with an occasional jump: all five prizes are touched
  (`PRIZE touched` x 5) and the lesson advances to 6 and 7.
- Station 3 respawn (teleport away and back after beating): the beaten boxes are on z 128, v2a 9.
- Carousel riders now pass through z 128 (z -56..319 over time), hurt Hercules (`hits` rises) and can be beaten with real punches (4 of 4 of the first hub
  went to v2a 9, hp < 0).
- Untouched: only `station_kids` (solids.c), the rider z factor and log lines (training.c) changed; grass order, hit boxes, doll trigger, checkpoint chime
  and the fix31 crank / stomp / box code are not modified.

## Not verified / notes

- Nothing was run with the real audio / window, only the dummy drivers; the carousel is now noticeably harder (riders actually reach the player);
  that follows the original code.
- The meter's rope child 0x34 is still not spawned (unchanged from fix31).
