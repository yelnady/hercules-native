# Front end, HUD and input: reverse-engineering notes (native port)

The code is in `work/frontend/src`: `frontend.c/h`, `video.c/h`, `hud.c/h`, `input.c/h`, `font5x7.h` and a rewritten `main.c`. `game.c/h` have small edits marked `FRONTEND:`.

## Startup flow (TITLE.DLL)

`PC_DLLEngineMain` (0x1000c1f0) calls the main loop FUN_10005fb0. That loop runs cooperative processes created with FUN_10005db0(proc, …). Each process keeps its state in `param_1[1]` and its wait-frame count in `param_1[2]`.

The top-level process is **FUN_100164b0**. At power-on it starts in state 0xffff with gamestruct[3]==0x11, then runs these states:

| State | Action |
|---|---|
| 0x33 → 0x26 → 0x27 | video 1, Disney Interactive logo |
| 0x25 | video 2, film countdown |
| 0x24 | video 3, "Disney's Hercules" title |
| 0xe7 → 0xe6 | main menu, process FUN_10007510 |

- FUN_10009980(n, flags) plays videos. Its result bit 8 means "skipped". Skipping video 1 or 2 jumps to video 3.
- The video table is at 0x100251e8 (16-byte entries). Entry *n* holds the PSX name `\MV\Mn` and corresponds to the PC file `e:\data\n.etv`.
- Video 0x1a (26) is only played on re-entry and doesn't exist on the PC CD.

**New game.** gamestruct[0] = 0, then state 0x28 → 0x29, which increments the level and goes to 100. State 100 plays video 0x12 (18.ETV, the Fates/Hades story), then card process FUN_10014300, then 0xa0, which launches `\SRC1\ENGINE\ENGINE1` with the level number.

**Level numbers** (TITLE string table):

| No. | Level | No. | Level |
|---|---|---|---|
| 0 | PLAYROOM | 6 | HYDRA |
| 1 | TRAINING GROUND | 7 | MEDUSA |
| 2 | TRAINING GAUNTLET | 8 | CYCLOPS |
| 3 | FOREST OF CENTAURS | 9 | TITANS |
| 4 | NESSUS | 10 | TORMENT |
| 5 | THEBES | 11 | VORTEX |

Number 12 is the ending, which differs by difficulty: credits videos, or ENDEASY.

## Main menu (FUN_10007510)

The main menu is assembled from objects (FUN_10006bb0 creates them, FUN_10006fb0 places them) on a 320x256 screen whose origin is the centre (160,128).

**Backdrop.** MENUB.TGA (the blue Disney Interactive frieze) is shown first.

**Board.** It cycles T015 → T013 → T003 → T001 → T012 → T010 using two VRAM slots. Each image holds for 60 ticks, then fades in from black over 8 ticks (FUN_100071e0 / FUN_10002090; the semi flag is ignored there).

**Carry-in (FUN_10008320).** Hercules runs in from x = -0x15e at 3 px/tick. The whole board, logo, menu text and muses are offset by his position (DAT_1002a044/a03c). He then lifts, drops (white additive flash, DAT_1002a020), the board shakes, and he falls away. Pressing Start/Cross skips the sequence. Tables:

| Address | Contents |
|---|---|
| 0x10024230 | run frames (+0x16) |
| 0x100241a4 | per-frame y |
| 0x10024278 | lift frames (+7) |
| 0x100241e2 / 0x10024202 | per-frame x / y |
| 0x100242b8 | drop (+0x26) |
| 0x100232b0 | fall loop (+0x26) |

**Muses.** Five muses, each with an animation table:

| Table | Frame base | Position |
|---|---|---|
| 0x10023af8 | 0x3c | (-0x50, 0x69) |
| 0x10023668 | 0x5b | (0x6e, 0x5f) |
| 0x100238b0 | 0x7c | (-0x7d, 0x55) |
| 0x10023d40 | 0xaf | (0x87, 0x4b) |
| 0x10023f88 | 0xce | (0x50, 0x69) |

- The animations loop from index 0x17.
- Each muse has a shadow: the same frame drawn 12 px higher, brightness 0, 50% blend, rotated by the +0x74 angle.
- That angle is 0x400 = 360°; the sine table is at 0x100243e0. Shadow rotations: 0x70, -0x70, 0x80, -0x60, -0x80.

**Sparkles.** Bank frames 0xf8–0xfa, spawned at 7% chance per tick at points from 0x100242e8. They fade by 8 per tick and use additive blending.

**Menu items.** The logo is frame 1; the menu text is frames 2–5, one image per selection. Both draw additive (flags |5), each with a subtractive copy at (+2,+2) (flags |6). Object flag bits:

| Bits of +0x54 | Meaning |
|---|---|
| 0–1 | ABR blend mode |
| 2 | semi-transparent |
| 4 | mirror |
| 31 | visible |

