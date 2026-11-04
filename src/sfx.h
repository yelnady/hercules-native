/* Game sound effects and level music, ported from the "Sonix" layer of HERCULES.EXE / ENG1.DLL.
 *
 * Per level, ENG1's level table (0x1005a6a8 + 0x14, i.e. (&PTR_DAT_1005a6a4)[level*6]) points at a byte list
 * read by HERCULES.EXE LoadLevelSonix (FUN_004016b0):
 *     [0xff musicvol sfxvol] [0xfe x] music_dir sfx_dir sfx_dir ... 0xff        ('c' = 99 entries are skipped)
 * Each dir index selects q:\<DIR>\ from the table at HERCULES.EXE 0x435178. music_dir names MUSIC.MUS (CD-audio
 * positions), each sfx_dir names SFX.SFX, the PC stand-in for a PSX VAB: u16 FX ids per tone at 0x44 and tone
 * volumes at 0x144 (FUN_00401330 / FUN_0040be60 / FUN_0040bec0). FX id N is the sample file
 * M:\LANGUAGE\SONIX\PC_N.ESF. The game's sound table (gamestruct+0x2f0, 3 bytes per id: bank, tone, volume)
 * maps a script sound id (& 0x7ff) back to its bank/tone; FUN_10006da0 plays it. */
#pragma once
#include <stdint.h>
#include "fs.h"
#include "eng1.h"

/* Sound id flag bits (FUN_10006da0). */
enum { SFX_QUIET = 0x800, SFX_FAR2 = 0x1000, SFX_NEAR2 = 0x2000, SFX_POSITIONAL = 0x4000, SFX_KEEP = 0x8000 };

/* Screen position of the emitting object as ENG1 sees it: obj+0xc = screen x (0..320, centre 160),
 * obj+0x10 = depth (160 at the player plane; louder in front, fading out by depth 0x2a0). */
typedef struct { int sx, depth; int hidden; } SfxPos;       /* hidden: obj+0x68 == 0 (not in this frame's draw list) */

/* AUDSHELL: tracked emitters. FUN_1000666b re-reads the emitting object's screen position each frame for sounds with
 * flag 0x800. The callback fills `pos` and returns 0 once the object is gone. */
typedef int (*SfxTrackFn)(void *ctx, SfxPos *pos);

/* AUDSHELL: Properties > Sound (sound effects / CD music toggles, volume 0..100): the original's user options
 * gamestruct +0x1b0c / +0x1b10 scale the level volumes (+0x1b18 = level volume * option >> 8). */
void sfx_set_options(int sfx_on, int music_on, int volume);
int sfx_load_level(const Fs *fs, const PeImage *eng1, int level, const char *data_dir);
/* AUDSHELL: ENG3 levels: the sound list pointer of level n is the u32 at table_va + 0x18 * n (ENG3.DLL 0x1003853c, found in
 * FUN_1001abb0: FUN_1001fa40(*(0x1003853c + level * 0x18))); ENG1's table is 0x1005a6a4. */
int sfx_load_level_tbl(const Fs *fs, const PeImage *dll, uint32_t table_va, int level, const char *data_dir);
void sfx_free(void);
/* FUN_10006da0: owner = any non-zero tag identifying the emitting object (0 = none). */
int sfx_play(uint16_t id, int owner, const SfxPos *pos);
int sfx_play_ex(uint16_t id, int owner, const SfxPos *pos, SfxTrackFn fn, void *ctx);
/* FUN_1000666b: per-tick update of tracked voices (call once per game tick). */
void sfx_tick(void);
/* FUN_10007ef6: stop every voice owned by `owner`. */
void sfx_stop_owner(int owner);
/* AUDSHELL: stop only the looping voices of `owner` (called when the emitting object disappears). */
void sfx_stop_owner_loops(int owner);
/* FUN_10007f8e: stop voices playing id (for owner, or every owner if 0). */
void sfx_stop(uint16_t id, int owner);
void sfx_stop_all(void);
/* AUDIT: is this id registered in the level's sound table (otherwise FUN_10006da0 drops it)? */
int sfx_has(uint16_t id);
/* If non-NULL, every play request is logged here (headless verification). */
void sfx_set_log(void *file);
/* Dump all samples of the loaded level as WAV files into dir; returns the count. */
int sfx_dump_wavs(const char *dir);
/* NESSUS: FUN_1000e670 ReplaceSfx - load one more SFX.SFX bank (directory index) into the running level. */
void sfx_load_bank(const Fs *fs, int dir);

/* POLISH: front-end banks (OPTIONS, plus TALLY on the level-complete screen) and title music. */
int sfx_load_title(const Fs *fs, const char *data_dir, int tally);
/* POLISH: speech clip n (PC_<1000+n>.ESF); returns its length in 30 Hz ticks, 0 if missing. */
int sfx_speech(const Fs *fs, int n);
void sfx_speech_stop(void);
/* AUDSHELL: 1 while a speech clip is still playing (gamestruct+0x1b00). */
int sfx_speech_busy(void);
/* POLISH: lip-sync level 0..31 at `ticks` after the clip started. */
int sfx_speech_level(int ticks);
