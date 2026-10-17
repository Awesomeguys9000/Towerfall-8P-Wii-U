// Standalone test of runtime/xna/audio_core.h (no GC runtime).
//
// Build (macOS):
//   clang++ -std=c++20 -O2 -I../runtime -I/opt/homebrew/include/SDL2 audio_test.cpp -L/opt/homebrew/lib -lSDL2 -o /tmp/audio_test
// Usage:
//   audio_test <TF-8P dir> <out.wav>          offline render of a scripted music/SFX session to out.wav
//   audio_test <TF-8P dir> --decode N out.raw  decode XWB wave N to raw s16le (compare with ffmpeg)
//   audio_test <TF-8P dir> --sfx              parse every SFX wav
//   audio_test <TF-8P dir> --play             play live through the SDL device for a few seconds
#include "xna/audio_core.h"
#include <dirent.h>
#include <functional>

using namespace xaudio;

static bool read_all(const std::string& p, std::vector<uint8_t>& out)
{
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.resize(n);
    bool ok = fread(out.data(), 1, n, f) == (size_t)n;
    fclose(f);
    return ok;
}

static void write_wav(const std::string& p, const std::vector<int16_t>& s, int rate)
{
    FILE* f = fopen(p.c_str(), "wb");
    auto w32 = [&](uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; fwrite(b, 1, 4, f); };
    auto w16 = [&](uint16_t v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; fwrite(b, 1, 2, f); };
    uint32_t dl = (uint32_t)s.size() * 2;
    fwrite("RIFF", 1, 4, f); w32(36 + dl); fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(2); w32(rate); w32(rate * 4); w16(4); w16(16);
    fwrite("data", 1, 4, f); w32(dl);
    for (int16_t v : s) w16((uint16_t)v);
    fclose(f);
}

