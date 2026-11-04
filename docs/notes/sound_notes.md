# Hercules sound system (PC port): decoded 2026-11-02

Implementation: `work/sound/src/sfx.c/h` (game layer) + `audio.c/h` (mixer).

## Data
- `M:\LANGUAGE\SONIX\PC_<N>.ESF` = sample for FX id N (HERCULES.EXE FUN_0040f3d0, format string `m:\language\sonix\pc_%ld.esf`, max 1200 ids).
  Header 16 bytes: `'ESF',3` | u32 PCM byte size | u32 rate (11025/22050) | byte loop flag (DSBPLAY_LOOPING, stored at DAT_00a98a18[id]) | byte 8-bit-buffer flag | 2 pad.
  Body: IMA ADPCM, high nibble first, size/4 bytes -> size/2 samples, mono. The engine's decoder output is byte-identical to tools/assets.py.
- `Q:\<DIR>\SFX.SFX` (452 bytes, the PC stand-in for a PSX VAB): u16 FX id per tone at 0x44 + 2*t (t < 0x7f), signed-byte tone volume at 0x144 + t.
- `Q:\<DIR>\MUSIC.MUS` (452 bytes): 7 u32 offsets; offsets[2..6] = sequences of {from, to} CD positions, 4 bytes each = {frame, sec, min, track}. All level music is a section of **CD audio track 2** (e.g. Training 4:25-6:55, Thebes 15:29-18:33, Vortex 31:08-34:20; OPTIONS seq 1-4 = menu music 2:18-6:55). Played through MCI `play cdaudio from .. to ..` (FUN_00409e60).
- Directory table: HERCULES.EXE 0x435178, {path, size} pairs. 0-12 `q:\PLAYROOM\ TRAINING GAUNTLET FOREST NESSUS THEBES HYDRA MEDUSA CYCLOPS TITANS TORMENT VORTEX OPTIONS` (MUSIC.MUS), 13-38 the same plus `HERC_TRA HERC_GAU HERC_FOR HERC_NES HERC_THE HERC_HYD HERC_MED HERC_CYC HERC_TIT HERC_TOR HERC_VOR NESSUS TALLY` (SFX.SFX).

## Per-level list (ENG1)
`(&PTR_DAT_1005a6a4)[level*6]` = level table 0x1005a6a8 entry + 0x14, passed by FUN_10020300 to LoadLevelSonix (HERCULES.EXE FUN_004016b0, through the callback table at EXE 0x4361b8; ENG1 global = EXE address + 0xFC3AD68, e.g. DAT_10070f8c).
Bytes: `[ff musicvol sfxvol] [fe x] music_dir sfx_dir... ff`. 'c' (99) entries are skipped. Sfx banks get VAB ids 2, 3, ...
E.g. Thebes: `ff 46 6e 05 12 1e ff` = music 70, sfx 110, MUSIC THEBES, banks THEBES + HERC_THE.

## Sound table (gamestruct+0x2f0)
3 bytes per id (0x800 ids): bank, tone, volume, filled by LoadSfx FUN_00401330 from every tone of each bank. Script sound id & 0x7ff == FX id == ESF number.

## Play path
ENG1 FUN_10006da0(id, obj) (script opcode 0x17, footstep opcode 0x00):
- reject if the table entry is 0xff, if id has flags (0xf800) but no object, or if 6 voices already play this id (FUN_10006613).
- no 0x4000 flag: L = R = master*127>>7 (0 when 0x800 is set). master = +0x1b18 = level sfx volume * user option (0x100) >> 8.
- 0x4000 = positional: from obj+0xc (screen x, centre 0xa0) and obj+0x10 (depth, 160 = player plane): pan attenuation per side, distance factor 0x800-(2*depth-0x140) (x2 with 0x1000, /2 with 0x2000), L/R 0..127, then *master>>7 (stereo option +0x1b24; mono = average).
- keyon through DAT_100712a8 = HERCULES.EXE FUN_0040bf20(bank, tone, 0, 60, 0, L, R): level = ((L*tonevol)^2>>14)*129>>7 (14-bit), FUN_0040c160 *127>>7, FUN_0040fb10 -> DirectSound volume = table[max(l,r)] (FUN_0040fdf0: 1650*log10(i/16384) clamped to -10000, minus 200; built with fldlg2/fyl2x, so log10, not log2) and pan = +-10000*((r-l)/max)^2.
- Stop: FUN_10007f8e(id, obj) (e.g. 0x26 by FUN_1001a410 = script op 0x1a, 0x15 by op 0x16). Per-frame update FUN_1000666b only re-volumes 0x800 sounds.
- Footsteps (script op 0x00, levels 1 and 3): below the water line DAT_100718e0 (Forest 0x2e1, set in FUN_10015090; Training 0x360 by FUN_1003ca10) play the op's id and set DAT_1006efa8 = 3; above it play 0x401e, 0x401f, 0x4020 once each as drips.

## Music
The ISO is data-only, so track 2 is not available. The port looks for an optional rip next to HERCULES.FS: `Track02.wav`, `track02.wav`, `Track 02.wav`, `HERCULES_TRACK02.wav` or `music/Track02.wav` (16-bit PCM WAV, any rate, mono or stereo). It loads only the level's section and loops it. Verified with a synthetic WAV.

## Remaining
- Enemy/object sounds (call sfx_play with their owner tag and screen position), sfx_stop for 0x15 (op 0x16 sword-charge path not ported), and 0x800 "quiet then fade-in" sounds with FUN_1000666b updates.
- "ReplaceSfx" dynamic bank swapping (ENG1 string) is not investigated.
- Title/menu sounds: TALLY / OPTIONS banks, loaded by TITLE.DLL.
