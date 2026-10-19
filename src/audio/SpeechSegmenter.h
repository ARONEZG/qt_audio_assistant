#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace audio {

// A speech interval in frame units: a contiguous run of frames classified as
// speech. Frame indices are half-open — [start(), end()) — and end() is always
// greater than start(). Immutable value: the state is fixed at construction
// and never mutated.
class SpeechInterval {
public:
    SpeechInterval(std::size_t startFrame, std::size_t endFrame);

    [[nodiscard]] std::size_t start() const noexcept;
    [[nodiscard]] std::size_t end() const noexcept;
    [[nodiscard]] std::size_t frameCount() const noexcept;

private:
    std::size_t start_;
    std::size_t end_;
};

// Turns a stream of per-frame speech probabilities into a set of speech
// intervals. It applies the classic VAD post-processing trio:
//
//  * threshold       — a frame is "voiced" when its probability is at least
//                      this; below it the frame is "silent".
//  * hysteresis      — once inside a speech run, the probability must drop
//                      below (threshold - hysteresis) before the run can end.
//                      This prevents a single dip in one frame from splitting
//                      an utterance into two.
//  * minSpeechFrames — a run shorter than this is rejected as a false trigger;
//                      minSilenceFrames — a gap shorter than this is bridged
//                      into one utterance (the "hangover" that keeps a pause
//                      inside a sentence continuous).
//
// The output is a minimal, merged set: intervals are non-overlapping, ordered
// by start, and every interval is at least minSpeechFrames long. The behaviour
// is deterministic and side-effect free: the same probability stream always
// produces the same intervals. All parameters are validated at construction
// and are immutable afterwards.
//
// This class is pure C++/STL (no Qt, no ONNX, no platform code), so it is
// unit-testable without the detector: a test double provides the probabilities.
class SpeechSegmenter {
public:
    struct Parameters {
        float threshold = 0.5F;
        float hysteresis = 0.15F;
        std::size_t minSpeechFrames = 8;   // 256 ms at 32 ms/frame
        std::size_t minSilenceFrames = 4;  // 128 ms at 32 ms/frame
    };

    // Preconditions (rejected with std::invalid_argument): threshold in (0, 1),
    // hysteresis in (0, threshold), minSpeechFrames > 0, minSilenceFrames >= 0.
    explicit SpeechSegmenter(Parameters parameters);

    // Classifies the given probability stream and returns the merged speech
    // intervals. An empty input produces an empty result. The probabilities
    // must lie in [0, 1].
    [[nodiscard]] std::vector<SpeechInterval> segment(std::span<const float> probabilities) const;

private:
    Parameters parameters_;
};

}  // namespace audio
