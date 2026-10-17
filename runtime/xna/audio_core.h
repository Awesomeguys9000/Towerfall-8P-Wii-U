// audio_core.h - GC-free audio core for the XNA Audio port.
//   * software mixer on SDL2 (voices: volume / pan / pitch / loop / pause)
//   * RIFF WAV parser (PCM8/16, MS-ADPCM, WAVE_FORMAT_EXTENSIBLE)
//   * MS-ADPCM block decoder
//   * XACT v46 (XNA 4 / FACT) .xgs / .xsb / .xwb parser and a cue player that
//     stream-decodes the wave bank (only the waves of playing cues are held in RAM,
//     loaded progressively from the open .xwb by XactEngine::update()).
//
// Header-only on purpose: included by audio.cpp (the GC-facing XNA API) and by the
// standalone test in port/tests/ (no GC runtime). Nothing in here touches ::System.
//
// Threading: the SDL callback (audio thread) only reads/advances Voice / MusicCue
// state under the device lock. All allocation / freeing / file IO happens on the
// main thread. The audio thread never frees or allocates.
#pragma once
#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Microsoft { namespace Xna { namespace Framework { namespace Audio {
// Decoded sample data of a SoundEffect (plain C++, never freed).
struct SoundData
{
    std::vector<int16_t> pcm; // interleaved
    int channels = 1;
    int sampleRate = 44100;
    uint32_t frames = 0;
};
}}}}

