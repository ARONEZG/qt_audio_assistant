#include "audio/WavEncoder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace audio {

namespace {

// WAVE format tag 1 is linear PCM. Together with 16 bits per sample it is the
// classic, universally decodable payload.
constexpr std::uint16_t kPcmFormatTag = 1U;
constexpr std::uint16_t kBitsPerSample = 16U;

// "RIFF"(4) + chunkSize(4) + "WAVE"(4) + "fmt "(4) + subchunk1Size(4) + the
// 16-byte fmt body + "data"(4) + dataSize(4).
constexpr std::uint32_t kHeaderBytes = 44U;

// Appends a four-character chunk identifier. It takes a string_view and iterates
// it (no pointer subscripting) so the strict -Wunsafe-buffer-usage profile used
// by the audio library stays clean.
void appendTag(std::vector<std::uint8_t>& out, std::string_view tag) {
    for (const char kByte : tag) {
        out.push_back(static_cast<std::uint8_t>(kByte));
    }
}

void appendU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

// Full-scale f32 -> int16 with an asymmetric negative end: +1.0 saturates at
// 32767 and -1.0 at -32768 (the int16 minimum), which is the conventional PCM
// mapping. Rounding is half away from zero (std::lround), so the result is
// deterministic and reproducible across platforms.
std::int16_t toPcm16(float sample) {
    const float kClamped = std::clamp(sample, -1.0F, 1.0F);
    const float kScaled = kClamped >= 0.0F ? kClamped * 32767.0F : kClamped * 32768.0F;
    const long kRounded = std::lround(kScaled);
    return static_cast<std::int16_t>(std::clamp(kRounded, -32768L, 32767L));
}

}  // namespace

std::vector<std::uint8_t> encodeWavPcm16(AudioFormat format, std::span<const float> samples) {
    // Bytes of sample data: one frame is a single float sample index (the ring
    // buffer already holds a non-interleaved canonical stream) times two bytes.
    const auto kDataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    const auto kBlockAlign = static_cast<std::uint16_t>(format.channels() * (kBitsPerSample / 8U));
    const auto kByteRate = format.sampleRate() * static_cast<std::uint32_t>(kBlockAlign);

    std::vector<std::uint8_t> wav;
    wav.reserve(static_cast<std::size_t>(kHeaderBytes) + static_cast<std::size_t>(kDataBytes));

    appendTag(wav, "RIFF");
    appendU32(wav, kHeaderBytes - 8U + kDataBytes);  // size of everything after this field
    appendTag(wav, "WAVE");

    appendTag(wav, "fmt ");
    appendU32(wav, 16U);  // fmt chunk body size
    appendU16(wav, kPcmFormatTag);
    appendU16(wav, static_cast<std::uint16_t>(format.channels()));
    appendU32(wav, format.sampleRate());
    appendU32(wav, kByteRate);
    appendU16(wav, kBlockAlign);
    appendU16(wav, kBitsPerSample);

    appendTag(wav, "data");
    appendU32(wav, kDataBytes);
    for (const float kSample : samples) {
        // The int16 bit pattern is reinterpreted as uint16_t for little-endian
        // serialisation; the conversion is well-defined modulo 2^16.
        appendU16(wav, static_cast<std::uint16_t>(toPcm16(kSample)));
    }
    return wav;
}

}  // namespace audio
