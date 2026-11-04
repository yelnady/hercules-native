# lv_vortex audit (VORTEX / Hades, level 11)

No source changes (no LV_VORTEX edits). Tree identical to base34.

## Findings
- VORTEX has 0 spawn records (`objects: 0 spawn records`); everything is spawned by the level hook FUN_10044120 / bosses.c `hades_start`.
  Types in play: 0x28 Hades, 0x2029 aura, 0x29 meteor, 0x202a flame, 0x2a fireball, 0x202b souls (x30), 0x202c/0x202d fx, drops.
- Decompile coverage: FUN_100447c0 (Hades update), 10045a80 (meteor), 10045ef0 (fireball), 10046320 (soul tick) are NOT in analysis/ENG1/functions,
  so those handlers could only be checked against objects_notes.md, not line by line. 10045bf0 (hurt gate 1/8, taunts), 10045cc0 (flame init),
  10045d50, 10045780 (death path), 10046230 (soul init) match the port.

## Verified headless (--level VORTEX)
- BOSS_GOD+BOSS_WEAK, 3000 frames: Hades takes hits (hp 200 -> 40 -> 0), hurt anim, drops, death swoop path, 300-tick death timer,
  `end=1 won=1` (level completes).
- Without hooks: player punches hit Hades (hp 200->196), Hades' flames/meteors/fireballs damage Hercules (HIT_LOG levels 3/0x11/0x12), player can die.
- --sfxlog 3000 frames: no looping samples (all loop=0), no SFX_MISS, fireball 0x4ac9 / death 0xc2ca / meteor 0xcac1 / flame 0x4ac2-4 / roar 0x4acb all fire.
- Fireballs reset fire_active on death; souls are killed after Hades dies.

## Not verified / remains
- Hades update, meteor and fireball logic not comparable to a decompile (missing from the analysis dir); hair-flame palette (-4) and
  the camera depth offset are as documented in objects_notes.md.
- Real audio and window not run; screenshot at one frame only (Hades was off screen there).
