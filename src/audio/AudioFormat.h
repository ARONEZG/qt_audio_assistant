#pragma once

#include <cstdint>

namespace audio {

// The sample encoding of a single PCM value. Only 32-bit float is defined
// today: the capture contracts normalise every source to f32 in [-1, 1], so the
// downstream ring buffer and transcriber never have to branch on the encoding.
//
// The underlying type is the default int on purpose. Narrowing it to a 1-byte
// type would not shrink AudioFormat: the two uint32 members already force 12
// bytes of size/alignment and a narrow enum would only introduce 3 bytes of tail
// padding, so performance-enum-size's advice buys nothing here.
// NOLINTNEXTLINE(performance-enum-size)
enum class SampleFormat {
    kFloat32,
};

// The complete description of a PCM stream: how many frames per second, how
// many interleaved channels, and how each sample is encoded.
//
// It is an immutable value: the state is valid at construction (the sample rate
// and channel count are strictly positive) and never mutated afterwards, so a
// format can be copied and compared freely. The canonical capture format is
// 16 kHz, mono, f32.
//
// A zero sample rate or channel count is a programming error in the composition
// root, not a recoverable runtime condition, so it is rejected at construction
// instead of being carried around as a half-valid "empty" format. Formats are
// built during setup, never from the real-time audio callback.
class AudioFormat {
public:
    // Precondition (rejected with std::invalid_argument): sampleRate > 0 and
    // channels > 0.
    AudioFormat(std::uint32_t sampleRate, std::uint32_t channels, SampleFormat sampleFormat);

    [[nodiscard]] std::uint32_t sampleRate() const noexcept;
    [[nodiscard]] std::uint32_t channels() const noexcept;
    [[nodiscard]] SampleFormat sampleFormat() const noexcept;

    [[nodiscard]] bool operator==(AudioFormat other) const noexcept;
    [[nodiscard]] bool operator!=(AudioFormat other) const noexcept;

private:
    std::uint32_t sampleRate_;
    std::uint32_t channels_;
    SampleFormat sampleFormat_;
};

}  // namespace audio
