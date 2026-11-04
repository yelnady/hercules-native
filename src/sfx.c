#include "sfx.h"
#include "audio.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FX 0x4b0        /* HERCULES.EXE keeps 1200 DirectSound buffer slots (DAT_00b99908) */
#define NTRACK 24           /* ENG1 voice records (DAT_100721e0, 0x40 bytes each) */

/* Directory table at HERCULES.EXE 0x435178 (path, size pairs). Indices 0..12 hold MUSIC.MUS,
 * 13..38 hold SFX.SFX; the strings are copied here because HERCULES.EXE is not shipped with the port. */
static const char *const DIRS[39] = {
    "PLAYROOM", "TRAINING", "GAUNTLET", "FOREST", "NESSUS", "THEBES", "HYDRA", "MEDUSA", "CYCLOPS", "TITANS",
    "TORMENT", "VORTEX", "OPTIONS", "PLAYROOM", "TRAINING", "GAUNTLET", "FOREST", "NESSUS", "THEBES", "HYDRA",
    "MEDUSA", "CYCLOPS", "TITANS", "TORMENT", "VORTEX", "OPTIONS", "HERC_TRA", "HERC_GAU", "HERC_FOR",
    "HERC_NES", "HERC_THE", "HERC_HYD", "HERC_MED", "HERC_CYC", "HERC_TIT", "HERC_TOR", "HERC_VOR", "NESSUS",
    "TALLY",
};

typedef struct { uint8_t bank, tone, vol; } FxEntry;        /* gamestruct+0x2f0 */

static struct {
    FxEntry table[0x800];
    Sound snd[MAX_FX];
    int music_vol, sfx_vol;     /* gamestruct+0x1b04 / +0x1b08 (level), user options +0x1b0c/+0x1b10 = 0x100 */
    int stereo;                 /* gamestruct+0x1b24 (options screen: stereo) */
    struct Trk { int voice, id, owner; const Sound *s; int flags, age, curL, curR, vol; SfxTrackFn fn; void *ctx; } trk[NTRACK];
    FILE *log;
    int opt_sfx, opt_music, opt_vol;    /* AUDSHELL: Properties > Sound */
    float music_gain;
} S = {.opt_sfx = 1, .opt_music = 1, .opt_vol = 100, .music_gain = 0.7f};

static int user_sfx(void) { return S.opt_sfx ? S.opt_vol * 256 / 100 : 0; }     /* gamestruct +0x1b0c (0x100 = full) */

void sfx_set_options(int sfx_on, int music_on, int volume)
{
    volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    if (sfx_on == S.opt_sfx && music_on == S.opt_music && volume == S.opt_vol) return;
    S.opt_sfx = sfx_on; S.opt_music = music_on; S.opt_vol = volume;
    music_set_gain(S.opt_music ? S.music_gain * (float)volume / 100.0f : 0.0f);
}

void sfx_set_log(void *file) { S.log = (FILE *)file; }

void sfx_stop_all(void)
{
    for (int i = 0; i < NTRACK; i++) {
        if (S.trk[i].s) sound_stop(S.trk[i].voice);
        S.trk[i].s = NULL;
    }
}