Draw order: the earliest-created object is in front.

**Selection** (a030 = 0/1/3). Text shown: START GAME, PASSWORDS, EXIT, plus "Alt+Enter toggles fullscreen / F1 for help F2 for options F3 for load game".

| Item | Action |
|---|---|
| 0 Start Game | new game |
| 1 Passwords | password process FUN_1000f460 |
| 3 Exit | FUN_1000c950 |
| 4 Cheats | only when gamestruct+0x30 bit 31 is set |

After 900 idle ticks the original starts the attract demo; that isn't ported. Menu sound ids are 0x300 move, 0x301 up, 0x302 select, 0x303 start and 0x326 board landing; they aren't played yet.

## Password screen

The screen is FUN_1000f460. The cursor is Meg (object type 0x2005, handler FUN_1000d910).

- **Background.** PSWD.TGA.
- **Vases.** Four, at x = -0x55, -0x12, 0x2d, 0x6d and y = -2. Frame ranges are 0x1e8–0x23f, 0x126–0x17d, 0x187–0x1de and 0xc5–0x11c. Every vase starts at offset 24.
- **Password table** (0x10025a30). 42 entries of {v0, v1, v2, v3 offsets from bases 0x1e3/0x121/0x182/0xc0, code, difficulty}. Codes 100–105 are paired cheat entries (they set flags).
- **Controls.** Left/right walk between vases, up/down (or Circle/Square) rotate one, Start/Cross checks, Triangle exits.
- **Success.** PSWD\LEV<code>.TGA is shown with the vases glowing additively at 0xff, then the code's card plays.
- **Meg's position.** She stands beside the previous pillar facing the selected vase. Facing right she is at MUSE_RX = {-, -0x4d, -0xc, 0x35}; facing left at {-0x1e, 0x23, 0x63}.
- **Simplifications.** Her walk and turn animations are reduced to snapping into position. Each vase rotates to the next symbol stop; the stops come from the password table.

## Pre-level cards (FUN_10014300 family)

All cards use the muse table 0x10025f50. The text fades in when the table value reaches 10, and the card fades out at the end.

| Level | Background | Bank | Text pos | Muse pos | Mirror |
|---|---|---|---|---|---|
| 1 | SEQ5 | seq5 | (0, -16) | (-100, 100) | no |
| 2 | SEQ6 | seq6 | (0, -16) | (-100, 100) | no |
| 3 | SEQ4 | seq4 | (0, 0) | (100, 40) | yes |
| 5 | SEQ1 | seq1 | (0, 0) | (100, 100) | yes |
| 6 | SEQ9 | seq9 | (-8, -24) | (100, 100) | yes |
| 7 | SEQ2 | seq2 | (-10, -24) | (100, 100) | yes |
| 8 | SEQ3 | seq3 | (0, 0) | (-100, 100) | no |
| 9 | SEQ7 | seq7 | (0, 0) | (-100, 100) | no |
| 10 | SEQ8 | seq8 | (0, 0) | (-100, 40) | no |
| 11 | SEQ10 | seq10 | (0, 0) | (-100, 40) | no |

Levels 7 and 11 use text frame 0x10 and muse base 0 instead of text frame 1 and muse base 1.

## ETV video (video.c)

`video.c` is a C port of tools/etv.py: RNC method-2 unpacking with a CRC check, then the 4x4 block codec with its jitter blend pass. Frames run at 15 fps; the 22 kHz mono soundtrack is decoded up front and played through the existing mixer.

The engine searches for `<n>.ETV` in this order:
1. `$HERCULES_ETV_DIR`
2. `<fsdir>`
3. `<fsdir>/DATA`
4. `<fsdir>/../Hercules_AG_extracted/DATA`

The single-bit repair in etv.py is not ported. A frame that fails its CRC keeps the previous picture; this affects one frame in 11.ETV and one in 15.ETV.

## Properties sheet (HERCULES.EXE COptionsSheet resources)

The original sheet has four pages:

| Page | Controls |
|---|---|
| keys | up, down, left, right, sword, jump, punch, power punch, weapon, "reset keys", "key1on" |
| gamepad | sword, jump, punch, power punch, weapon, pause; "Hold SPACE to clear a button selection." |
| sound | sfxtog, cdtog, volume |
| game play | difficulty, lives, restart level (training … vortex) |

The game play page is "only available during the Title's main menu". The original's level select is that page's restart-level list, plus passwords.

The native version is drawn Win95-style with a 5x7 font. It is opened with F2 and saved to `~/Library/Application Support/Hercules/controls.cfg`.

## HUD and pause (ENG1.DLL)