namespace xaudio {

using SoundData = ::Microsoft::Xna::Framework::Audio::SoundData;

// ------------------------------------------------------------------ byte helpers (LE, host independent)
inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline int16_t rds16(const uint8_t* p) { return (int16_t)rd16(p); }
inline uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// ------------------------------------------------------------------ MS-ADPCM
static const int kAdpcmAdapt[16] = { 230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230 };
static const int kAdpcmCoef1[7] = { 256, 512, 0, 192, 240, 460, 392 };
static const int kAdpcmCoef2[7] = { 0, -256, 0, 64, 0, -208, -232 };

// Decodes one MS-ADPCM block (len may be short for the final block). Writes at most
// spb frames (interleaved, ch channels) to out. Returns the number of frames decoded.
inline int adpcm_decode_block(const uint8_t* blk, size_t len, int ch, int spb, int16_t* out)
{
    if (ch < 1 || ch > 2 || len < (size_t)(7 * ch) || spb < 2) return 0;
    int pred[2], delta[2], s1[2], s2[2];
    const uint8_t* p = blk;
    for (int c = 0; c < ch; c++) { pred[c] = *p++; if (pred[c] > 6) pred[c] = 6; }
    for (int c = 0; c < ch; c++) { delta[c] = rds16(p); p += 2; }
    for (int c = 0; c < ch; c++) { s1[c] = rds16(p); p += 2; }
    for (int c = 0; c < ch; c++) { s2[c] = rds16(p); p += 2; }
    for (int c = 0; c < ch; c++) { out[c] = (int16_t)s2[c]; out[ch + c] = (int16_t)s1[c]; }
    int frames = 2;
    size_t nbytes = len - 7 * ch;
    int maxFrames = (int)std::min<size_t>((size_t)spb, 2 + nbytes * 2 / ch);
    int16_t* o = out + 2 * ch;
    int c = 0;
    for (size_t i = 0; i < nbytes && frames < maxFrames; i++)
    {
        int b = p[i];
        for (int h = 0; h < 2; h++)
        {
            int nib = h == 0 ? (b >> 4) : (b & 15);
            int sn = nib >= 8 ? nib - 16 : nib;
            int pr = ((s1[c] * kAdpcmCoef1[pred[c]]) + (s2[c] * kAdpcmCoef2[pred[c]])) / 256; // truncating, as FAudio / ffmpeg / MS ACM
            pr += sn * delta[c];
            if (pr > 32767) pr = 32767; else if (pr < -32768) pr = -32768;
            s2[c] = s1[c]; s1[c] = pr;
            delta[c] = (kAdpcmAdapt[nib] * delta[c]) >> 8;
            if (delta[c] < 16) delta[c] = 16;
            *o++ = (int16_t)pr;
            if (++c == ch) { c = 0; if (++frames >= maxFrames) break; }
        }
    }
    return frames;
}

// ------------------------------------------------------------------ WAV
inline bool parse_wav(const uint8_t* d, size_t n, SoundData& out, std::string& err)
{
    if (n < 12 || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) { err = "not a RIFF/WAVE file"; return false; }
    const uint8_t* fmt = nullptr; uint32_t fmtLen = 0;
    const uint8_t* data = nullptr; uint32_t dataLen = 0;
    size_t pos = 12;
    while (pos + 8 <= n)
    {
        const uint8_t* id = d + pos;
        uint32_t sz = rd32(d + pos + 4);
        size_t body = pos + 8;
        size_t avail = n - body;
        if (memcmp(id, "fmt ", 4) == 0) { fmt = d + body; fmtLen = (uint32_t)std::min<size_t>(sz, avail); }
        else if (memcmp(id, "data", 4) == 0) { data = d + body; dataLen = (uint32_t)std::min<size_t>(sz, avail); }
        if (fmt && data) break;
        if ((uint64_t)sz + (sz & 1) > avail) break;
        pos = body + sz + (sz & 1);
    }
    if (!fmt || fmtLen < 16) { err = "WAV without fmt chunk"; return false; }
    if (!data) { err = "WAV without data chunk"; return false; }
    int tag = rd16(fmt), ch = rd16(fmt + 2);
    uint32_t rate = rd32(fmt + 4);
    int blockAlign = rd16(fmt + 12), bits = rd16(fmt + 14);
    if (tag == 0xFFFE && fmtLen >= 26) tag = rd16(fmt + 24); // EXTENSIBLE: subformat GUID starts with the tag
    if (ch < 1 || ch > 2 || rate == 0) { err = "unsupported WAV channel count / rate"; return false; }
    out.channels = ch;
    out.sampleRate = (int)rate;
    if (tag == 1 && bits == 16)
    {
        uint32_t frames = dataLen / (2 * ch);
        out.pcm.resize((size_t)frames * ch);
        for (size_t i = 0; i < out.pcm.size(); i++) out.pcm[i] = rds16(data + 2 * i);
        out.frames = frames;
    }
    else if (tag == 1 && bits == 8)
    {
        uint32_t frames = dataLen / ch;
        out.pcm.resize((size_t)frames * ch);
        for (size_t i = 0; i < out.pcm.size(); i++) out.pcm[i] = (int16_t)(((int)data[i] - 128) << 8);
        out.frames = frames;
    }
    else if (tag == 2)
    {
        if (blockAlign <= 7 * ch) { err = "bad MS-ADPCM block align"; return false; }
        int spb = (blockAlign / ch - 7) * 2 + 2;
        if (fmtLen >= 20) { int s = rd16(fmt + 18); if (s >= 2 && s <= spb) spb = s; }
        size_t nblocks = (dataLen + blockAlign - 1) / blockAlign;
        out.pcm.resize(nblocks * spb * ch);
        uint32_t frames = 0;
        for (size_t b = 0; b < nblocks; b++)
        {
            size_t off = b * blockAlign;
            size_t len = std::min<size_t>(blockAlign, dataLen - off);
            frames += adpcm_decode_block(data + off, len, ch, spb, &out.pcm[(size_t)frames * ch]);
        }
        out.pcm.resize((size_t)frames * ch);
        out.frames = frames;
    }
    else { err = "unsupported WAV format tag " + std::to_string(tag) + " / bits " + std::to_string(bits); return false; }
    return true;
}

// ------------------------------------------------------------------ XACT data
inline float xact_volume_db(int v)
{
    return (float)(((-96.0 - 67.7385212334047) / (1.0 + std::pow(v / 80.1748600297963, 0.432254984608615))) + 67.7385212334047);
}
inline float db_to_amp(float db) { return std::pow(10.0f, db / 20.0f); }

enum Behavior { B_FAIL = 0, B_QUEUE = 1, B_REPLACE_OLDEST = 2, B_REPLACE_QUIETEST = 3, B_REPLACE_LOWEST_PRIORITY = 4 };

struct WaveInfo
{
    uint32_t offset = 0, length = 0;   // absolute file offset / byte length of the data
    int formatTag = 0, channels = 0, rate = 0, bits = 16;
    int blockAlign = 0, spb = 0;       // bytes per block / frames per block (ADPCM)
    uint32_t frames = 0;               // duration in frames
    uint32_t loopStart = 0, loopLength = 0;
};

// The compressed bytes of one wave, loaded progressively by the main thread.
struct WaveBuffer
{
    WaveInfo info;
    int index = -1;
    int refs = 0;
    std::vector<uint8_t> bytes;        // full size allocated up front
    std::atomic<size_t> loaded{0};     // bytes [0, loaded) are valid
};

struct XEvent
{
    int type = 0;                      // 1 PlayWave, 3 TrackVariation, 4 EffectVariation, 6 TrackEffectVariation
    uint32_t timestampMs = 0, randomOffsetMs = 0;
    int loopCount = 0;                 // 255 = infinite
    int varType = 0;                   // 0 ordered 1 ordered-from-random 2 random 3 random-no-repeat 4 shuffle
    std::vector<uint16_t> waves;       // wave index in bank
    std::vector<uint8_t> banks;        // soundbank wavebank-name index
    int lastVar = -1;                  // variation state (main thread)
};
struct XTrack { float gain = 1.0f; std::vector<XEvent> events; };
struct XSound { int category = 0; float gain = 1.0f; std::vector<XTrack> tracks; };
struct XCue
{
    std::string name;
    int sound = -1;                    // index into XSoundBank::sounds, -1 = unsupported (variation table)
    int instanceLimit = 255, fadeInMs = 0, fadeOutMs = 0, behavior = B_FAIL;
};
struct XCategory
{
    std::string name;
    int instanceLimit = 255, fadeInMs = 0, fadeOutMs = 0, behavior = B_FAIL, parent = -1;
    float amp = 1.0f;                  // authored volume
    float setVolume = 1.0f;            // AudioCategory.SetVolume
};

// ------------------------------------------------------------------ mixer state
enum { V_FREE = 0, V_PLAYING = 1, V_PAUSED = 2 };
static const int kMaxVoices = 128;
static const int kMaxCategories = 16;
static const int kMaxMusic = 16;
static const int kMaxTracks = 4;
enum { TR_OK, TR_STALL, TR_END };

struct Voice
{
    const SoundData* data = nullptr;
    double pos = 0;
    float vol = 1, pan = 0, pitch = 0;
    bool loop = false, oneShot = false;
    uint8_t state = V_FREE;
    uint32_t gen = 0;
    uint64_t seq = 0;
};

struct MusicTrack
{
    WaveBuffer* wave = nullptr;
    float gain = 1.0f;
    int64_t delay = 0;                 // output frames before start
    int loopsLeft = 0;                 // 255 = infinite
    bool finished = false;
    uint32_t framePos = 0;
    int blkIndex = -1, blkFrames = 0;
    std::vector<int16_t> blk;          // decoded block (allocated by the main thread)
    float cur[2] = { 0, 0 }, nxt[2] = { 0, 0 };
    double frac = 1.0, step = 1.0;
};

struct MusicCue
{
    const void* owner = nullptr;       // XSoundBank*
    int cue = -1, category = -1;
    float gain = 1.0f;                 // sound volume
    MusicTrack tracks[kMaxTracks];
    int ntracks = 0;
    float fade = 1.0f, fadeStep = 0.0f;
    bool stopping = false, paused = false, finished = false;
    uint64_t seq = 0;
};

struct Mixer
{
    SDL_AudioDeviceID dev = 0;
    int rate = 48000;
    bool headless = true;              // no device: SFX voices are not started
    float sfxMaster = 1.0f;
    Voice voices[kMaxVoices];
    uint64_t seq = 0;
    float catGain[kMaxCategories];
    bool catPaused[kMaxCategories];
    MusicCue* music[kMaxMusic];
    int nmusic = 0;
    std::vector<float> mixbuf;
    Mixer() { for (int i = 0; i < kMaxCategories; i++) { catGain[i] = 1.0f; catPaused[i] = false; } for (auto& m : music) m = nullptr; }
};

inline Mixer& mixer() { static Mixer m; return m; }

struct Lock
{
    Lock() { if (mixer().dev) SDL_LockAudioDevice(mixer().dev); }
    ~Lock() { if (mixer().dev) SDL_UnlockAudioDevice(mixer().dev); }
};

// ------------------------------------------------------------------ voices (main thread API)
inline int make_handle(int idx, uint32_t gen) { return (int)(((gen & 0x7FFFFFu) << 8) | (uint32_t)idx); }
inline Voice* lookup(int h)
{
    if (h < 0) return nullptr;
    int idx = h & 0xFF;
    if (idx >= kMaxVoices) return nullptr;
    Voice& v = mixer().voices[idx];
    if (v.state == V_FREE || (v.gen & 0x7FFFFFu) != ((uint32_t)h >> 8)) return nullptr;
    return &v;
}

// Starts a voice. Returns its handle or -1.
inline int voice_play(const SoundData* d, float vol, float pitch, float pan, bool loop, bool oneShot)
{
    Mixer& m = mixer();
    if (!d || d->frames == 0 || m.headless) return -1;
    Lock lk;
    int idx = -1;
    for (int i = 0; i < kMaxVoices; i++) if (m.voices[i].state == V_FREE) { idx = i; break; }
    if (idx < 0)
    {
        // steal the oldest non-looping voice
        uint64_t best = UINT64_MAX;
        for (int i = 0; i < kMaxVoices; i++)
            if (!m.voices[i].loop && m.voices[i].seq < best) { best = m.voices[i].seq; idx = i; }
        if (idx < 0) return -1;
        m.voices[idx].gen++;
    }
    Voice& v = m.voices[idx];
    v.data = d; v.pos = 0; v.vol = vol; v.pitch = pitch; v.pan = pan; v.loop = loop; v.oneShot = oneShot;
    v.seq = ++m.seq;
    v.state = V_PLAYING;
    return make_handle(idx, v.gen);
}
inline void voice_stop(int h) { Lock lk; if (Voice* v = lookup(h)) { v->state = V_FREE; v->gen++; } }
inline void voice_pause(int h) { Lock lk; if (Voice* v = lookup(h)) if (v->state == V_PLAYING) v->state = V_PAUSED; }
inline void voice_resume(int h) { Lock lk; if (Voice* v = lookup(h)) if (v->state == V_PAUSED) v->state = V_PLAYING; }
// 0 Playing, 1 Paused, 2 Stopped (XNA SoundState)
inline int voice_state(int h) { Lock lk; Voice* v = lookup(h); return !v ? 2 : v->state == V_PAUSED ? 1 : 0; }
inline void voice_set(int h, float vol, float pitch, float pan) { Lock lk; if (Voice* v = lookup(h)) { v->vol = vol; v->pitch = pitch; v->pan = pan; } }
inline void voice_set_loop(int h, bool loop) { Lock lk; if (Voice* v = lookup(h)) v->loop = loop; }

// ------------------------------------------------------------------ rendering (audio thread)
inline void mix_voice(Mixer& m, Voice& v, float* out, int frames)
{
    const SoundData* d = v.data;
    const int ch = d->channels;
    const int16_t* s = d->pcm.data();
    const double n = (double)d->frames;
    double step = (double)d->sampleRate / m.rate * std::exp2((double)std::clamp(v.pitch, -1.0f, 1.0f));
    float vol = std::max(0.0f, v.vol) * m.sfxMaster * (1.0f / 32768.0f);
    float pan = std::clamp(v.pan, -1.0f, 1.0f);
    // FNA pan matrix
    float ll, rl, lr, rr; // src->dst
    if (ch == 1) { ll = pan > 0 ? 1 - pan : 1; lr = pan < 0 ? 1 + pan : 1; rl = rr = 0; }
    else if (pan <= 0) { ll = 1; rl = -pan; lr = 0; rr = 1 + pan; }
    else { ll = 1 - pan; rl = 0; lr = pan; rr = 1; }
    ll *= vol; rl *= vol; lr *= vol; rr *= vol;
    double pos = v.pos;
    for (int i = 0; i < frames; i++)
    {
        if (pos >= n)
        {
            if (v.loop) { pos = std::fmod(pos, n); }
            else { v.state = V_FREE; v.gen++; return; }
        }
        uint32_t i0 = (uint32_t)pos;
        uint32_t i1 = i0 + 1;
        if (i1 >= d->frames) i1 = v.loop ? 0 : i0;
        float f = (float)(pos - i0);
        float a, b;
        if (ch == 1)
        {
            a = s[i0] + (s[i1] - s[i0]) * f;
            out[2 * i] += a * ll; out[2 * i + 1] += a * lr;
        }
        else
        {
            a = s[2 * i0] + (s[2 * i1] - s[2 * i0]) * f;
            b = s[2 * i0 + 1] + (s[2 * i1 + 1] - s[2 * i0 + 1]) * f;
            out[2 * i] += a * ll + b * rl;
            out[2 * i + 1] += a * lr + b * rr;
        }
        pos += step;
    }
    v.pos = pos;
}

// Produces the next source frame of a music track.
inline int track_read(MusicTrack& t, float* fr)
{
    WaveBuffer* w = t.wave;
    const WaveInfo& wi = w->info;
    bool loopRegion = wi.loopLength > 0 && wi.loopStart + wi.loopLength <= wi.frames;
    uint32_t end = (t.loopsLeft > 0 && loopRegion) ? wi.loopStart + wi.loopLength : wi.frames;
    if (t.framePos >= end)
    {
        if (t.loopsLeft > 0)
        {
            if (t.loopsLeft != 255) t.loopsLeft--;
            t.framePos = loopRegion ? wi.loopStart : 0;
        }
        else return TR_END;
    }
    const int ch = wi.channels;
    size_t loaded = w->loaded.load(std::memory_order_acquire);
    if (wi.formatTag == 2)
    {
        int b = (int)(t.framePos / wi.spb);
        if (b != t.blkIndex)
        {
            size_t off = (size_t)b * wi.blockAlign;
            if (off >= wi.length) return TR_END;
            size_t len = std::min<size_t>(wi.blockAlign, wi.length - off);
            if (off + len > loaded) return TR_STALL;
            t.blkFrames = adpcm_decode_block(w->bytes.data() + off, len, ch, wi.spb, t.blk.data());
            t.blkIndex = b;
        }
        int k = (int)(t.framePos % wi.spb);
        if (k >= t.blkFrames) return TR_END;
        const int16_t* s = &t.blk[(size_t)k * ch];
        fr[0] = s[0] * (1.0f / 32768.0f);
        fr[1] = (ch == 2 ? s[1] : s[0]) * (1.0f / 32768.0f);
    }
    else if (wi.formatTag == 0)
    {
        int bps = wi.bits / 8;
        size_t off = (size_t)t.framePos * ch * bps;
        if (off + ch * bps > wi.length) return TR_END;
        if (off + ch * bps > loaded) return TR_STALL;
        const uint8_t* p = w->bytes.data() + off;
        for (int c = 0; c < 2; c++)
        {
            const uint8_t* q = p + (ch == 2 ? c : 0) * bps;
            fr[c] = bps == 2 ? rds16(q) * (1.0f / 32768.0f) : ((int)q[0] - 128) * (1.0f / 128.0f);
        }
    }
    else return TR_END;
    t.framePos++;
    return TR_OK;
}

inline void mix_track(MusicTrack& t, float* out, int frames, float g0, float fadeStep, float& fadeOut)
{
    float fade = g0;
    int i = 0;
    if (t.delay > 0)
    {
        int64_t skip = std::min<int64_t>(t.delay, frames);
        t.delay -= skip; i = (int)skip;
        fade += fadeStep * (float)skip;
    }
    for (; i < frames; i++)
    {
        while (t.frac >= 1.0)
        {
            float fr[2];
            int r = track_read(t, fr);
            if (r == TR_STALL) { fadeOut = fade; return; }
            if (r == TR_END) { t.finished = true; fadeOut = fade; return; }
            t.cur[0] = t.nxt[0]; t.cur[1] = t.nxt[1];
            t.nxt[0] = fr[0]; t.nxt[1] = fr[1];
            t.frac -= 1.0;
        }
        float f = (float)t.frac;
        float g = std::clamp(fade, 0.0f, 1.0f) * t.gain;
        out[2 * i] += (t.cur[0] + (t.nxt[0] - t.cur[0]) * f) * g;
        out[2 * i + 1] += (t.cur[1] + (t.nxt[1] - t.cur[1]) * f) * g;
        t.frac += t.step;
        fade += fadeStep;
    }
    fadeOut = fade;
}

inline void mix_cue(Mixer& m, MusicCue& c, float* out, int frames)
{
    if (c.finished || c.paused) return;
    if (c.category >= 0 && c.category < kMaxCategories && m.catPaused[c.category]) return;
    float catg = (c.category >= 0 && c.category < kMaxCategories) ? m.catGain[c.category] : 1.0f;
    float g = catg * c.gain;
    // fades are evaluated per frame inside mix_track; the cue gain is folded into track gain temporarily
    bool anyAlive = false;
    for (int k = 0; k < c.ntracks; k++)
    {
        MusicTrack& t = c.tracks[k];
        if (t.finished) continue;
        float save = t.gain;
        t.gain = save * g;
        float dummy;
        mix_track(t, out, frames, c.fade, c.fadeStep, dummy);
        t.gain = save;
        if (!t.finished) anyAlive = true;
    }
    c.fade += c.fadeStep * frames;
    if (c.fadeStep > 0 && c.fade >= 1.0f) { c.fade = 1.0f; c.fadeStep = 0; }
    if (c.fadeStep < 0 && c.fade <= 0.0f) { c.fade = 0.0f; c.fadeStep = 0; if (c.stopping) c.finished = true; }
    if (!anyAlive) c.finished = true;
}

// Renders interleaved stereo float. Must be called with the device locked (SDL does it for the callback).
inline void render(float* out, int frames)
{
    Mixer& m = mixer();
    memset(out, 0, sizeof(float) * 2 * frames);
    for (int i = 0; i < kMaxVoices; i++)
        if (m.voices[i].state == V_PLAYING) mix_voice(m, m.voices[i], out, frames);
    for (int i = 0; i < m.nmusic; i++)
        if (m.music[i]) mix_cue(m, *m.music[i], out, frames);
}

inline void sdl_callback(void*, Uint8* stream, int len)
{
    Mixer& m = mixer();
    int16_t* o = (int16_t*)stream;
    int total = len / 4;
    int cap = (int)(m.mixbuf.size() / 2);
    while (total > 0)
    {
        int n = std::min(total, cap);
        render(m.mixbuf.data(), n);
        for (int i = 0; i < 2 * n; i++)
        {
            float v = m.mixbuf[i] * 32767.0f;
            o[i] = (int16_t)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : (int)v);
        }
        o += 2 * n; total -= n;
    }
}

