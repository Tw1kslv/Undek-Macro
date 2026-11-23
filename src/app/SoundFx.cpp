#include "app/SoundFx.h"

#include "app/AppSettings.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h> // must come first: mmsystem.h needs its types
#include <mmsystem.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace undek::soundfx {

#ifdef _WIN32
namespace {

constexpr int kSampleRate = 22050;

void putU16(std::vector<char>& out, uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void putU32(std::vector<char>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

struct Tone {
    double freq_hz;
    double duration_ms;
};

struct WavClip {
    std::vector<char> bytes;
    int duration_ms = 0;
};

WavClip makeSequenceWav(const std::vector<Tone>& tones, double gap_ms, double volume) {
    std::vector<int16_t> pcm;
    const double amplitude = 0.60 * std::clamp(volume, 0.0, 100.0) / 100.0;
    const double two_pi = 2.0 * 3.14159265358979323846;

    for (size_t t = 0; t < tones.size(); ++t) {
        const int n = static_cast<int>(tones[t].duration_ms * kSampleRate / 1000.0);
        const int fade = std::min(n / 4, static_cast<int>(0.004 * kSampleRate));
        for (int i = 0; i < n; ++i) {
            const double phase = two_pi * tones[t].freq_hz * (static_cast<double>(i) / kSampleRate);
            double env = 1.0;
            if (i < fade)
                env = static_cast<double>(i) / fade;
            else if (i > n - fade)
                env = static_cast<double>(n - i) / fade;
            const double s = std::sin(phase) * amplitude * env;
            pcm.push_back(static_cast<int16_t>(s * 32767.0));
        }
        if (t + 1 < tones.size()) {
            const int g = static_cast<int>(gap_ms * kSampleRate / 1000.0);
            pcm.insert(pcm.end(), g, 0);
        }
    }

    WavClip clip;
    const uint32_t data_size = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    const char riff[] = {'R', 'I', 'F', 'F'};
    const char wave[] = {'W', 'A', 'V', 'E'};
    const char fmt[] = {'f', 'm', 't', ' '};
    const char data[] = {'d', 'a', 't', 'a'};
    clip.bytes.insert(clip.bytes.end(), riff, riff + 4);
    putU32(clip.bytes, 36 + data_size);
    clip.bytes.insert(clip.bytes.end(), wave, wave + 4);
    clip.bytes.insert(clip.bytes.end(), fmt, fmt + 4);
    putU32(clip.bytes, 16);
    putU16(clip.bytes, 1);
    putU16(clip.bytes, 1);
    putU32(clip.bytes, kSampleRate);
    putU32(clip.bytes, kSampleRate * 2);
    putU16(clip.bytes, 2);
    putU16(clip.bytes, 16);
    clip.bytes.insert(clip.bytes.end(), data, data + 4);
    putU32(clip.bytes, data_size);
    for (int16_t s : pcm)
        putU16(clip.bytes, static_cast<uint16_t>(s));

    clip.duration_ms = static_cast<int>((pcm.size() * 1000.0) / kSampleRate);
    return clip;
}

// The clips stay alive for the whole session, which is all SND_ASYNC needs, so
// playing one doesn't have to spawn a thread that then sleeps for its length.
const WavClip& cueClip(bool enabled, double volume) {
    static WavClip enabled_clip;
    static WavClip disabled_clip;
    static double built_volume = -1.0;
    if (built_volume != volume) {
        enabled_clip = makeSequenceWav({{880, 70}, {1175, 110}}, 25, volume);
        disabled_clip = makeSequenceWav({{1175, 70}, {587, 140}}, 25, volume);
        built_volume = volume;
    }
    return enabled ? enabled_clip : disabled_clip;
}

void playCue(bool enabled) {
    const auto& s = AppSettings::get();
    if (!s.toggle_sound || s.toggle_sound_volume <= 0.0) return;
    const WavClip& clip = cueClip(enabled, s.toggle_sound_volume);
    if (clip.bytes.empty()) return;
    PlaySoundA(clip.bytes.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

} // namespace
#endif

void macrosEnabled() {
#ifdef _WIN32
    playCue(true);
#endif
}

void macrosDisabled() {
#ifdef _WIN32
    playCue(false);
#endif
}

} // namespace undek::soundfx
