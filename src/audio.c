#include "audio.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>

#define MIX_RATE 44100
#define NVOICES 24

static const int IMA_INDEX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
static const int IMA_STEP[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55,
    60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
    4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767};

typedef struct {
    const Sound *s;
    uint64_t pos;       /* 32.32 fixed */
    uint64_t step;
    float gl, gr;
    int loop, active;
} Voice;

static SDL_AudioDeviceID dev;
static Voice voices[NVOICES];
static struct { int16_t *pcm; int frames; uint64_t pos, step; float gain; } music;

static void mix(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    int16_t *out = (int16_t *)stream;
    int frames = len / 4;
    static float acc[8192];
    if (frames > 4096) frames = 4096;
    memset(acc, 0, sizeof(float) * frames * 2);
    for (int v = 0; v < NVOICES; v++) {
        Voice *V = &voices[v];
        if (!V->active) continue;
        const int16_t *pcm = V->s->pcm;
        int n = V->s->n;
        for (int i = 0; i < frames; i++) {
            uint32_t idx = (uint32_t)(V->pos >> 32);
            if ((int)idx >= n) {
                if (V->loop) { V->pos -= (uint64_t)n << 32; idx = (uint32_t)(V->pos >> 32); }
                else { V->active = 0; break; }
            }
            float f = (float)(uint32_t)V->pos * (1.0f / 4294967296.0f);
            int nx = (int)idx + 1 < n ? (int)idx + 1 : V->loop ? 0 : (int)idx;
            float smp = pcm[idx] + (pcm[nx] - pcm[idx]) * f;
            acc[2 * i] += smp * V->gl;
            acc[2 * i + 1] += smp * V->gr;
            V->pos += V->step;
        }
    }
    if (music.pcm && music.frames > 1) {
        for (int i = 0; i < frames; i++) {
            uint32_t idx = (uint32_t)(music.pos >> 32);
            if ((int)idx + 1 >= music.frames) { music.pos = 0; idx = 0; }
            float f = (float)(uint32_t)music.pos * (1.0f / 4294967296.0f);
            const int16_t *a = music.pcm + 2 * idx;
            acc[2 * i] += (a[0] + (a[2] - a[0]) * f) * music.gain;
            acc[2 * i + 1] += (a[1] + (a[3] - a[1]) * f) * music.gain;
            music.pos += music.step;
        }
    }
    for (int i = 0; i < frames * 2; i++) {
        float x = acc[i];
        out[i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
    }
}

int audio_init(void)
{
    SDL_AudioSpec want = {0}, have;
    want.freq = MIX_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = mix;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) return -1;
    SDL_PauseAudioDevice(dev, 0);
    return 0;
}

void audio_shutdown(void)
{
    if (dev) SDL_CloseAudioDevice(dev);
    dev = 0;
    free(music.pcm);
    memset(&music, 0, sizeof music);
}

int sound_load(Sound *s, const Fs *fs, const char *path)
{
    memset(s, 0, sizeof *s);
    uint32_t sz;
    const uint8_t *b = fs_get(fs, path, &sz);
    if (!b || sz < 16) return -1;
    uint32_t magic = b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24;
    uint32_t size = b[4] | b[5] << 8 | b[6] << 16 | (uint32_t)b[7] << 24;
    if (magic != 0x03465345) return -1;
    s->rate = b[8] | b[9] << 8;
    s->loop = b[12] != 0;
    s->bits8 = b[13] != 0;
    if (s->rate <= 0) s->rate = 22050;
    s->n = (int)(size / 2);
    s->pcm = malloc(sizeof(int16_t) * (s->n ? s->n : 1));
    const uint8_t *data = b + 16;
    uint32_t dlen = sz - 16;
    int idx = 0, pred = 0;
    for (int i = 0; i < s->n; i++) {
        uint32_t bi = (uint32_t)i >> 1;
        int byte = bi < dlen ? data[bi] : 0;
        int code = (i & 1) ? (byte & 15) : (byte >> 4);
        int step = IMA_STEP[idx];
        idx += IMA_INDEX[code];
        idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
        int diff = step >> 3;
        if (code & 4) diff += step;
        if (code & 2) diff += step >> 1;
        if (code & 1) diff += step >> 2;
        if (code & 8) diff = -diff;
        pred += diff;
        pred = pred < -32768 ? -32768 : pred > 32767 ? 32767 : pred;
        s->pcm[i] = (int16_t)pred;
    }
    return 0;
}

