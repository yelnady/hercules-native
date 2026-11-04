# Training dolls / sacks: hit reactions (work/dolls)

Root causes (all verified against ENG1 decompiles, FUN_1003e160/e220/e3d0/e580/e5d0/e6d0/e770, FUN_100283e0):
1. objects.c run_pairs ignored script ops that spawn/own things: -0x1f / -5 (FUN_100300c0 / FUN_10030180: spawn child type arg
   at the object), -0x20 (+0x4c), -0x2b/-0x2c/-0x2d (+0xa8/+0xb8/+0xc8 = arg<<8). Sack anim 0xb (0x2a/0x30/0x36/0x3a/0x4d,
   bank 31) spawns 0x403f (shards) + 0x2031 (flour burst) with -0x1f, so the sack just vanished silently. Also Phil's -0x20 (crank speed).
   run_words now executes -0x1f too (0x4005 lightning -> 0x2003, Thebes 0x45 -> 0x202a). SCRIPT_SCAN=1 lists every script using these ops:
   across the whole game only the Training targets, Phil, 0x4005, 0x45 use them (other enemies spawn their debris in C).
2. enemies.c dummy 0x36 (the falling sack): FUN_1003e6d0/e770 was ported without physics. Real behaviour: init hop velocities
   (b95=0xff, v[4]=vymax>>4, v[7]=rnd+0x8000); when Hercules is within 0x20 in x and z, anim 0x17 (v2a=1) releases it, it falls
   (FUN_1002bc50 with the box bottom as the probe point), on landing sound 0x4040 + anim 0xb (burst) + v2a 9, hurts the player
   (level 1, hit counter) while falling. Now ported (enemies3_phys exported from training.c).
3. took_hit() (enemies.c) replaced any box whose bottom is below the object origin by a default box above the origin ("dormant
   frames"); the hanging targets/dummies have their body box BELOW the rope origin, so swords at body height never hit. Exempted
   ai >= 300 (AI3_*) and AI_DUMMY (one-line change).
Test hooks: SHOT_SEQ=prefix (+ --every N) writes BMP per N frames; TRAINING_BEAT=1|2 (+TRAINING_BEAT_AT=frame); DUMMY_BEAT=n,
DUMMY_LOG, DOLL_LOG, OP_LOG, SCRIPT_SCAN, SPAWN_LOG, ANIM_DUMP. game_place_player (--x) now keeps the level start plane z.
Evidence: sword hit on target 0x30 -> flinch, burst of shards+flour, head left on rope; dummy 0x36 at x4135 falls y489->733, bursts,
rises (y per frame logged). Not changed: coin y offset of dummy drop (original y+0x400000, port y-0x20).
