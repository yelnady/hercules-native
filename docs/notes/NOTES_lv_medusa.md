# lv_medusa: MEDUSA level audit (edits tagged LV_MEDUSA)

MEDUSA has 0 map spawn records (the original's runtime template table is empty too); every object comes from `medusa_start` (FUN_10021050) and the
boss script: Medusa 0x2e / dying 0x31, gaze rig 0x2f (6 x 0x2032), columns 0x30, shields 0x2d, floor pool 0x202d, brazier flames 0x2029, rocks 0x34
(+ shadow 0x202f), snakes 0x33 (+ trail 0x202c), skeleton warriors 0x32 (+ bones 0x2030), smoke 0x202a, sparkle 0x202b, roar ring 0x2033,
head pieces 0x35. All compared against /tmp/bosses_w/dec (FUN_10021320, 10021400, 10021aa0, 10021d10, 10022280, 10022440, 100220c0, 10022dd0,
10023120, 10023460, 10023a50, 10023b30, 10022c20 ...).

## Found and fixed
1. `bosses.c boss_draw`: the FX2 rule `if (o->ai == R_MEDUSA_GAZE) return;` (meant for the gaze rig parts) skipped EVERY object with that role:
   smoke/rubble, sparkle dust, brazier flames, snakes and their trails, bones, head pieces, roar ring and the floor pool 0x202d. So snakes were
   invisible (but hurt), the ring-centre floor and the flames never showed. Now only type 0x2032 is skipped.
2. `bosses_medusa.inc medusa_tick`: FUN_10021320 counts DAT_1006fde0 (set to 3 by rock and shield impacts) down and shakes the camera 0xa0000 while
   > 0. The port stored the 3 in `M.shake` and then overwrote it with the quake value every tick, so impact shakes never happened. Now a real counter.
3. `medusa_tick`: the timers M.t / M.frame (DAT_1006fe2c / fe30) were decremented by both Medusa 0x2e and the dying Medusa 0x31 (twice per tick);
   original only FUN_10021320 does. The death sequence ran at double speed (level end now t=1027 instead of 947 in the BOSS_WEAK run).
4. Skeleton kill: the 1-in-8 Hercules speech line 0x27..0x29 (no repeat) is now played (FUN_10023b30).
5. Rubble (kind 2 smoke, death explosion): clatter sounds 0xd1e7..0xd1e9 every 16th puff (FUN_10022c20).
6. `MED_LOG=1` test log: attack pattern picks, gaze start/end, snake positions.

## Verified headless (--level MEDUSA, dummy drivers)
- Normal play: BOSS_GOD, screenshots of idle, rock rain (rocks, shadow, smoke visible), gaze beams, snake attack (snakes + trails visible),
  skeletons (rise, attack, die from SSSS / PPPP script, `skels` count falls), flames and floor pool now drawn.
- BOSS_TELEPORT=160,736,76 (standing at column 0): first gaze breaks the column (state 0 -> 8 sequence), second gaze is reflected off the fallen
  shield, Medusa loses a life (hp 7/8, bar damage).
- BOSS_WEAK=1: one reflection -> dying Medusa 0x31 -> petrify, head pieces, rubble -> `LEVEL DONE ... next=CYCLOPS` (LEVEL_DONE_LOG).
- HYDRA smoke run unchanged (no crash).

## Not verified / remains
- Sound loops checked only by code reading (0x1c6 stop at death, 0x1c7 stops) - no audio output in dummy mode.
- The brazier-flame visibility rule (hidden within +-0x400 of the player) follows FUN_10021780 literally; looked right in screenshots.
- Skeleton body looks pale/purple translucent in screenshots; the original init only makes the overlay child semi (0x4000605), not checked pixel-wise.
- Gaze is still drawn through the FX2 additive tube approximation (no vertex-sharing mesh).