void sound_free(Sound *s)
{
    if (dev) {
        SDL_LockAudioDevice(dev);
        for (int v = 0; v < NVOICES; v++) if (voices[v].s == s) voices[v].active = 0;
        SDL_UnlockAudioDevice(dev);
    }
    free(s->pcm);
    memset(s, 0, sizeof *s);
}

int sound_play_gain(const Sound *s, float gl, float gr, int loop)
{
    if (!dev || !s || !s->pcm || !s->n) return -1;
    SDL_LockAudioDevice(dev);
    int best = -1;
    for (int v = 0; v < NVOICES; v++) if (!voices[v].active) { best = v; break; }
    if (best < 0) {     /* steal the voice nearest to its end */
        uint64_t most = 0;
        for (int v = 0; v < NVOICES; v++) {
            if (voices[v].loop) continue;
            uint64_t left = ((uint64_t)voices[v].s->n << 32) - voices[v].pos;
            if (best < 0 || left < most) { best = v; most = left; }
        }
        if (best < 0) best = 0;
    }
    Voice *V = &voices[best];
    V->s = s;
    V->pos = 0;
    V->step = ((uint64_t)s->rate << 32) / MIX_RATE;
    V->gl = gl;
    V->gr = gr;
    V->loop = loop;
    V->active = 1;
    SDL_UnlockAudioDevice(dev);
    return best;
}

int sound_play(const Sound *s, int vol, int pan, int loop)
{
    pan = pan < -256 ? -256 : pan > 256 ? 256 : pan;
    float gl = vol * (256 - (pan > 0 ? pan : 0)) / 65536.0f, gr = vol * (256 + (pan < 0 ? pan : 0)) / 65536.0f;
    return sound_play_gain(s, gl, gr, loop);
}

void sound_set_gain(int voice, float gl, float gr)
{
    if (!dev || voice < 0 || voice >= NVOICES) return;
    SDL_LockAudioDevice(dev);
    voices[voice].gl = gl;
    voices[voice].gr = gr;
    SDL_UnlockAudioDevice(dev);
}

int sound_playing(int voice, const Sound *s)
{
    if (!dev || voice < 0 || voice >= NVOICES) return 0;
    SDL_LockAudioDevice(dev);
    int r = voices[voice].active && voices[voice].s == s;
    SDL_UnlockAudioDevice(dev);
    return r;
}

void sound_stop(int voice)
{
    if (!dev || voice < 0 || voice >= NVOICES) return;
    SDL_LockAudioDevice(dev);
    voices[voice].active = 0;
    SDL_UnlockAudioDevice(dev);
}

void music_set(int16_t *pcm, int frames, int rate, float gain)
{
    int16_t *old;
    if (dev) SDL_LockAudioDevice(dev);
    old = music.pcm;
    music.pcm = pcm;
    music.frames = pcm ? frames : 0;
    music.pos = 0;
    music.step = rate > 0 ? ((uint64_t)rate << 32) / MIX_RATE : 0;
    music.gain = gain;
    if (dev) SDL_UnlockAudioDevice(dev);
    if (old != pcm) free(old);
}

void music_set_gain(float gain)
{
    if (dev) SDL_LockAudioDevice(dev);
    music.gain = gain;
    if (dev) SDL_UnlockAudioDevice(dev);
}
