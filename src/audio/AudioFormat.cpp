#include "audio/AudioFormat.h"

#include <stdexcept>

namespace audio {

AudioFormat::AudioFormat(std::uint32_t sampleRate, std::uint32_t channels, SampleFormat sampleFormat)
    : sampleRate_(sampleRate), channels_(channels), sampleFormat_(sampleFormat) {
    if (sampleRate_ == 0U) {
        throw std::invalid_argument("AudioFormat requires a positive sample rate");
    }
    if (channels_ == 0U) {
        throw std::invalid_argument("AudioFormat requires at least one channel");
    }
}

std::uint32_t AudioFormat::sampleRate() const noexcept {
    return sampleRate_;
}

std::uint32_t AudioFormat::channels() const noexcept {
    return channels_;
}

SampleFormat AudioFormat::sampleFormat() const noexcept {
    return sampleFormat_;
}

bool AudioFormat::operator==(AudioFormat other) const noexcept {
    return sampleRate_ == other.sampleRate_ && channels_ == other.channels_ && sampleFormat_ == other.sampleFormat_;
}

bool AudioFormat::operator!=(AudioFormat other) const noexcept {
    return !(*this == other);
}

}  // namespace audio