- **Art.** `LANGUAGE\SRCLANG\STPANEL1.DAT` (256x512, 8-bit) is the status-panel page. FUN_10032b10 loads it to VRAM (0,512). It holds the health clouds, the bar, the gift orbs (24x24 grid from row 0x38) and the pause font.
- **Health bar.** Drawn by FUN_10032bd0 with FUN_10033620(x, y, u, v, w, h, clut).
  - bar length = clamp(max*0x92 >> 8, 0x1c, 0xff); fill = health*0x92 >> 8.
  - gift icons: (col,row) table at 0x1005dd80.
  - It also draws a boss bar at (0xd0,0xd0) when DAT_10070834 is set; that isn't ported.
- **Pause text.** FUN_10001f40 / FUN_10002450 draw PAUSED, CONTINUE and QUIT, then ARE YOU SURE? with NO and YES. The selected item uses clut 5 (opaque), the other clut 4 (50% blend). The glyph table {u, v, width} at 0x1004d760 indexes characters by (c-1)&31; height is 0x12.

## Input (original defaults)

- **Keys.** Arrows move, Z jump, X sword, C punch, D power punch, S weapon.
- **ENG1 pad masks** (DAT_1004d958 → gamestruct +0x6c..+0x76): sword = SQUARE, jump = CROSS, punch = CIRCLE, power = TRIANGLE, weapon = R1 (next weapon; L1 is previous).
- **Fixed keys.** F1 help, F2 properties, F3 restart level, Esc pause, Enter/Space select, Alt+Enter fullscreen.
- **`--dev` keys.** 1–7 jump to a level, F5 collision overlay, F6 free camera, F12 screenshot.

## Polish pass (work/polish): flow, tally, credits, cheats, attract, Meg, HUD

### Game flow: FUN_100164b0 states, ported in `fe_flow.inc` `flow_run`

**Level re-entry.** gamestruct[3] holds the ENG exit code:
- 0x31: level done.
- 0x12 / 0x13: quit.
- Anything else: game over. ENG1 FUN_10018f60 sets 1 when the lives run out; script op 0x29 gives 0x21 / 0x22.

**State 0x29 (next level).** Clears the per-level counters (+0x11, +0x14..+0x1c), then increments the level:
- Level 4 (Nessus) becomes 5.
- Levels 10–12 on easy go to 0xcf: credits.
- Level 12 goes to 0xfa: every story video in turn, used by the movie password.
- Otherwise the level's card plays. Video 18 plays before level 1 only when DAT_1002acec == 0, i.e. a new game rather than a continue.

**Level done.** Tally (0xd2), then the story video for the level (0xd3), then the next level:

| Level | Video |
|---|---|
| 3 Forest | 12 |
| 4 / 5 Thebes | 13 |
| 6 Hydra | 14 |
| 7 Medusa | 17 |
| 8 Cyclops | 19 |
| 9 Titans | 25, or credits on easy |
| 10 Torment | 16 |
| 11 Vortex | 15 (hard) or 27, then credits (0xcd) → video 3 → menu |
| 1 / 2 | none |

**Game over.** A per-level scene plays first:

| Level | Video |
|---|---|
| 1 / 2 | 4 |
| 3 / 9 | 8 |
| 4 / 5 / 7 | 6 |
| 6 | 20 |
| 8 | 7 |
| 10 / 11 | 10 |

Then state 200 runs:
- Continues come from +0x10. Cheat 0x10 forces 1.
- With no continue: video 9, then 3, then the menu. Skipping 9 goes straight to the menu.
- Otherwise video 22 plays (FUN_10009980 flag 0x4000). Cross during it means continue: continues−−, lives = start lives, the level is replayed after video 21.
- A continue is earned when all eight story scrolls are found (+0x1c == 0xff, 0x200b pickups).

**Attract mode (0xdc).**
- Triggers after 900 idle menu ticks (ff0 = 5, DAT_1002bb30).
- Demos alternate with video 11.
- Demo table at 0x10026068 is {level, file}: (1,1) (1,2) (1,3) (2,4) (3,5) (5,6) (5,7).
- Files: ENG1 0x1004d938 = M:\DEMO\DEMO{,1_1,1_2,1_3,2_1,3_1,5_1,5_2}.DAT. Header: byte 0 = checkpoint, u16 at +2 = flags, byte 4 = difficulty. Pad words run from word 3 to 0x373 (ENG1 FUN_10005ea0 / FUN_10006005). Any pad press ends the demo.

**PC display.**
- HERCULES.EXE FUN_00401bd0 stores the requested 320x256 at gamestruct+0x130 but nothing reads it.
- TITLE FUN_1001d590 puts the object origin at (160,120).
- So title screens are drawn from the top of a 320x240 view and their last 16 lines are cut off, not squeezed.

