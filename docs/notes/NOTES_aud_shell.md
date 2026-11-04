# NOTES_aud_shell: front end, HUD, video, save flow and ALL audio (audit of the port against the decompile)

Tree: `work/aud_shell` (copy of the live tree). New file: `src/scrolls.c/.h`. Test hooks added: `HERC_AUDIO=1` (open the audio device in
`--shot` runs so voice allocation / tracking can be exercised headless, use with `--sfxlog`), `SCROLL_TEST=<id>` (trigger the letter display
at tick 10, ENG1 and ENG3). Scratch scripts: `scratchpad/aud_shell/cov2.py` (static sound-site coverage) and `done.sh` / `done2.sh`
(LEVEL_DONE regression).

## 1. Coverage matrix

### Sound plumbing (ENG1 FUN_10006da0 / FUN_100075e3 / FUN_10007f8e / FUN_10007ef6 / FUN_1000666b, ENG3 FUN_1000bc47 / FUN_1000c48a / FUN_1000ce35)

| Original rule | Before | Now (file:function) |
|---|---|---|
| id & 0x7ff -> PC_N.ESF through the level's SFX.SFX banks | ported | `sfx.c:load_bank` |
| ENG3 levels load their banks (`*(0x1003853c + level*0x18)` in ENG3.DLL, FUN_1001abb0) | **missing: Gauntlet, Cyclops, Torment were completely silent** (table never loaded, every sound hit tone 0 vol 0) | `game.c:game_load_level` -> `sfx.c:sfx_load_level_tbl` |
| flagged id (0xf800) with no emitter object is rejected | ignored | `sfx_play_ex` (owner 0 = no object); `-0x27` script op and `FUN_10006da0(id,0)` sites pass owner 0 |
| 0x4000 sounds need the emitter in this frame's draw list (obj+0x68) | ignored | `SfxPos.hidden` from the projection (`game.c:obj_sfx_pos`, margin 80 px) |
| pan from obj screen x (+0xc), distance from depth (+0x10 = GTE SZ>>2, 160 at the player plane), 0x1000 / 0x2000 attenuation modifiers | depth fixed at 160, x = path x - camx | real projection of the emitter (`obj_sfx_pos`), `sfx.c:pan_levels` |
| flag 0x800 = tracked: starts silent, after 2 ticks follows the emitter (smoothed by half the distance per tick, FUN_1000666b) | played at volume 0 forever (0x8xxx) or frozen at the emit position (0x48xx/0x58xx) | `sfx.c:sfx_tick` (called every game tick), `SfxTrackFn` |
| script op -0x2e = stop every sound of this object (FUN_10007ef6) | no-op | `objects.c:snd_stop_owner` -> `sfx_stop_owner` |
| FUN_10007f8e(id, 0) from boss code | only the player's `-id` path | `objects.c:obj_sound_stop` -> `sfx_stop` |
| 6 voices per id, 24 tracks | ported | `sfx.c:playing_count`, `NTRACK` |
| per-tick sound queue was 16 (objects), 8 (player), 16 (ENG3): excess events silently dropped | dropped | 64 / 48 entries (`objects.h`, `e3world.h`) |
| looping ESF (30 ids) | never stopped when the owner went away | `kill_obj` queues `sfx_stop_owner_loops` (ENG1); `e3_remove` does the same for ENG3; explicit stops below |
| user options (Properties > Sound: sfx on/off, cd music on/off, volume) | **stored in controls.cfg but never applied** | `sfx.c:sfx_set_options` (master = level volume * option >> 8, music gain, speech gain), called every frame from `main.c` |
| speech clips (FUN_1002c4c0 / FUN_1002c4d0 = PC_<1000+n>.ESF) | Training (Phil) and Gauntlet only | + Hydra 0x2a, Medusa 0x26 / 0x27, Hades 0x40..0x42 / 0x6c..0x75 (see 2) |

### Sound sites per context (ENG1; `cov2.py` = literal ids of every FUN_10006da0 / 100075e3 / 10007f8e call in ENG1 + ENG3 against the port source)

