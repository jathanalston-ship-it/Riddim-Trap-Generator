#pragma once
// Minimal dependency-free 16-bit PCM WAV writer (header-only) for the
// headless CLI and tests. The GUI app exports through JUCE's writers.
#include <cstdint>
#include <cstdio>
#include <string>
#include "rtg/utils/audio.h"

namespace rtg {

inline bool writeWav16(const std::string& path, const StereoBuffer& audio,
                       double sampleRate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    const uint32_t frames = (uint32_t)audio.size();
    const uint16_t channels = 2, bits = 16;
    const uint32_t sr = (uint32_t)sampleRate;
    const uint32_t byteRate = sr * channels * (bits / 8);
    const uint16_t blockAlign = channels * (bits / 8);
    const uint32_t dataSize = frames * blockAlign;
    const uint32_t riffSize = 36 + dataSize;

    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };

    std::fwrite("RIFF", 1, 4, f); w32(riffSize); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); w32(16); w16(1); w16(channels);
    w32(sr); w32(byteRate); w16(blockAlign); w16(bits);
    std::fwrite("data", 1, 4, f); w32(dataSize);

    for (uint32_t i = 0; i < frames; ++i) {
        for (float s : { audio.l[i], audio.r[i] }) {
            float c = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s);
            int16_t v = (int16_t)(c * 32767.0f);
            std::fwrite(&v, 2, 1, f);
        }
    }
    std::fclose(f);
    return true;
}

} // namespace rtg