struct Session
{
    XactEngine eng;
    XWaveBank* wb = new XWaveBank();
    XSoundBank sb;
    bool init(const std::string& root)
    {
        std::string dir = root + "/Content/Music/Win/";
        std::vector<uint8_t> d; std::string err;
        if (!read_all(dir + "TowerFallMusic.xgs", d) || !eng.load(d.data(), d.size(), err)) { fprintf(stderr, "xgs: %s\n", err.c_str()); return false; }
        if (!read_all(dir + "MusicSoundBank.xsb", d) || !sb.load(d.data(), d.size(), err)) { fprintf(stderr, "xsb: %s\n", err.c_str()); return false; }
        sb.eng = &eng;
        if (!wb->open(dir + "MusicWaveBank.xwb", err)) { fprintf(stderr, "xwb: %s\n", err.c_str()); return false; }
        eng.banks.push_back(wb);
        printf("categories:"); for (auto& c : eng.cats) printf(" %s(limit %d, fade %d/%d, beh %d, amp %.4f)", c.name.c_str(), c.instanceLimit, c.fadeInMs, c.fadeOutMs, c.behavior, c.amp); printf("\n");
        printf("wavebank '%s' %zu waves; soundbank '%s' %zu cues\n", wb->name.c_str(), wb->waves.size(), sb.name.c_str(), sb.cues.size());
        for (auto& c : sb.cues)
        {
            const XSound& s = sb.sounds[c.sound];
            printf("  %-18s cat %d gain %.4f lim %d beh %d |", c.name.c_str(), s.category, s.gain, c.instanceLimit, c.behavior);
            for (auto& t : s.tracks) for (auto& e : t.events) { printf(" [t%u loop%d g%.3f w", e.timestampMs, e.loopCount, t.gain); for (auto w : e.waves) printf(" %u", w); printf("]"); }
            printf("\n");
        }
        return true;
    }
    void play(const char* name)
    {
        int ci = sb.findCue(name);
        bool ok = sb.playCue(ci);
        printf("PlayCue %s -> %d (%s)\n", name, ci, ok ? "ok" : "FAILED");
    }
};

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "usage: see source\n"); return 1; }
    std::string root = argv[1], mode = argv[2];
    SDL_SetMainReady();

    if (mode == "--sfx")
    {
        std::string dir = root + "/Content/SFX/";
        int ok = 0, bad = 0; size_t bytes = 0;
        std::vector<std::string> files;
        std::function<void(const std::string&)> walk = [&](const std::string& d) {
            DIR* dp = opendir(d.c_str()); if (!dp) return;
            while (dirent* e = readdir(dp))
            {
                std::string n = e->d_name; if (n[0] == '.') continue;
                if (e->d_type == DT_DIR) walk(d + n + "/");
                else if (n.size() > 4 && n.substr(n.size() - 4) == ".wav") files.push_back(d + n);
            }
            closedir(dp);
        };
        walk(dir);
        int mono = 0, stereo = 0; std::vector<int> rates;
        for (auto& f : files)
        {
            std::vector<uint8_t> d; std::string err; SoundData s;
            if (!read_all(f, d) || !parse_wav(d.data(), d.size(), s, err)) { bad++; printf("FAIL %s: %s\n", f.c_str(), err.c_str()); continue; }
            ok++; bytes += s.pcm.size() * 2; (s.channels == 1 ? mono : stereo)++;
            if (std::find(rates.begin(), rates.end(), s.sampleRate) == rates.end()) rates.push_back(s.sampleRate);
        }
        printf("sfx: %d ok, %d failed, mono %d stereo %d, %.1f MB decoded, rates:", ok, bad, mono, stereo, bytes / 1048576.0);
        for (int r : rates) printf(" %d", r); printf("\n");
        return bad ? 1 : 0;
    }

    Session s;
    if (!s.init(root)) return 1;
    Mixer& m = mixer();

    if (mode == "--decode")
    {
        int idx = atoi(argv[3]);
        WaveBuffer* b = s.wb->acquire(idx);
        while (b->loaded < b->info.length) s.wb->loadMore(b, 1 << 20);
        MusicTrack t; t.wave = b; t.loopsLeft = argc > 5 ? atoi(argv[5]) : 0;
        int loops = t.loopsLeft; t.blk.assign((size_t)b->info.spb * b->info.channels, 0);
        std::vector<int16_t> out; float fr[2];
        while (track_read(t, fr) == TR_OK) { out.push_back((int16_t)lrintf(fr[0] * 32768.0f)); out.push_back((int16_t)lrintf(fr[1] * 32768.0f)); }
        FILE* f = fopen(argv[4], "wb"); fwrite(out.data(), 2, out.size(), f); fclose(f);
        printf("wave %d: %zu frames decoded (header says %u), rate %d, loop %u+%u\n", idx, out.size() / 2, b->info.frames, b->info.rate, b->info.loopStart, b->info.loopLength);
        return out.size() / 2 == (size_t)b->info.frames * (loops + 1) ? 0 : 1;
    }

    // SFX for the session
    SoundData sfx1, sfx2;
    {
        std::vector<uint8_t> d; std::string err;
        if (!read_all(root + "/Content/SFX/BOSS_HIT_01.wav", d) || !parse_wav(d.data(), d.size(), sfx1, err)) { fprintf(stderr, "sfx1: %s\n", err.c_str()); return 1; }
        if (!read_all(root + "/Content/SFX/BOSS_BEAM.wav", d) || !parse_wav(d.data(), d.size(), sfx2, err)) { fprintf(stderr, "sfx2: %s\n", err.c_str()); return 1; }
    }
    int music = s.eng.findCategory("Music");
    s.eng.setCategoryVolume(music, 2.0f); // Music.MasterVolume = 1

    if (mode == "--play")
    {
        if (!open_device()) return 1;
        printf("device rate %d\n", m.rate);
        s.play("Title");
        for (int i = 0; i < 240; i++)
        {
            s.eng.update();
            if (i == 60) voice_play(&sfx1, 1, 0, -0.5f, false, true);
            if (i == 120) s.play("Mirage");
            SDL_Delay(16);
        }
        close_device();
        return 0;
    }

    // offline render
    m.headless = false;
    m.rate = 48000;
    std::vector<int16_t> pcm;
    std::vector<float> buf(2 * 800);
    double t = 0;
    auto run = [&](double secs) {
        int n = (int)(secs * 60);
        for (int i = 0; i < n; i++)
        {
            s.eng.update();
            render(buf.data(), 800);
            for (float v : buf) { float x = v * 32767.0f; pcm.push_back((int16_t)std::clamp(x, -32768.0f, 32767.0f)); }
            t += 1.0 / 60;
        }
    };
    auto peak = [&](double from, double to) {
        size_t a = (size_t)(from * 48000) * 2, b = std::min(pcm.size(), (size_t)(to * 48000) * 2);
        int p = 0; double rms = 0;
        for (size_t i = a; i < b; i++) { p = std::max(p, std::abs((int)pcm[i])); rms += (double)pcm[i] * pcm[i]; }
        return std::make_pair(p, std::sqrt(rms / std::max<size_t>(1, b - a)));
    };
    s.play("Title");                      // 0..4 s
    run(1.0);
    int h = voice_play(&sfx1, 1.0f, 0.0f, -0.5f, false, true);
    int h2 = voice_play(&sfx2, 0.5f, 0.5f, 0.5f, true, false);
    printf("sfx handles %d %d state %d %d\n", h, h2, voice_state(h), voice_state(h2));
    run(1.0);
    voice_pause(h2); printf("paused state %d\n", voice_state(h2));
    run(0.5);
    voice_resume(h2);
    run(0.5);
    voice_stop(h2); printf("stopped state %d\n", voice_state(h2));
    run(1.0);
    s.play("Mirage");                     // 4 s: crossfade
    for (int k = 0; k < 6; k++)
    {
        printf("  t=%.2f cues:", t);
        for (int i = 0; i < m.nmusic; i++) printf(" [cue %d fade %.3f%s]", m.music[i]->cue, m.music[i]->fade, m.music[i]->stopping ? " stopping" : "");
        printf("\n");
        run(0.25);
    }
    run(2.5);
    s.play("VictoryKyle");                // 8 s: jingle + TheArchives/TheTale after 6.4 s
    run(9.0);
    printf("music cues alive at %.1fs: %d\n", t, m.nmusic);
    s.play("UnlockMoonstone");            // 17 s: plays once
    run(3.0);
    s.eng.stopCategory(music, false);     // 20 s: 1 s fade out
    run(2.0);
    printf("music cues alive after stop: %d (playing list %zu)\n", m.nmusic, s.eng.playing.size());
    s.play("Title");                      // 22 s: fresh start, no fade-in
    run(1.0);
    s.eng.stopCategory(music, true);      // 23 s: hard stop
    run(0.5);
    s.eng.update();
    printf("after immediate stop: %d cues, wave buffers cached:", m.nmusic);
    for (auto* b : s.wb->cache) if (b) printf(" %d", b->index);
    printf("\n");
    for (double a = 0; a < 23.5; a += 0.5) { auto pr = peak(a, a + 0.5); printf("%5.1fs peak %5d rms %7.1f\n", a, pr.first, pr.second); }
    write_wav(argv[2], pcm, 48000);
    printf("wrote %s (%.1f s)\n", argv[2], pcm.size() / 2 / 48000.0);
    return 0;
}