Of 120 constant-id sites in ENG1 and 25 in ENG3, 20 were not in the port at the start. Status:

| Site (original) | id | Port |
|---|---|---|
| FUN_10002290 / ENG3 10007680 pause menu | 0x339 open, 0x337 move, 0x338 select | fixed: `main.c` (keyboard and pad) |
| FUN_10012790 scroll icon tick, FUN_10017090 (ENG3) | 0xd / 0x600d | fixed: `scrolls.c` (new letter display) |
| FUN_1001c720 Hydra head turned up / 1 in 0x78 | 0x19e, 0x184 | fixed: `bosses_hydra.inc` (wrong invented 0x183 removed) |
| FUN_100176e0 landing in water (Forest 0x2e1, Training 0x360) | 0x4276 | fixed: `player.c:go_land`; Training water line was 0 in the port -> footsteps in Training used the dry branch; now 0x360 |
| FUN_10012040 invulnerability gift selected | 0x14 after 0x14 ticks | fixed: `player.c` |
| FUN_10015d00 op 0x16 charge released | stop 0x15 | fixed: `player.c` |
| FUN_10019f50 type 0x4005 lightning bolt spawn | 0x4811 (tracked) | fixed: `enemies.c:enemies_player_spawns` |
| FUN_10012ef0 health pickup: 0xc03b, then 0xc03f for 50 ticks | always 0xc03b | fixed: `objects.c:obj_collect` |
| FUN_10021050 / 10021400 / 10022440 Medusa: 0x1c6 loop + stop at death, 0x1c7 stop at every gaze end | 0x1c6 looped for the rest of the level | fixed: `bosses_medusa.inc` |
| FUN_100393b0 Titans lava rise: stop 0x246 | looped forever | fixed: `titans_ai.c` |
| FUN_10044240 Hades taunts (speech state machine DAT_10070240), FUN_10045bf0 gloat after a hit | missing ("XA streams not on the PC build": wrong, PC_1064..1117.ESF are in the VORTEX bank) | fixed: `bosses.c:vortex_level_tick`, `hades_hurt` |
| FUN_1001ae00 Hydra last scream, FUN_10021400 / 10022440 Medusa lines | missing | fixed (speech 0x2a / 0x27 / 0x26) |
| FUN_10009890 (Forest type 0x43, 0x8ce) | plays 0xce / 0x8cf from the rock code of enemies2.c, not the same handler | residual |
| FUN_10034750 / 10035b20 (Thebes 0x402a / 0x402b earthquake man's dust, 0xd150..52, table 0x1005febc) | handler not ported (objects notes already list it) | residual |
| FUN_10042280 (Training 0x40a2 / 0x202c: 0xc3 / 0xc4 random), FUN_1000e620 (Forest 0x4048: 0xd3), FUN_1003fc60 (Training 0x8071), FUN_10037f80 (0x6010) | handlers not ported | residual (objects / enemies agents) |
| every other constant site | present | - |

ENG3: constant sites all present (0x600d done). ENG3 sounds are positioned by lateral offset only (depth fixed at 160, no visibility
gating): the 3-D levels' obj+0x10 depth is not ported. ENG3 `FUN_1000ce35` (stop 0x2c on hurt) was already ported.

### TITLE sounds (FUN_100054f0)

0x300 / 0x301 / 0x302 / 0x303 / 0x326 (menu), 0x318 / 0x319 / 0x31a / 0x31b / 0x31d / 0x31e / 0x325 (tally), 0x430f / 0x4311 / 0x4319 /
0x431f..0x4323 (Phil, Meg, Pegasus, goat) were already ported (`fe_flow.inc`, `fe_pswd.inc`, `frontend.c`). 0x430a / 0x430c belong to
FUN_1000a300 (three muse idle animations on the PSX memory-card screens), which has no caller on PC (dead code). Speech: password hints and
tally ratings ported; FUN_10010fe0 (load/save card screen) unreachable on PC.

### Music

CD-DA track 2 is not on the ISO. Every `.FS` entry was listed: the only audio files are the 559 ESF samples (the longest looping ones are
ambience loops 384 Hydra, 454 Medusa, 704 Vortex, all already played by the level code) and 8 s fanfares in the OPTIONS / TALLY banks; there
is no stand-in music. `sfx.c:load_music` degrades to one `music: ... not available` line and `music_set` stays empty; an optional
`Track02.wav` next to HERCULES.FS still works and now obeys the cd-music / volume options.

### Front end / HUD / video / save

| Piece | Status |
|---|---|
| boot videos, menu, passwords, properties, cards, tally, credits, cheats, game over / continue, attract mode | ported earlier (polish); all `--fe-start` entries (0..9, 10..20) run headless without error |
| autosave at every level start (`fe_flow.inc:flow_run 0x29` -> savegame.dat), CONTINUE row (default selection when a save exists), F3, `seen_intro` skip | verified end to end with `HOME=<scratch>`: no save -> START GAME writes `0x10103` (level 1, difficulty 1, lives 3); save level 5 / difficulty 2 / lives 4 -> menu starts on CONTINUE, X -> `start_level 5`; `seen_intro` present -> boot goes straight to the menu with CONTINUE selected |
| HUD (health bar, lives / gift orbs, charge meters, boss bar at (0xd0,0xd0), time-trial digits) | ported (`hud.c`); the boss bar *is* ported (the task text said it was not) |
| HERCULES letter / scroll display on pickup (FUN_10012620 + 10012790 + 10012960, ENG3 10016f30 + 10017090 + 10017130) | **missing in both engines**: the eight letters slide down, the new one animates, sound 0xd per icon; ported in `scrolls.c` (verified by screenshot on Thebes and Cyclops) |
| pause menu | text ported; sounds were missing: fixed |
| video | ETV in-engine, which video when: unchanged; videos 5 / 23 / 24 / 26 do not exist on the PC disc |

## 2. What was fixed, with evidence

* **ENG3 silence**: `--level CYCLOPS --sfxlog` printed `sfx id=0x6204 ... bank=0 tone=0 vol=0 gain=0.000/0.000 len=0.00s` for every event and no
  `sfx: level N` line; now `sfx: level 8: 2 banks, 62 samples ...`, `bank=2 tone=4 vol=90 gain=0.050/0.253 len=1.56s`.
* **Object sounds**: per-object ownership (slot + 2) so positional sounds use the object's own screen x / depth, are gated by visibility,
  track movers (`sfx track id=0x58c9 owner=12 sx=117 depth=224`) and die with the object when they loop (Thebes 0x5942 snow loop).
* **Letter display**: screenshots `scratchpad/aud_shell/sc40.png`, `sc3.png`.
* **Speech**: `--level VORTEX` log shows `speech 108 / 115 / 117 / 65 / 110 ...` (Hades' lines), taunts only when no clip is playing, once-only
  victory / defeat lines.
* **Regression**: all 10 levels, 600 frames, rc=0; `LEVEL_DONE_LOG`: Cyclops t=10495, Gauntlet t=7335, Titans t=7867, Hydra (BOSS_WEAK) t=569, all
  identical to the unmodified tree (`work/merge`); Torment does not finish with these plain test parameters in either tree.

## 3. Residual gaps and why

* Object handlers whose sounds are unported because the handler itself is (listed above); need the objects / enemies agents.
* ENG3 depth attenuation / visibility (needs the 3-D projection of obj+0xc / +0x10, only lateral x is used).
* When all 24 voices are busy the port steals the voice nearest its end; the original's allocator drops the new sound (unknown policy, EXE
  FUN_0040bf20 not decompiled).
* The ESF `8-bit buffer` flag (the PC build kept 8-bit DirectSound buffers for some samples) is ignored (16-bit playback).
* Speech volume is a constant 0.9 x the user option (EXE FUN_00401dd0 is not decompiled).
* The menu still draws the "build <date>" stamp (kept from the polish pass).
