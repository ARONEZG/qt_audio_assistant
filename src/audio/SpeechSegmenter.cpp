#include "audio/SpeechSegmenter.h"

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace audio {

SpeechInterval::SpeechInterval(std::size_t startFrame, std::size_t endFrame) : start_(startFrame), end_(endFrame) {
}

std::size_t SpeechInterval::start() const noexcept {
    return start_;
}

std::size_t SpeechInterval::end() const noexcept {
    return end_;
}

std::size_t SpeechInterval::frameCount() const noexcept {
    return end_ - start_;
}

namespace {

// "No frame" sentinel stored in speechEndFrame_ (no open run) and in
// gapStartFrame_ (no open gap). A real frame index is always far below
// numeric_limits<size_t>::max(), so the sentinel is unambiguous. It lets the
// accumulator keep both flags without separate bool members, which would pad
// the class to a non-8-byte size (-Wpadded).
inline constexpr std::size_t kNoFrame = std::numeric_limits<std::size_t>::max();

// Accumulates speech runs from a per-frame probability stream. It implements
// the VAD post-processing state machine: a frame is "voiced" above the
// threshold, stays "in speech" down to the hysteresis floor, and a silence
// gap must last minSilenceFrames before it ends the open run. Runs shorter
// than minSpeechFrames are rejected. The behaviour is deterministic; the
// class is a pure C++/STL value that only transforms the incoming stream.
class SpeechAccumulator {
public:
    SpeechAccumulator(std::size_t minSpeechFrames, std::size_t minSilenceFrames);

    // Consumes one frame probability. `index` is the frame's position in the
    // stream (0-based); it is stored so a finished run keeps its boundaries.
    void consume(std::size_t index, float probability, float threshold, float silenceThreshold);

    // Closes any open run (trailing speech) and returns the merged intervals.
    [[nodiscard]] std::vector<SpeechInterval> finish();

private:
    void closeRun();

    std::vector<SpeechInterval> intervals_;
    std::size_t startFrame_ = 0;             // first frame of the open run
    std::size_t speechEndFrame_ = kNoFrame;  // last speech frame; kNoFrame = no run
    std::size_t gapStartFrame_ = kNoFrame;   // first frame of the open gap; kNoFrame = none
    std::size_t minSpeechFrames_;
    std::size_t minSilenceFrames_;
};

SpeechAccumulator::SpeechAccumulator(std::size_t minSpeechFrames, std::size_t minSilenceFrames)
    : minSpeechFrames_(minSpeechFrames), minSilenceFrames_(minSilenceFrames) {
}

void SpeechAccumulator::closeRun() {
    const std::size_t kEnd = speechEndFrame_ + 1;
    if (kEnd - startFrame_ >= minSpeechFrames_) {
        intervals_.emplace_back(startFrame_, kEnd);
    }
}

void SpeechAccumulator::consume(std::size_t index, float probability, float threshold, float silenceThreshold) {
    // Voiced: (re)starts the run, extends it, and closes any open gap.
    if (probability >= threshold) {
        if (speechEndFrame_ == kNoFrame) {
            startFrame_ = index;
        }
        speechEndFrame_ = index;
        gapStartFrame_ = kNoFrame;
        return;
    }

    // Hysteresis band: still speech, but not a trigger. It also closes any
    // open gap (the speech came back), so the gap never extends here.
    if (probability >= silenceThreshold) {
        if (speechEndFrame_ != kNoFrame) {
            speechEndFrame_ = index;
            gapStartFrame_ = kNoFrame;
        }
        return;
    }

    // Below the hysteresis band: silence. The gap is measured from the first
    // such frame; once it reaches minSilenceFrames the run closes.
    if (speechEndFrame_ == kNoFrame) {
        return;
    }
    if (gapStartFrame_ == kNoFrame) {
        gapStartFrame_ = index;
    }
    if (index - gapStartFrame_ + 1 >= minSilenceFrames_) {
        closeRun();
        speechEndFrame_ = kNoFrame;
        gapStartFrame_ = kNoFrame;
    }
}

std::vector<SpeechInterval> SpeechAccumulator::finish() {
    if (speechEndFrame_ != kNoFrame) {
        closeRun();
    }
    return intervals_;
}

}  // namespace

SpeechSegmenter::SpeechSegmenter(Parameters parameters) : parameters_(parameters) {
    const bool kThresholdInRange = parameters_.threshold > 0.0F && parameters_.threshold < 1.0F;
    if (!kThresholdInRange) {
        throw std::invalid_argument("SpeechSegmenter: threshold must be in (0, 1)");
    }
    const bool kHysteresisInRange = parameters_.hysteresis > 0.0F && parameters_.hysteresis < parameters_.threshold;
    if (!kHysteresisInRange) {
        throw std::invalid_argument("SpeechSegmenter: hysteresis must be in (0, threshold)");
    }
    if (parameters_.minSpeechFrames == 0) {
        throw std::invalid_argument("SpeechSegmenter: minSpeechFrames must be > 0");
    }
}

std::vector<SpeechInterval> SpeechSegmenter::segment(std::span<const float> probabilities) const {
    const float kSilenceThreshold = parameters_.threshold - parameters_.hysteresis;
    SpeechAccumulator accumulator(parameters_.minSpeechFrames, parameters_.minSilenceFrames);
    for (std::size_t i = 0; i < probabilities.size(); ++i) {
        accumulator.consume(i, probabilities[i], parameters_.threshold, kSilenceThreshold);
    }
    return accumulator.finish();
}

}  // namespace audio
