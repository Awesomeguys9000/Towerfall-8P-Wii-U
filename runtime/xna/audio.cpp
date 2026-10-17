// Microsoft.Xna.Framework.Audio on the GC-free core in audio_core.h
// (software mixer on SDL2, WAV / MS-ADPCM decoding, XACT music banks).
#include "xna/xna.h"
#include "xna/audio_core.h"

namespace Microsoft { namespace Xna { namespace Framework { namespace Audio {

using xaudio::Lock;
using xaudio::mixer;

void xna_audio_init() { xaudio::open_device(); }
void xna_audio_shutdown() { xaudio::close_device(); }

// ------------------------------------------------------------------ SoundEffect
SoundEffect* SoundEffect::FromStream(::System::IO::Stream* s)
{
    if (!s) ::sys::throw_null();
    std::vector<uint8_t> bytes = s->ReadAllBytesNative();
    SoundData* d = new SoundData();
    std::string err;
    if (!xaudio::parse_wav(bytes.data(), bytes.size(), *d, err))
    {
        delete d;
        ::sys::throw_msg("ArgumentException", ("SoundEffect.FromStream: " + err).c_str());
    }
    SoundEffect* e = ::sys::gcnew<SoundEffect>();
    e->data = d;
    return e;
}

SoundEffectInstance* SoundEffect::CreateInstance() { return ::sys::gcnew<SoundEffectInstance>(this); }

bool SoundEffect::Play() { return Play(1.0f, 0.0f, 0.0f); }

bool SoundEffect::Play(float volume, float pitch, float pan)
{
    if (!data || disposed) return false;
    return xaudio::voice_play(data, volume, pitch, pan, false, true) >= 0;
}

::System::TimeSpan SoundEffect::get_Duration()
{
    if (!data || data->sampleRate <= 0) return ::System::TimeSpan();
    return ::System::TimeSpan::FromTicks((int64_t)data->frames * 10000000LL / data->sampleRate);
}

float SoundEffect::get_MasterVolume() { return mixer().sfxMaster; }
void SoundEffect::set_MasterVolume(float v) { Lock lk; mixer().sfxMaster = std::max(0.0f, v); }

// ------------------------------------------------------------------ SoundEffectInstance
SoundEffectInstance::~SoundEffectInstance()
{
    if (voice >= 0) xaudio::voice_stop(voice);
    voice = -1;
}

void SoundEffectInstance::Play()
{
    if (disposed || !effect || !effect->data) return;
    int st = xaudio::voice_state(voice);
    if (st == 0) return;
    if (st == 1) { xaudio::voice_resume(voice); return; }
    voice = xaudio::voice_play(effect->data, volume, pitch, pan, looped, false);
}

void SoundEffectInstance::Pause() { if (voice >= 0) xaudio::voice_pause(voice); }
void SoundEffectInstance::Resume()
{
    if (voice < 0) return;
    if (xaudio::voice_state(voice) == 1) xaudio::voice_resume(voice);
}
void SoundEffectInstance::Stop()
{
    if (voice >= 0) xaudio::voice_stop(voice);
    voice = -1;
}
void SoundEffectInstance::Dispose()
{
    Stop();
    disposed = true;
}
SoundState SoundEffectInstance::get_State() { return (SoundState)xaudio::voice_state(voice); }
void SoundEffectInstance::set_Volume(float v)
{
    volume = std::clamp(v, 0.0f, 1.0f);
    if (voice >= 0) xaudio::voice_set(voice, volume, pitch, pan);
}
void SoundEffectInstance::set_Pitch(float v)
{
    pitch = std::clamp(v, -1.0f, 1.0f);
    if (voice >= 0) xaudio::voice_set(voice, volume, pitch, pan);
}
void SoundEffectInstance::set_Pan(float v)
{
    pan = std::clamp(v, -1.0f, 1.0f);
    if (voice >= 0) xaudio::voice_set(voice, volume, pitch, pan);
}

// ------------------------------------------------------------------ XACT
static xaudio::XactEngine* native_engine(AudioEngine* e) { return e ? (xaudio::XactEngine*)e->native : nullptr; }

AudioEngine::AudioEngine(String* settingsFile)
{
    if (!settingsFile) ::sys::throw_null();
    std::vector<uint8_t> d;
    std::string path = ::sys::resolve_path(settingsFile);
    if (!::sys::read_file(path, d)) ::sys::throw_msg("FileNotFoundException", ("AudioEngine: cannot read " + path).c_str());
    auto* eng = new xaudio::XactEngine();
    std::string err;
    if (!eng->load(d.data(), d.size(), err))
    {
        delete eng;
        ::sys::throw_msg("InvalidOperationException", ("AudioEngine: " + err).c_str());
    }
    native = eng;
    int mi = eng->findCategory("Music");
    if (mi >= 0) categoryGain = eng->cats[mi].amp;
}

void AudioEngine::Update()
{
    if (auto* eng = native_engine(this)) eng->update();
}

void AudioEngine::Dispose()
{
    if (auto* eng = native_engine(this))
    {
        eng->stopCategory(-1, true);
        eng->update();
    }
}

AudioCategory AudioEngine::GetCategory(String* name)
{
    auto* eng = native_engine(this);
    int idx = (eng && name) ? eng->findCategory(name->ToUtf8()) : -1;
    if (idx < 0) ::sys::throw_msg("InvalidOperationException", "AudioEngine.GetCategory: invalid category name");
    AudioCategory c;
    c.engine = this;
    c.name = name;
    c.index = idx;
    return c;
}

void AudioCategory::SetVolume(float v)
{
    if (auto* eng = native_engine(engine)) eng->setCategoryVolume(index, v);
    if (engine) engine->musicVolume = v;
}
void AudioCategory::Stop(AudioStopOptions o)
{
    if (auto* eng = native_engine(engine)) eng->stopCategory(index, o == AudioStopOptions::Immediate);
}
void AudioCategory::Pause() { if (auto* eng = native_engine(engine)) eng->pauseCategory(index, true); }
void AudioCategory::Resume() { if (auto* eng = native_engine(engine)) eng->pauseCategory(index, false); }

WaveBank::WaveBank(AudioEngine* e, String* path)
{
    auto* eng = native_engine(e);
    if (!eng || !path) ::sys::throw_null();
    auto* wb = new xaudio::XWaveBank();
    std::string err;
    std::string p = ::sys::resolve_path(path);
    if (!wb->open(p, err))
    {
        if (wb->f) fclose(wb->f);
        delete wb;
        ::sys::throw_msg("InvalidOperationException", ("WaveBank: " + err).c_str());
    }
    eng->banks.push_back(wb); // the file stays open: waves are streamed from it
    native = wb;
}

SoundBank::SoundBank(AudioEngine* e, String* path) : engine(e)
{
    auto* eng = native_engine(e);
    if (!eng || !path) ::sys::throw_null();
    std::vector<uint8_t> d;
    std::string p = ::sys::resolve_path(path);
    if (!::sys::read_file(p, d)) ::sys::throw_msg("FileNotFoundException", ("SoundBank: cannot read " + p).c_str());
    auto* sb = new xaudio::XSoundBank();
    std::string err;
    if (!sb->load(d.data(), d.size(), err))
    {
        delete sb;
        ::sys::throw_msg("InvalidOperationException", ("SoundBank: " + err).c_str());
    }
    sb->eng = eng;
    native = sb;
}

void SoundBank::PlayCue(String* name)
{
    auto* sb = (xaudio::XSoundBank*)native;
    if (!sb || !name) return;
    std::string n = name->ToUtf8();
    int ci = sb->findCue(n);
    if (ci < 0) { fprintf(stderr, "[audio] SoundBank.PlayCue: unknown cue '%s'\n", n.c_str()); return; }
    sb->playCue(ci);
}

Cue* SoundBank::GetCue(String* name)
{
    Cue* c = ::sys::gcnew<Cue>();
    c->name = name;
    return c;
}

}}}} // namespace Microsoft::Xna::Framework::Audio