void sfx_free(void)
{
    sfx_stop_all();
    for (int i = 0; i < MAX_FX; i++) if (S.snd[i].pcm) sound_free(&S.snd[i]);
    music_set(NULL, 0, 0, 0);
    memset(S.table, 0xff, sizeof S.table);
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* FUN_00401330 LoadSfx: register the bank's tones in the sound table and load each FX id's ESF. */
static void load_bank(const Fs *fs, int dir, int bank)
{
    char path[64];
    if (dir < 0 || dir >= 39) return;
    snprintf(path, sizeof path, "Q:\\%s\\SFX.SFX", DIRS[dir]);
    uint32_t sz;
    const uint8_t *b = fs_get(fs, path, &sz);
    if (!b || sz < 0x1c4) { fprintf(stderr, "sfx: %s missing\n", path); return; }
    for (int t = 0; t < 0x7f; t++) {
        int id = rd16(b + 0x44 + 2 * t);
        if (!id || id >= MAX_FX) continue;
        S.table[id].bank = (uint8_t)bank;
        S.table[id].tone = (uint8_t)t;
        S.table[id].vol = b[0x144 + t];
        if (!S.snd[id].pcm) {
            snprintf(path, sizeof path, "M:\\LANGUAGE\\SONIX\\PC_%d.ESF", id);
            if (sound_load(&S.snd[id], fs, path)) fprintf(stderr, "sfx: %s missing\n", path);
        }
    }
}

/* ---- level music: CD audio track 2, positioned by MUSIC.MUS (tracks are not part of the ISO image) ---- */

static int16_t *load_wav_segment(const char *path, double from, double to, int *frames, int *rate)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t h[12], ch[8];
    int16_t *out = NULL;
    int nch = 0, bits = 0;
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) goto done;
    while (fread(ch, 1, 8, f) == 8) {
        uint32_t len = rd32(ch + 4);
        if (!memcmp(ch, "fmt ", 4)) {
            uint8_t fm[16];
            if (len < 16 || fread(fm, 1, 16, f) != 16) goto done;
            if (rd16(fm) != 1) goto done;               /* PCM only */
            nch = rd16(fm + 2); *rate = (int)rd32(fm + 4); bits = rd16(fm + 14);
            fseek(f, (long)(len - 16 + (len & 1)), SEEK_CUR);
        } else if (!memcmp(ch, "data", 4)) {
            if (bits != 16 || nch < 1 || nch > 2 || *rate <= 0) goto done;
            long total = (long)(len / (2u * nch));
            long a = (long)(from * *rate), e = to > from ? (long)(to * *rate) : total;
            if (e > total) e = total;
            if (a >= e) goto done;
            long n = e - a;
            out = malloc((size_t)n * 4);
            int16_t *tmp = malloc((size_t)n * 2 * nch);
            if (!out || !tmp) { free(out); free(tmp); out = NULL; goto done; }
            fseek(f, a * 2 * nch, SEEK_CUR);
            long got = (long)fread(tmp, (size_t)(2 * nch), (size_t)n, f);
            for (long i = 0; i < got; i++) {
                out[2 * i] = tmp[i * nch];
                out[2 * i + 1] = tmp[i * nch + nch - 1];
            }
            free(tmp);
            *frames = (int)got;
            goto done;
        } else fseek(f, (long)(len + (len & 1)), SEEK_CUR);
    }
done:
    fclose(f);
    return out;
}

/* MUSIC.MUS: 7 offsets, offsets 2..6 = sequences of {from, to} CD positions, bytes {frame, sec, min, track}. */
static void load_music(const Fs *fs, int dir, int seq, const char *data_dir)
{
    char path[512];
    if (dir < 0 || dir >= 13 || !data_dir) return;
    snprintf(path, sizeof path, "Q:\\%s\\MUSIC.MUS", DIRS[dir]);
    uint32_t sz;
    const uint8_t *b = fs_get(fs, path, &sz);
    if (!b || sz < 0x1c) return;
    uint32_t o = rd32(b + 8 + 4 * seq);
    if (o + 8 > sz) return;
    const uint8_t *p = b + o;
    int track = p[3];
    double from = p[2] * 60 + p[1] + p[0] / 75.0, to = p[6] * 60 + p[5] + p[4] / 75.0;
    if (p[7] != track) to = 0;
    if (from == 0 && to == 0) return;
    static const char *const names[] = {"Track%02d.wav", "track%02d.wav", "Track %02d.wav", "HERCULES_TRACK%02d.wav",
                                        "music/Track%02d.wav", "music/track%02d.wav"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        char name[64];
        snprintf(name, sizeof name, names[i], track);
        snprintf(path, sizeof path, "%s/%s", data_dir, name);
        int frames = 0, rate = 0;
        int16_t *pcm = load_wav_segment(path, from, to, &frames, &rate);
        if (pcm) {
            /* CD audio is not scaled by the game's music volume (FUN_0040be30 is a stub on PC). */
            music_set(pcm, frames, rate, S.opt_music ? S.music_gain * (float)S.opt_vol / 100.0f : 0.0f);
            printf("music: %s %.2fs-%.2fs\n", path, from, to);
            return;
        }
    }
    printf("music: CD track %d %d:%02d-%d:%02d not available (no Track%02d.wav next to HERCULES.FS)\n", track,
           (int)from / 60, (int)from % 60, (int)to / 60, (int)to % 60, track);
}

