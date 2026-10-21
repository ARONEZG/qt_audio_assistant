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

// Maximum silence between two speech intervals that still belongs to one
// utterance. The segmenter hangover (minSilenceFrames * 32 ms, 512 ms in
// production) already bridges short pauses, but a natural pause inside a
// question can be longer and then produces two intervals; sending only the
// final one truncated the phrase to its tail. Walking back over intervals
// closer than this window rebuilds the whole phrase while keeping older,
// unrelated speech out of the request: 1.5 s is longer than a breath/comma
// pause but much shorter than the pauses between separate questions, and it
// bounds the added audio to a few seconds instead of the 20 s capture window.
inline constexpr std::size_t kUtteranceMergeGapSamples = 24000;  // 1.5 s at 16 kHz

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
    analyze(samples);
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
    analyze(samples);
    if (intervals_.empty()) {
        return {};
    }

    // The current utterance is the final cluster of intervals: the last
    // interval plus every preceding interval whose gap is short enough to be a
    // pause inside the same phrase. Earlier, unrelated speech stays out, so
    // the request does not grow with the whole capture window.
    const std::size_t kFirstInterval = lastUtteranceFirstInterval();
    const std::size_t kSpeechStart = intervals_[kFirstInterval].start() * kVadFrameSize;
    const std::size_t kSpeechEnd = intervals_.back().end() * kVadFrameSize;
    const std::size_t kStart = kSpeechStart > kPreRollSamples ? kSpeechStart - kPreRollSamples : 0;
    const std::size_t kEnd = std::min(samples.size(), kSpeechEnd + kPostRollSamples);
    if (kStart >= kEnd) {
        return {};
    }
    return {samples.begin() + static_cast<std::ptrdiff_t>(kStart), samples.begin() + static_cast<std::ptrdiff_t>(kEnd)};
}

std::size_t VadSpeechGate::lastUtteranceFirstInterval() const noexcept {
    std::size_t first = intervals_.size() - 1;
    while (first > 0) {
        const SpeechInterval& kPrevious = intervals_[first - 1];
        const std::size_t kGapSamples = (intervals_[first].start() - kPrevious.end()) * kVadFrameSize;
        if (kGapSamples > kUtteranceMergeGapSamples) {
            break;
        }
        --first;
    }
    return first;
}

std::size_t VadSpeechGate::lastSpeechSamples() const noexcept {
    if (intervals_.empty()) {
        return 0;
    }
    // Count only the speech frames of the selected utterance cluster: the
    // pauses between its intervals and the pre/post-roll are not speech, so the
    // caller's minimum-duration guard stays honest.
    std::size_t speechSamples = 0;
    for (std::size_t i = lastUtteranceFirstInterval(); i < intervals_.size(); ++i) {
        speechSamples += intervals_[i].frameCount() * kVadFrameSize;
    }
    return speechSamples;
}

void VadSpeechGate::analyze(std::span<const float> samples) {
    reset();
    feedSamples(samples);
}

void VadSpeechGate::reset() {
    detector_.reset();
    intervals_.clear();
    probabilities_.clear();
    pending_.clear();
    consumedSamples_ = 0;
}

}  // namespace audio
