#pragma once
// Minimal dependency-free WAV reader for the reference-calibration analyzer.
// Supports 16/24/32-bit PCM and 32-bit IEEE float, mono/stereo, 44100/48000 Hz
// (44.1 kHz is crudely linear-resampled to 48 kHz — fine for the long-term
// averages calibration measures). Returns a 48 kHz StereoBuffer.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "rtg/utils/audio.h"

namespace rtg {

// Reads `path` into `out` at 48 kHz. Returns false on any parse/IO failure.
inline bool readWavToStereo48k(const std::string& path, StereoBuffer& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> bytes;
    { char buf[65536]; size_t r;
      while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + r); }
    std::fclose(f);
    if (bytes.size() < 44) return false;

    auto rd16 = [&](size_t o) -> uint16_t { return uint16_t(bytes[o] | (bytes[o + 1] << 8)); };
    auto rd32 = [&](size_t o) -> uint32_t {
        return uint32_t(bytes[o]) | (uint32_t(bytes[o + 1]) << 8) |
               (uint32_t(bytes[o + 2]) << 16) | (uint32_t(bytes[o + 3]) << 24);
    };
    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
        return false;

    uint16_t audioFormat = 0, channels = 0, bits = 0;
    uint32_t sampleRate = 0;
    size_t dataOff = 0, dataLen = 0;
    bool haveFmt = false, haveData = false;

    size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        char id[5] = {0}; std::memcpy(id, bytes.data() + pos, 4);
        uint32_t sz = rd32(pos + 4);
        size_t body = pos + 8;
        if (std::memcmp(id, "fmt ", 4) == 0 && body + 16 <= bytes.size()) {
            audioFormat = rd16(body);
            channels = rd16(body + 2);
            sampleRate = rd32(body + 4);
            bits = rd16(body + 14);
            if (audioFormat == 0xFFFE && sz >= 40 && body + 26 <= bytes.size())
                audioFormat = rd16(body + 24);   // extensible: real format tag in SubFormat GUID
            haveFmt = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            dataOff = body;
            dataLen = std::min<size_t>(sz, bytes.size() - body);
            haveData = true;
        }
        pos = body + sz + (sz & 1);              // chunks are word-aligned
    }
    if (!haveFmt || !haveData || channels == 0) return false;

    const int ch = channels;
    const size_t bytesPerSample = bits / 8;
    if (bytesPerSample == 0) return false;
    const size_t frameBytes = bytesPerSample * ch;
    const size_t frames = dataLen / frameBytes;
    if (frames == 0) return false;

    auto sampleAt = [&](size_t frame, int c) -> float {
        size_t o = dataOff + frame * frameBytes + size_t(c) * bytesPerSample;
        if (audioFormat == 3 && bits == 32) {          // IEEE float
            float v; std::memcpy(&v, bytes.data() + o, 4); return v;
        }
        if (bits == 16) {
            int16_t v = int16_t(rd16(o)); return float(v) / 32768.0f;
        }
        if (bits == 24) {
            int32_t v = int32_t(bytes[o]) | (int32_t(bytes[o + 1]) << 8) | (int32_t(bytes[o + 2]) << 16);
            if (v & 0x800000) v |= ~0xFFFFFF;          // sign-extend
            return float(v) / 8388608.0f;
        }
        if (bits == 32) {                              // 32-bit PCM
            int32_t v = int32_t(rd32(o)); return float(double(v) / 2147483648.0);
        }
        return 0.0f;
    };

    std::vector<float> L(frames), R(frames);
    for (size_t i = 0; i < frames; ++i) {
        float l = sampleAt(i, 0);
        float r = (ch >= 2) ? sampleAt(i, 1) : l;
        L[i] = l; R[i] = r;
    }

    // Resample to 48 kHz if needed (crude linear interpolation).
    if (sampleRate == 48000 || sampleRate == 0) {
        out.l = std::move(L); out.r = std::move(R);
        return true;
    }
    const double ratio = 48000.0 / double(sampleRate);
    const size_t outN = size_t(double(frames) * ratio);
    out.l.assign(outN, 0.0f); out.r.assign(outN, 0.0f);
    for (size_t i = 0; i < outN; ++i) {
        double srcPos = double(i) / ratio;
        size_t i0 = size_t(srcPos);
        size_t i1 = std::min(i0 + 1, frames - 1);
        float frac = float(srcPos - double(i0));
        out.l[i] = L[i0] + (L[i1] - L[i0]) * frac;
        out.r[i] = R[i0] + (R[i1] - R[i0]) * frac;
    }
    return true;
}

} // namespace rtg