/* FUN_004016b0 LoadLevelSonix, driven by ENG1's per-level list. */
void sfx_load_bank(const Fs *fs, int dir) { load_bank(fs, dir, 3); }   /* NESSUS */

int sfx_load_level(const Fs *fs, const PeImage *eng1, int level, const char *data_dir)
{
    return sfx_load_level_tbl(fs, eng1, 0x1005a6a4u, level, data_dir);
}

int sfx_load_level_tbl(const Fs *fs, const PeImage *eng1, uint32_t table_va, int level, const char *data_dir)
{
    sfx_free();
    S.stereo = 1;
    S.music_vol = S.sfx_vol = 0x80;
    uint32_t list = pe_u32(eng1, table_va + 0x18u * (uint32_t)level);
    const uint8_t *p = list ? pe_ptr(eng1, list, 16) : NULL;
    if (!p) { fprintf(stderr, "sfx: no sound list for level %d\n", level); return -1; }
    if (p[0] == 0xff) { S.music_vol = p[1]; S.sfx_vol = p[2]; p += 3; }
    if (p[0] == 0xfe) p += 2;           /* reverb/mode byte (FUN_0040be30 stub on PC) */
    int music_dir = p[0];
    int bank = 2, nbanks = 0;
    for (int i = 1; i < 12 && p[i] != 0xff; i++) {
        if (p[i] == 'c') continue;
        load_bank(fs, p[i], bank++);
        nbanks++;
    }
    load_music(fs, music_dir, 0, data_dir);
    int n = 0;
    for (int i = 0; i < MAX_FX; i++) n += S.snd[i].pcm != NULL;
    printf("sfx: level %d: %d banks, %d samples, music dir %s, volumes music %d sfx %d\n", level, nbanks, n,
           music_dir < 13 ? DIRS[music_dir] : "?", S.music_vol, S.sfx_vol);
    return 0;
}

/* HERCULES.EXE volume path: KeyOn FUN_0040bf20 squares (vol * tone volume) into a 14-bit SPU-style level,
 * FUN_0040c160 applies the 127 master, FUN_0040fb10 turns the louder channel into DirectSound hundredths of
 * a dB through the table built in FUN_0040fdf0 (1650*log10(i/16384) clamped to -10000, minus 200) and the
 * channel ratio into a pan of 10000*((r-l)/max)^2. */
static double ds_gain(double hundredths_db) { return pow(10.0, hundredths_db / 2000.0); }

static void keyon_gains(int L, int R, int tone_vol, float *gl, float *gr)
{
    int l = L * tone_vol, r = R * tone_vol;
    l = ((l * l) >> 14) * 0x81 >> 7;
    r = ((r * r) >> 14) * 0x81 >> 7;
    l = (127 * l >> 7) & 0x3fff;
    r = (127 * r >> 7) & 0x3fff;
    int m = l > r ? l : r;
    double vol = m <= 0 ? -10000.0 : 1650.0 * log10(m / 16384.0);   /* fldlg2; fyl2x */
    if (vol < -10000) vol = -10000;
    if (vol > 0) vol = 0;
    vol -= 200;
    double pan = 10000;
    if (l && r) {
        double d = (double)(r - l) / m;
        pan = d * d * 10000.0;
        if (pan > 10000) pan = 10000;
    }
    if (l > r) pan = -pan;
    *gl = (float)ds_gain(vol - (pan > 0 ? pan : 0));
    *gr = (float)ds_gain(vol - (pan < 0 ? -pan : 0));
    if (m <= 0) *gl = *gr = 0;
}