// Opens the SDL audio device. Returns false (and stays headless) on failure.
inline bool open_device()
{
    Mixer& m = mixer();
    if (m.dev) return true;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        fprintf(stderr, "[audio] SDL audio init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = sdl_callback;
    m.mixbuf.assign(2 * 4096, 0.0f);
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (!dev)
    {
        fprintf(stderr, "[audio] SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return false;
    }
    m.rate = have.freq;
    m.headless = false;
    m.dev = dev;
    SDL_PauseAudioDevice(dev, 0);
    return true;
}

inline void close_device()
{
    Mixer& m = mixer();
    if (!m.dev) return;
    SDL_AudioDeviceID d = m.dev;
    SDL_LockAudioDevice(d);
    m.dev = 0;
    m.headless = true;
    for (auto& v : m.voices) { v.state = V_FREE; v.gen++; }
    SDL_UnlockAudioDevice(d);
    SDL_CloseAudioDevice(d);
}

// ------------------------------------------------------------------ XACT banks (main thread)
struct Rd
{
    const uint8_t* d; size_t n; size_t p;
    bool ok = true;
    Rd(const uint8_t* d_, size_t n_, size_t p_ = 0) : d(d_), n(n_), p(p_) {}
    bool need(size_t k) { if (p + k > n) { ok = false; return false; } return true; }
    uint8_t u8() { if (!need(1)) return 0; return d[p++]; }
    uint16_t u16() { if (!need(2)) return 0; uint16_t v = rd16(d + p); p += 2; return v; }
    int16_t s16() { return (int16_t)u16(); }
    uint32_t u32() { if (!need(4)) return 0; uint32_t v = rd32(d + p); p += 4; return v; }
    void skip(size_t k) { p += k; }
};

inline std::string cstr_at(const uint8_t* d, size_t n, size_t off, size_t maxLen)
{
    std::string s;
    for (size_t i = off; i < n && i < off + maxLen && d[i]; i++) s += (char)d[i];
    return s;
}

struct XWaveBank
{
    std::string name;
    FILE* f = nullptr;
    std::vector<WaveInfo> waves;
    std::vector<WaveBuffer*> cache;

    bool open(const std::string& path, std::string& err)
    {
        f = fopen(path.c_str(), "rb");
        if (!f) { err = "cannot open " + path; return false; }
        uint8_t hdr[52];
        if (fread(hdr, 1, 52, f) != 52 || memcmp(hdr, "WBND", 4) != 0) { err = "not a little-endian XWB"; return false; }
        uint32_t segOff[5], segLen[5];
        for (int i = 0; i < 5; i++) { segOff[i] = rd32(hdr + 12 + 8 * i); segLen[i] = rd32(hdr + 16 + 8 * i); }
        std::vector<uint8_t> bd(segLen[0]);
        if (segLen[0] < 96 || !readAt(segOff[0], bd.data(), bd.size())) { err = "bad XWB bank data"; return false; }
        uint32_t flags = rd32(&bd[0]);
        uint32_t count = rd32(&bd[4]);
        name = cstr_at(bd.data(), bd.size(), 8, 64);
        uint32_t metaSize = rd32(&bd[72]);
        if (flags & 0x20000) { err = "compact XWB not supported"; return false; }
        if (metaSize < 24) { err = "bad XWB entry size"; return false; }
        std::vector<uint8_t> meta((size_t)metaSize * count);
        if (!readAt(segOff[1], meta.data(), meta.size())) { err = "bad XWB entry table"; return false; }
        waves.resize(count);
        cache.assign(count, nullptr);
        for (uint32_t i = 0; i < count; i++)
        {
            const uint8_t* e = &meta[(size_t)i * metaSize];
            uint32_t fd = rd32(e), fmt = rd32(e + 4);
            WaveInfo& w = waves[i];
            w.formatTag = fmt & 3;
            w.channels = (fmt >> 2) & 7;
            w.rate = (fmt >> 5) & 0x3FFFF;
            int ba = (fmt >> 23) & 0xFF;
            w.bits = (fmt >> 31) ? 16 : 8;
            w.frames = fd >> 4;
            w.offset = segOff[4] + rd32(e + 8);
            w.length = rd32(e + 12);
            w.loopStart = rd32(e + 16);
            w.loopLength = rd32(e + 20);
            if (w.formatTag == 2)
            {
                w.blockAlign = (ba + 22) * w.channels;
                w.spb = (w.blockAlign / w.channels - 7) * 2 + 2;
                w.bits = 4;
            }
            else w.blockAlign = ba;
        }
        return true;
    }
    bool readAt(uint32_t off, void* dst, size_t len)
    {
        if (len == 0) return true;
        if (fseek(f, (long)off, SEEK_SET) != 0) return false;
        return fread(dst, 1, len, f) == len;
    }
    // Returns a (possibly partially loaded) buffer of a wave; loads the first chunk now.
    WaveBuffer* acquire(int idx)
    {
        if (idx < 0 || idx >= (int)waves.size()) return nullptr;
        const WaveInfo& wi = waves[idx];
        if (wi.channels < 1 || wi.channels > 2 || wi.rate <= 0 || (wi.formatTag != 0 && wi.formatTag != 2)) return nullptr;
        if (wi.formatTag == 2 && wi.spb <= 2) return nullptr;
        if (!cache[idx])
        {
            WaveBuffer* b = new WaveBuffer();
            b->info = wi; b->index = idx;
            b->bytes.resize(wi.length);
            cache[idx] = b;
            loadMore(b, 256 * 1024);
        }
        cache[idx]->refs++;
        return cache[idx];
    }
    void release(WaveBuffer* b)
    {
        if (!b) return;
        if (--b->refs <= 0) { cache[b->index] = nullptr; delete b; }
    }
    size_t loadMore(WaveBuffer* b, size_t budget)
    {
        size_t have = b->loaded.load(std::memory_order_relaxed);
        if (have >= b->info.length) return 0;
        size_t n = std::min(budget, (size_t)b->info.length - have);
        if (fseek(f, (long)(b->info.offset + have), SEEK_SET) != 0) return 0;
        size_t got = fread(b->bytes.data() + have, 1, n, f);
        if (got == 0) { b->loaded.store(b->info.length, std::memory_order_release); return 0; } // truncated file: give up
        b->loaded.store(have + got, std::memory_order_release);
        return got;
    }
    // Streams more of the partially loaded waves (main thread), within budget bytes
    void update(size_t& budget)
    {
        for (WaveBuffer* b : cache)
            if (b && budget > 0) { size_t got = loadMore(b, budget); budget -= std::min(budget, got); }
    }
};

struct XactEngine
{
    std::vector<XCategory> cats;
    std::vector<XWaveBank*> banks;
    std::vector<MusicCue*> playing;   // main-thread mirror of mixer().music
    uint32_t rng = 0x12345678u;

    uint32_t rand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

    bool load(const uint8_t* d, size_t n, std::string& err)
    {
        if (n < 4 || memcmp(d, "XGSF", 4) != 0) { err = "not an XGS"; return false; }
        Rd r(d, n, 4);
        r.skip(6 + 8 + 1);
        int ncat = r.u16(); r.u16(); r.skip(4); r.u16(); r.u16(); r.u16();
        uint32_t offCat = r.u32(); r.u32(); r.skip(4); uint32_t offCatIdx = r.u32();
        if (!r.ok || ncat > kMaxCategories) { err = "bad XGS header"; return false; }
        cats.resize(ncat);
        Rd c(d, n, offCat);
        for (int i = 0; i < ncat; i++)
        {
            XCategory& k = cats[i];
            k.instanceLimit = c.u8(); k.fadeInMs = c.u16(); k.fadeOutMs = c.u16();
            k.behavior = c.u8() >> 3;
            int parent = c.u16(); k.parent = parent == 0xFFFF ? -1 : parent;
            k.amp = db_to_amp(xact_volume_db(c.u8()));
            c.u8();
            uint32_t noff = (offCatIdx + 6 * i + 4 <= n) ? rd32(d + offCatIdx + 6 * i) : (uint32_t)n;
            k.name = cstr_at(d, n, noff, 256);
        }
        if (!c.ok) { err = "truncated XGS"; return false; }
        rng ^= SDL_GetTicks() * 2654435761u;
        if (!rng) rng = 1;
        Lock lk;
        for (int i = 0; i < ncat; i++) mixer().catGain[i] = cats[i].amp * cats[i].setVolume;
        return true;
    }
    int findCategory(const std::string& name) const
    {
        for (size_t i = 0; i < cats.size(); i++) if (cats[i].name == name) return (int)i;
        return -1;
    }
    XWaveBank* findBank(const std::string& name) const
    {
        for (XWaveBank* b : banks) if (b->name == name) return b;
        return banks.size() == 1 ? banks[0] : nullptr;
    }
    void setCategoryVolume(int cat, float v)
    {
        if (cat < 0 || cat >= (int)cats.size()) return;
        cats[cat].setVolume = std::max(0.0f, v);
        Lock lk;
        // FACT: currentVolume = authored volume * SetVolume; children follow their parent
        for (size_t i = 0; i < cats.size(); i++)
        {
            float g = cats[i].amp * cats[i].setVolume;
            mixer().catGain[i] = g;
        }
    }
    static void beginFadeOut(MusicCue* c, int ms, int rate)
    {
        if (c->finished) return;
        if (ms <= 0) { c->finished = true; return; }
        c->stopping = true;
        c->fadeStep = -1.0f / (ms * 0.001f * rate);
    }
    void stopCategory(int cat, bool immediate)
    {
        Lock lk;
        int rate = mixer().rate;
        for (MusicCue* c : playing)
            if (c->category == cat || cat < 0)
            {
                if (immediate) c->finished = true;
                else beginFadeOut(c, cat >= 0 && cat < (int)cats.size() ? cats[cat].fadeOutMs : 0, rate);
            }
    }
    void pauseCategory(int cat, bool p)
    {
        if (cat < 0 || cat >= kMaxCategories) return;
        Lock lk;
        mixer().catPaused[cat] = p;
    }
    // Removes finished cues and streams more wave data. Call every frame.
    void update()
    {
        std::vector<MusicCue*> dead;
        {
            Lock lk;
            Mixer& m = mixer();
            int k = 0;
            for (int i = 0; i < m.nmusic; i++)
            {
                if (m.music[i]->finished) dead.push_back(m.music[i]);
                else m.music[k++] = m.music[i];
            }
            for (int i = k; i < m.nmusic; i++) m.music[i] = nullptr;
            m.nmusic = k;
        }
        for (MusicCue* c : dead)
        {
            playing.erase(std::remove(playing.begin(), playing.end(), c), playing.end());
            releaseCue(c);
        }
        size_t budget = 512 * 1024;
        for (XWaveBank* b : banks) b->update(budget);
    }
    void releaseCue(MusicCue* c)
    {
        for (int k = 0; k < c->ntracks; k++)
            if (c->tracks[k].wave)
                for (XWaveBank* b : banks)
                    if (b->cache.size() > (size_t)c->tracks[k].wave->index && b->cache[c->tracks[k].wave->index] == c->tracks[k].wave) { b->release(c->tracks[k].wave); break; }
        delete c;
    }
};

struct XSoundBank
{
    XactEngine* eng = nullptr;
    std::string name;
    std::vector<std::string> wbNames;
    std::vector<XSound> sounds;
    std::vector<XCue> cues;

    static bool parseEvents(const uint8_t* d, size_t n, uint32_t off, std::vector<XEvent>& out)
    {
        Rd r(d, n, off);
        int cnt = r.u8();
        for (int i = 0; i < cnt && r.ok; i++)
        {
            XEvent ev;
            uint32_t info = r.u32();
            ev.randomOffsetMs = r.u16();
            ev.type = info & 0x1F;
            ev.timestampMs = (info >> 5) & 0xFFFF;
            r.skip(1);
            if (ev.type == 1)
            {
                r.u8(); uint16_t tr = r.u16(); uint8_t wb = r.u8();
                ev.loopCount = r.u8(); r.u16(); r.u16();
                ev.waves.push_back(tr); ev.banks.push_back(wb);
            }
            else if (ev.type == 3 || ev.type == 4 || ev.type == 6)
            {
                r.u8();
                ev.loopCount = r.u8(); r.u16(); r.u16();
                if (ev.type != 3)
                {
                    if (ev.type == 4) { uint16_t tr = r.u16(); uint8_t wb = r.u8(); ev.waves.push_back(tr); ev.banks.push_back(wb); }
                    r.skip(2 + 2 + 1 + 1 + 16 + 2 + 1); // pitch/volume/filter variation (ignored)
                }
                if (ev.type == 3 || ev.type == 6)
                {
                    int nv = r.u16(); int vt = r.u16(); r.skip(4);
                    ev.varType = vt & 0xF;
                    for (int k = 0; k < nv && r.ok; k++)
                    {
                        uint16_t tr = r.u16(); uint8_t wb = r.u8(); r.u8(); r.u8();
                        ev.waves.push_back(tr); ev.banks.push_back(wb);
                    }
                }
            }
            else if (ev.type == 0) { r.u8(); }
            else { out.push_back(ev); break; } // unknown layout: stop parsing this track
            if (r.ok) out.push_back(ev);
        }
        return r.ok;
    }

    int parseSound(const uint8_t* d, size_t n, uint32_t off)
    {
        Rd s(d, n, off);
        int flags = s.u8();
        XSound snd;
        snd.category = s.u16();
        snd.gain = db_to_amp(xact_volume_db(s.u8()));
        s.s16(); s.u8(); s.skip(2);
        int ntracks = (flags & 1) ? s.u8() : 1;
        if (flags & 0x0E) { int ln = s.u16(); s.skip(ln - 2); }
        if (flags & 0x10) { int ln = s.u16(); s.skip(ln - 2); }
        if (flags & 1)
        {
            for (int t = 0; t < ntracks && s.ok; t++)
            {
                XTrack tr;
                int tv = s.u8(); uint32_t evoff = s.u32(); s.u8(); s.u8(); s.u16();
                tr.gain = db_to_amp(xact_volume_db(tv));
                parseEvents(d, n, evoff, tr.events);
                snd.tracks.push_back(std::move(tr));
            }
        }
        else
        {
            XTrack tr;
            XEvent ev; ev.type = 1; ev.loopCount = 0;
            ev.waves.push_back(s.u16()); ev.banks.push_back(s.u8());
            tr.events.push_back(ev);
            snd.tracks.push_back(std::move(tr));
        }
        if (!s.ok) return -1;
        sounds.push_back(std::move(snd));
        return (int)sounds.size() - 1;
    }

    bool load(const uint8_t* d, size_t n, std::string& err)
    {
        if (n < 4 || memcmp(d, "SDBK", 4) != 0) { err = "not an XSB"; return false; }
        Rd r(d, n, 4);
        r.u16(); r.u16(); r.u16(); r.skip(8); r.u8();
        int nSimple = r.u16(), nComplex = r.u16(); r.skip(2);
        r.u16(); int nWb = r.u8(); r.u16();
        r.u16(); r.skip(2);
        uint32_t offSimple = r.u32(), offComplex = r.u32(); r.u32(); r.skip(4);
        r.u32(); r.skip(4);
        uint32_t offWbNames = r.u32(); r.u32(); uint32_t offNameIdx = r.u32(); r.u32();
        if (!r.ok) { err = "bad XSB header"; return false; }
        name = cstr_at(d, n, r.p, 64);
        for (int i = 0; i < nWb; i++) wbNames.push_back(cstr_at(d, n, offWbNames + 64 * i, 64));
        for (int i = 0; i < nSimple; i++)
        {
            Rd c(d, n, offSimple + 5 * i);
            XCue cue;
            c.u8(); uint32_t so = c.u32();
            if (!c.ok) { err = "truncated XSB cue table"; return false; }
            cue.sound = parseSound(d, n, so);
            cues.push_back(cue);
        }
        for (int i = 0; i < nComplex; i++)
        {
            Rd c(d, n, offComplex + 15 * i);
            XCue cue;
            int fl = c.u8();
            uint32_t so = c.u32();
            c.skip(4);
            cue.instanceLimit = c.u8();
            cue.fadeInMs = c.u16(); cue.fadeOutMs = c.u16();
            cue.behavior = c.u8() >> 3;
            if (!c.ok) { err = "truncated XSB cue table"; return false; }
            cue.sound = (fl & 0x04) ? parseSound(d, n, so) : -1;
            cues.push_back(cue);
        }
        for (size_t i = 0; i < cues.size(); i++)
        {
            if (offNameIdx + 6 * i + 4 > n) break;
            cues[i].name = cstr_at(d, n, rd32(d + offNameIdx + 6 * i), 256);
        }
        return true;
    }

    int findCue(const std::string& nm) const
    {
        for (size_t i = 0; i < cues.size(); i++) if (cues[i].name == nm) return (int)i;
        return -1;
    }

    int pickVariation(XEvent& ev)
    {
        int nv = (int)ev.waves.size();
        if (nv <= 1) return 0;
        int k;
        switch (ev.varType)
        {
        case 0: k = (ev.lastVar + 1) % nv; break;
        case 1: k = ev.lastVar < 0 ? (int)(eng->rand() % nv) : (ev.lastVar + 1) % nv; break;
        case 2: k = (int)(eng->rand() % nv); break;
        default: // random without immediate repeat / shuffle
            k = (int)(eng->rand() % nv);
            if (ev.lastVar >= 0 && k == ev.lastVar) k = (k + 1 + (int)(eng->rand() % (nv - 1))) % nv;
            break;
        }
        ev.lastVar = k;
        return k;
    }

    // Plays a cue with XACT instance-limit semantics (crossfade on replace). Returns false if not played.
    bool playCue(int ci)
    {
        if (!eng || ci < 0 || ci >= (int)cues.size()) return false;
        XCue& cue = cues[ci];
        if (cue.sound < 0) return false;
        XSound& snd = sounds[cue.sound];
        Mixer& m = mixer();
        const int rate = m.rate;

        MusicCue* mc = new MusicCue();
        mc->owner = this; mc->cue = ci;
        mc->category = snd.category < (int)eng->cats.size() ? snd.category : -1;
        mc->gain = snd.gain;
        for (XTrack& tr : snd.tracks)
        {
            if (mc->ntracks >= kMaxTracks) break;
            for (XEvent& ev : tr.events)
            {
                if (ev.type != 1 && ev.type != 3 && ev.type != 4 && ev.type != 6) continue;
                if (ev.waves.empty()) break;
                int k = pickVariation(ev);
                int bi = ev.banks[k];
                XWaveBank* bank = eng->findBank(bi < (int)wbNames.size() ? wbNames[bi] : std::string());
                WaveBuffer* wb = bank ? bank->acquire(ev.waves[k]) : nullptr;
                if (!wb) break;
                MusicTrack& t = mc->tracks[mc->ntracks++];
                t.wave = wb;
                t.gain = tr.gain;
                uint32_t ms = ev.timestampMs + (ev.randomOffsetMs ? eng->rand() % (ev.randomOffsetMs + 1) : 0);
                t.delay = (int64_t)ms * rate / 1000;
                t.loopsLeft = ev.loopCount;
                t.step = (double)wb->info.rate / rate;
                if (wb->info.formatTag == 2) t.blk.assign((size_t)wb->info.spb * wb->info.channels, 0);
                break; // one play event per track
            }
        }
        if (mc->ntracks == 0) { delete mc; return false; }

        Lock lk;
        // cue instance limit (complex cues)
        bool replaced = false;
        int fadeInMs = 0;
        auto liveCount = [&](auto pred, MusicCue*& oldest) {
            int cnt = 0; oldest = nullptr;
            for (MusicCue* c : eng->playing)
                if (!c->finished && !c->stopping && pred(c)) { cnt++; if (!oldest || c->seq < oldest->seq) oldest = c; }
            return cnt;
        };
        MusicCue* oldest = nullptr;
        if (cue.instanceLimit < 255 && liveCount([&](MusicCue* c) { return c->owner == this && c->cue == ci; }, oldest) >= cue.instanceLimit)
        {
            if (cue.behavior == B_FAIL || cue.behavior == B_QUEUE) { releaseUnplayed(mc); return false; }
            XactEngine::beginFadeOut(oldest, cue.fadeOutMs, rate);
            replaced = true; fadeInMs = cue.fadeInMs;
        }
        if (mc->category >= 0)
        {
            XCategory& cat = eng->cats[mc->category];
            if (cat.instanceLimit < 255 && liveCount([&](MusicCue* c) { return c->category == mc->category; }, oldest) >= cat.instanceLimit)
            {
                if (cat.behavior == B_FAIL || cat.behavior == B_QUEUE) { releaseUnplayed(mc); return false; }
                XactEngine::beginFadeOut(oldest, cat.fadeOutMs, rate);
                replaced = true; fadeInMs = std::max(fadeInMs, cat.fadeInMs);
            }
        }
        if (m.nmusic >= kMaxMusic)
        {
            // should not happen (finished cues are reaped in update); drop the oldest hard
            MusicCue* o = nullptr;
            for (int i = 0; i < m.nmusic; i++) if (!o || m.music[i]->seq < o->seq) o = m.music[i];
            if (o) o->finished = true;
            releaseUnplayed(mc);
            return false;
        }
        if (replaced && fadeInMs > 0) { mc->fade = 0.0f; mc->fadeStep = 1.0f / (fadeInMs * 0.001f * rate); }
        mc->seq = ++m.seq;
        m.music[m.nmusic++] = mc;
        eng->playing.push_back(mc);
        return true;
    }

    void releaseUnplayed(MusicCue* mc) { eng->releaseCue(mc); }
};

} // namespace xaudio
