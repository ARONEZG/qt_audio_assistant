#include "audio/VadSpeechGate.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

#include "audio/VoiceActivityDetector.h"

namespace audio {

namespace {

// The VAD frame size in samples (32 ms at 16 kHz). A frame is the unit the
// detector consumes; sample indices map to frame indices through it.
inline constexpr std::size_t kVadFrameSize = 512;

// Context kept around the selected utterance so the STT model hears word
// onsets and endings instead of a hard cut at a 32 ms frame boundary. These are
// starting values: they are the tuning knobs for real-microphone behaviour.
inline constexpr std::size_t kPreRollSamples = 4800;   // 300 ms at 16 kHz
inline constexpr std::size_t kPostRollSamples = 3200;  // 200 ms at 16 kHz

}  // namespace

VadSpeechGate::VadSpeechGate(VoiceActivityDetector& detector, const SpeechSegmenter::Parameters& parameters)
    : detector_(detector), segmenter_(parameters) {
}

void VadSpeechGate::feedSamples(std::span<const float> samples) {
    // The detector consumes exactly one frame per process() call, in order; the
    // 576-wide input is assembled inside the detector from the frame plus the
    // carried context tail. The gate's job is only to cut the stream into
    // 512-sample frames.
    pending_.insert(pending_.end(), samples.begin(), samples.end());
    consumedSamples_ += samples.size();

    const std::size_t kFrameCount = pending_.size() / kVadFrameSize;
    if (kFrameCount == 0) {
        return;
    }
    const std::span<const float> kPending(pending_);
    for (std::size_t i = 0; i < kFrameCount; ++i) {
        const float kProbability = detector_.process(kPending.subspan(i * kVadFrameSize, kVadFrameSize));
        probabilities_.push_back(kProbability);
    }
    // Keep the partial frame (if any) for the next call.
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(kFrameCount * kVadFrameSize));

    intervals_ = segmenter_.segment(probabilities_);
}

const std::vector<SpeechInterval>& VadSpeechGate::intervals() const noexcept {
    return intervals_;
}

std::vector<bool> VadSpeechGate::keepMask(std::size_t sampleCount) const {
    std::vector<bool> mask(sampleCount, false);
    if (intervals_.empty()) {
        return mask;
    }
    for (const SpeechInterval& interval : intervals_) {
        const std::size_t kFirstSample = interval.start() * kVadFrameSize;
        const std::size_t kEndSample = std::min(sampleCount, interval.end() * kVadFrameSize);
        if (kFirstSample >= sampleCount) {
            break;
        }
        std::fill(mask.begin() + static_cast<std::ptrdiff_t>(kFirstSample),
                  mask.begin() + static_cast<std::ptrdiff_t>(kEndSample),
                  true);
    }
    return mask;
}

std::size_t VadSpeechGate::consumedSamples() const noexcept {
    return consumedSamples_;
}

std::vector<float> VadSpeechGate::trimSilence(std::span<const float> samples) {
    reset();
    feedSamples(samples);
    const std::vector<bool> kKeep = keepMask(samples.size());
    std::vector<float> trimmed;
    trimmed.reserve(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (kKeep[i]) {
            trimmed.push_back(samples[i]);
        }
    }
    return trimmed;
}

std::vector<float> VadSpeechGate::lastSpeechBlock(std::span<const float> samples) {
    reset();
    feedSamples(samples);
    if (intervals_.empty()) {
        return {};
    }

    // Only the final interval is the current utterance; earlier intervals are
    // older phrases in the capture window and would make the STT request (and
    // its latency) grow with the whole 30 s window.
    const SpeechInterval& kLast = intervals_.back();
    const std::size_t kSpeechStart = kLast.start() * kVadFrameSize;
    const std::size_t kSpeechEnd = kLast.end() * kVadFrameSize;
    const std::size_t kStart = kSpeechStart > kPreRollSamples ? kSpeechStart - kPreRollSamples : 0;
    const std::size_t kEnd = std::min(samples.size(), kSpeechEnd + kPostRollSamples);
    if (kStart >= kEnd) {
        return {};
    }
    return {samples.begin() + static_cast<std::ptrdiff_t>(kStart), samples.begin() + static_cast<std::ptrdiff_t>(kEnd)};
}

void VadSpeechGate::reset() {
    detector_.reset();
    intervals_.clear();
    probabilities_.clear();
    pending_.clear();
    consumedSamples_ = 0;
}

}  // namespace audio