static int playing_count(int id)        /* FUN_10006613 */
{
    int n = 0;
    for (int i = 0; i < NTRACK; i++)
        if (S.trk[i].s && S.trk[i].id == id) {
            if (sound_playing(S.trk[i].voice, S.trk[i].s)) n++;
            else S.trk[i].s = NULL;
        }
    return n;
}

/* Pan / distance part of FUN_10006da0 (0x4000 branch) and of the FUN_1000666b update: emitter screen x and depth
 * give the L/R levels 0..127 before the master volume. */
static void pan_levels(int id, const SfxPos *pos, int *pbl, int *pbr)
{
    int d = pos->sx - 0xa0, bl, br;
    if (d < -0x13 || d > 0x13) {
        bl = d < -0x13 ? -(pos->sx - 0x8c) : d * 2 - 0x28;
        br = d < 0x15 ? (pos->sx - 0x8c) * -2 : pos->sx - 0xb4;
    } else bl = br = 0;
    bl = bl < -0x280 ? -0x280 : bl > 0x280 ? 0x280 : bl;
    br = br < -0x280 ? -0x280 : br > 0x280 ? 0x280 : br;
    bl = 0x7f - bl / 5; br = 0x7f - br / 5;
    bl = bl < 0 ? 0 : bl > 0x7f ? 0x7f : bl;
    br = br < 0 ? 0 : br > 0x7f ? 0x7f : br;
    int att = pos->depth * 2 - 0x140;
    if (id & SFX_FAR2) att *= 2;
    if (id & SFX_NEAR2) att >>= 1;
    int k = 0x800 - att;
    k = k < 0 ? 0 : k > 0x800 ? 0x800 : k;
    *pbl = bl * k >> 11; *pbr = br * k >> 11;
}

static void master_lr(int bl, int br, int *L, int *R)
{
    int master = S.sfx_vol * user_sfx() >> 8;   /* +0x1b18 = user option (0x100) * level volume >> 8 */
    if (S.stereo) { *L = bl * master >> 7; *R = br * master >> 7; }
    else *L = *R = (bl + br) * master >> 8;
}

int sfx_play_ex(uint16_t id, int owner, const SfxPos *pos, SfxTrackFn fn, void *ctx)
{
    int n = id & 0x7ff;
    const FxEntry *e = &S.table[n];
    if (e->bank == 0xff || e->tone == 0xff) { if (getenv("SFX_MISS")) fprintf(stderr, "SFX_MISS id %04x (fx %d not in this level's sound table)\n", id, n); return -1; }
    if (!owner && (id & 0xf800)) return -1;      /* flagged sounds need an emitting object */
    if ((id & SFX_POSITIONAL) && owner && pos && pos->hidden) return -1;    /* obj+0x68 == 0: not drawn this frame */
    if (playing_count(n) >= 6) return -1;
    int master = S.sfx_vol * user_sfx() >> 8;
    int L, R, quiet = 0;
    if (!(id & SFX_POSITIONAL) || !pos) {
        if ((id & SFX_QUIET) && pos && !fn) {
            /* tracked sound without a live emitter: place it where it was emitted (the original fades in to the
             * emitter's screen position after two ticks) */
            int bl, br;
            pan_levels(id, pos, &bl, &br);
            master_lr(bl, br, &L, &R);
        } else {
            L = R = (id & SFX_QUIET) ? 0 : master * 0x7f >> 7;
            quiet = (id & SFX_QUIET) != 0;
        }
    } else {
        int bl, br;
        pan_levels(id, pos, &bl, &br);
        master_lr(bl, br, &L, &R);
    }
    const Sound *s = &S.snd[n];
    float gl, gr;
    keyon_gains(L, R, (int8_t)e->vol, &gl, &gr);
    int voice = sound_play_gain(s, gl, gr, s->loop);
    if (S.log)
        fprintf(S.log, "sfx id=0x%04x fx=%d bank=%d tone=%d vol=%d L=%d R=%d gain=%.3f/%.3f loop=%d len=%.2fs voice=%d owner=%d%s\n",
                id, n, e->bank, e->tone, e->vol, L, R, gl, gr, s->loop, s->rate ? (double)s->n / s->rate : 0.0, voice, owner,
                quiet ? " tracked" : "");
    for (int i = 0; i < NTRACK; i++) {
        if (S.trk[i].s && sound_playing(S.trk[i].voice, S.trk[i].s)) continue;
        S.trk[i].voice = voice; S.trk[i].id = n; S.trk[i].owner = owner; S.trk[i].s = voice >= 0 ? s : NULL;
        S.trk[i].flags = id; S.trk[i].age = 0; S.trk[i].curL = L; S.trk[i].curR = R;
        S.trk[i].vol = (int8_t)e->vol; S.trk[i].fn = (id & SFX_QUIET) && owner ? fn : NULL; S.trk[i].ctx = ctx;
        break;
    }
    return voice;
}