**Save / load (PC).**
- PSX memory-card screens (lsgame, FUN_100102f0 / FUN_10010670) are unreachable on PC.
- Start on the password-reward screen calls FUN_1000c9b0(level<<16 | difficulty<<8 | lives) with the next level; the native port writes ~/Library/Application Support/Hercules/savegame.dat.
- F3 on the menu (FUN_1000c910(-1) → ff0 = 3) continues that save.

### Tally (FUN_1001c500), ported in `fe_flow.inc`
- **Assets.** Background T007.TGA. Bank TALLYUP is TITLE bank 0xc; the bank table is at 0x10026110 (5 dwords per row).
- **Objects.** TITLE objects project as x * (0x400 − z) >> 10 (FUN_100195f0). The draw routine is table 0x100220d0[+0x23 = 6], i.e. FUN_10003d10, which uses OT depth +0x3e.
- **Ported routines:**
  - Title zoom: 9b40.
  - Subtitle: bdc0.
  - HERCULES scroll letters: b150. Missing letters are drawn dimmed; boss levels 6, 7 and 11 skip the letters.
  - CONTINUE BONUS: b710.
  - Percentage row with coin icons: a3a0 / 9fa0.
    - percent = (+0x18 + +0x14) * 100 / (+0x1a + +0x16)
    - rating = 50 + percent/2 − 25 * deaths(+0xd), clamped to 0..100.
    - Bonus life when rating > 40 / 60 / 80 (by difficulty).
  - Rating text: b900 (0x2fd..0x301).
  - Phil with speech bubble, running for the bonus coin: a7c0 / 9c90.
  - Pegasus: a630. Goat: a180. Hercules sparkle run: beb0.
  - Sparkles: 9a40 / 9870.
- **Speech clips** (FUN_1000c4d0 → HERCULES.EXE FUN_00401dd0) are XA clips n = sound id 1000+n, i.e. PC_<1000+n>.ESF. The table is at EXE 0x4352b8. Rating clips: 0x16 / 0x1a / 0x1c / 0x22 / 0x21 / 0x1b / 0x1f.
- **Password reward** (all four vases, +0x11 == 0xf, or cheat 0x800000):
  - Vases come from password-table entry difficulty*12 + level (ac90 / ab70); the header is bab0.
  - Start saves; Cross continues.

### Other screens
- **Credits.** FUN_10001a70 shows endeasy (easy only, 300 ticks), then c1..c31 at 60 ticks each, skipping 29. Start ends it.
- **Cheats.**
  - Passwords 100+101 open the full cheat screen; 102+103 open the level-select-only screen (+0x2e = 1). Either adds CHEATS to the menu (bit 31).
  - 104+105 set cheat flag 8 and gamestruct[6] = 3: the time-trial countdown, drawn by ENG1 FUN_10032bd0.
  - Screen: FUN_100016f0 / FUN_10001000 / FUN_10001300, CHEAT.TGA, bank CHEATS (0x11).
  - Up: unlimited lives (8). Left: unlimited continues (0x10). Down: all vases (0x800000).
  - Right (or Up/Down in mode 1) picks the level from 0x10022030 / 0x10022080; Start begins it.
- **Password / Meg.** Full port of FUN_1000d910 + FUN_1000d720 / FUN_1000d4b0 in `fe_pswd.inc`:
  - Movement: walk-in using the int 16.16 step table at 0x100257e8, intro speech clip 0 with lip sync, walk and turn between the gaps.
  - Hits: Up/Down hit the vase, which turns until the next picture stop (switch at 0x1000f300). Holding Circle/Square repeats.
  - Fidgets: tables 0x10025968 / 0x100259b8 / 0x100259d0, with 999 = sound marker. Hint clips 1..5 come from FUN_1000d6c0.
  - Wrong code: clips 7 / 0x14 / 6 / 0x15 / 0x13, then back to the menu.
  - Right code: per-level clip, vases rise and glow, LEVn picture, then the level.
- **Menu sounds.** 0x300 down, 0x301 up, 0x302 select, 0x303 skip carry-in / back, 0x326 board lands. Loaded from the OPTIONS bank (dir 0x19), plus TALLY (0x26) on the tally screen.

### HUD (ENG1 FUN_10012040 + FUN_10032bd0), ported in `hud.c`
- **Left orb.** Alternates between Hercules (icon 0x7d) and the lives count (0x10055330[lives]) every 0x7f ticks; a life pickup shows the count at once.
- **Right orb.** Selected Gift of the Gods (0x10055310). Orbs spin through 12-frame icon groups (icon table 0x1005dd80).
- **Charge meters** (DAT_1007082c): power punch on the left (u 0xd0), weapon charges on the right (u 200).
- **Boss bar.** DAT_10072188 states 2 = slide in, 5 = fade in, 7 = fade out; damage via DAT_10072184. Drawn at (DAT_10070838, 0xd0).
- **Time-trial digits** at (0x18,0xd0) / (0x25,0xd0).
- The original HUD has no coin, letter or vase counters.