int sfx_play(uint16_t id, int owner, const SfxPos *pos) { return sfx_play_ex(id, owner, pos, NULL, NULL); }

/* FUN_1000666b: voices of flag-0x800 sounds follow their emitter after two ticks (smoothed by half the distance
 * per tick; hidden emitters keep their last level). */
void sfx_tick(void)
{
    for (int i = 0; i < NTRACK; i++) {
        struct Trk *t = &S.trk[i];
        if (!t->s || !t->fn) continue;
        if (!sound_playing(t->voice, t->s)) { t->s = NULL; continue; }
        if (t->age < 2) { t->age++; continue; }
        SfxPos p = {0};
        if (!t->fn(t->ctx, &p) || p.hidden || p.depth == 0) continue;
        int bl, br, L, R;
        pan_levels(t->flags & ~SFX_POSITIONAL, &p, &bl, &br);
        master_lr(bl, br, &L, &R);
        t->curL += (L - t->curL) >> 1;
        t->curR += (R - t->curR) >> 1;
        float gl, gr;
        keyon_gains(t->curL, t->curR, t->vol, &gl, &gr);
        sound_set_gain(t->voice, gl, gr);
        if (S.log) fprintf(S.log, "sfx track id=0x%04x owner=%d sx=%d depth=%d L=%d R=%d\n", t->flags, t->owner, p.sx, p.depth, t->curL, t->curR);
    }
}

int sfx_has(uint16_t id)
{
    const FxEntry *e = &S.table[id & 0x7ff];
    return e->bank != 0xff && e->tone != 0xff;
}

void sfx_stop(uint16_t id, int owner)
{
    int n = id & 0x7ff;
    if (S.log) fprintf(S.log, "sfx stop id=0x%04x owner=%d\n", id, owner);   /* FIX31: log stops too (headless verification) */
    for (int i = 0; i < NTRACK; i++) {
        if (!S.trk[i].s || S.trk[i].id != n || (owner && S.trk[i].owner != owner)) continue;
        if (sound_playing(S.trk[i].voice, S.trk[i].s)) sound_stop(S.trk[i].voice);
        S.trk[i].s = NULL;
    }
}

void sfx_stop_owner_loops(int owner)
{
    if (!owner) return;
    for (int i = 0; i < NTRACK; i++) {
        if (!S.trk[i].s || S.trk[i].owner != owner || !S.trk[i].s->loop) continue;
        if (sound_playing(S.trk[i].voice, S.trk[i].s)) sound_stop(S.trk[i].voice);
        S.trk[i].s = NULL;
    }
}

void sfx_stop_owner(int owner)
{
    if (!owner) return;
    for (int i = 0; i < NTRACK; i++) {
        if (!S.trk[i].s || S.trk[i].owner != owner) continue;
        if (sound_playing(S.trk[i].voice, S.trk[i].s)) sound_stop(S.trk[i].voice);
        S.trk[i].s = NULL;
    }
}

static void put32(FILE *f, uint32_t v) { uint8_t b[4] = {v, v >> 8, v >> 16, v >> 24}; fwrite(b, 1, 4, f); }
static void put16(FILE *f, uint16_t v) { uint8_t b[2] = {v, v >> 8}; fwrite(b, 1, 2, f); }

int sfx_dump_wavs(const char *dir)
{
    int count = 0;
    for (int id = 0; id < MAX_FX; id++) {
        const Sound *s = &S.snd[id];
        if (!s->pcm) continue;
        char path[512];
        snprintf(path, sizeof path, "%s/PC_%d.wav", dir, id);
        FILE *f = fopen(path, "wb");
        if (!f) continue;
        fwrite("RIFF", 1, 4, f); put32(f, 36 + 2u * s->n); fwrite("WAVEfmt ", 1, 8, f);
        put32(f, 16); put16(f, 1); put16(f, 1); put32(f, (uint32_t)s->rate); put32(f, 2u * s->rate);
        put16(f, 2); put16(f, 16); fwrite("data", 1, 4, f); put32(f, 2u * s->n);
        fwrite(s->pcm, 2, (size_t)s->n, f);
        fclose(f);
        count++;
    }
    return count;
}

/* ---- POLISH: front-end sounds ----
 * TITLE.DLL FUN_10016470 loads the OPTIONS bank (dir 0x19) as VAB 2 and plays OPTIONS music sequence 1
 * (FUN_1000c410(0xc, 1)); the level tally (process FUN_1001c500, state 0xd2) adds TALLY (dir 0x26). */
int sfx_load_title(const Fs *fs, const char *data_dir, int tally)
{
    sfx_free();
    S.stereo = 1;
    S.music_vol = S.sfx_vol = 0x80;
    load_bank(fs, 25, 2);
    if (tally) load_bank(fs, 38, 3);
    load_music(fs, 12, tally ? 2 : 1, data_dir);
    return 0;
}

/* Speech (HERCULES.EXE FUN_00401dd0): clip n is FX id 1000+n (the PSX XA table at EXE 0x4352b8); the
 * lip-sync callback FUN_00401ee0 returns the current output level & 0x1f. */
static struct { Sound s; int voice, have, n; Uint64 start; } SP = {.voice = -1};

int sfx_speech(const Fs *fs, int n)
{
    sfx_speech_stop();
    if (SP.have) { sound_free(&SP.s); SP.have = 0; }
    char path[64];
    snprintf(path, sizeof path, "M:\\LANGUAGE\\SONIX\\PC_%d.ESF", 1000 + n);
    if (sound_load(&SP.s, fs, path)) return 0;
    SP.have = 1; SP.n = n;
    { float g = 0.9f * (float)user_sfx() / 256.0f; SP.voice = sound_play_gain(&SP.s, g, g, 0); }
    SP.start = SDL_GetTicks64();
    if (S.log) fprintf(S.log, "speech %d (%s) %.2fs\n", n, path, (double)SP.s.n / SP.s.rate);
    return SP.s.rate ? (int)((long long)SP.s.n * 30 / SP.s.rate) + 1 : 0;
}

void sfx_speech_stop(void)
{
    if (SP.have && SP.voice >= 0 && sound_playing(SP.voice, &SP.s)) sound_stop(SP.voice);
    SP.voice = -1;
}

int sfx_speech_busy(void) { return SP.have && SP.voice >= 0 && sound_playing(SP.voice, &SP.s); }

int sfx_speech_level(int ticks)
{
    if (!SP.have || !SP.s.rate) return 0;
    long pos = (long)ticks * SP.s.rate / 30;
    if (pos < 0 || pos >= SP.s.n) return 0;
    long a = 0, cnt = 0;
    for (long i = pos; i < pos + SP.s.rate / 30 && i < SP.s.n; i++, cnt++) a += abs(SP.s.pcm[i]);
    if (!cnt) return 0;
    int lv = (int)(a / cnt) >> 9;           /* 0..63 for full-scale speech */
    return lv > 0x1f ? 0x1f : lv;
}
