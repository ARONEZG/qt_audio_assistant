#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "audio/SpeechSegmenter.h"
#include "audio/VoiceActivityDetector.h"

namespace audio {

// Feeds the canonical capture stream (16 kHz, mono, f32) to a
// VoiceActivityDetector and builds the "keep" mask that trims silence before
// transcription.
//
// It owns the framing: the stream arrives as arbitrary-length sample blocks
// (the ring buffer snapshot) and is cut into 512-sample frames (32 ms) with a
// 64-sample context tail carried between frames. Each frame yields one
// probability, and the SpeechSegmenter turns the probability stream into
// speech intervals in frame units. The 1:1 correspondence of a sample to a
// frame is simply index / 512, so intervals map back to sample ranges.
//
// The result is a SpeechMask: for every sample of the processed stream, whether
// it belongs to a speech interval (true) or to silence (false). The caller
// hands the mask to a trimmer that produces the actual audio block.
//
// The gate does not hold the raw audio: it is fed block-by-block with
// feedSamples() and accumulates the probability stream plus the intervals.
// reset() clears the detector state and all accumulated intervals, exactly like
// the detector's own reset() — a new independent stream.
//
// This class is pure C++/STL (no Qt, no ONNX); the VoiceActivityDetector is an
// injected role, so tests use a deterministic double.
//
// Public API note: production only calls trimSilence(). feedSamples(),
// intervals(), keepMask() and consumedSamples() form a deliberate seam for
// streaming segmentation (feed blocks as they arrive, query the mask) and for
// the deterministic tests; they are intentionally kept public rather than
// hidden, so the streaming path can be built without reshaping the class.
class VadSpeechGate {
public:
    // Non-copyable and non-movable: the gate borrows the detector and owns
    // accumulated stream state, so copying/moving it would alias the detector
    // or duplicate the intervals. The deleted members are public so accidental
    // copies fail with a readable diagnostic at the call site.
    VadSpeechGate(const VadSpeechGate&) = delete;
    VadSpeechGate& operator=(const VadSpeechGate&) = delete;
    VadSpeechGate(VadSpeechGate&&) = delete;
    VadSpeechGate& operator=(VadSpeechGate&&) = delete;

    ~VadSpeechGate() = default;

    // The detector is borrowed and must outlive the gate. The segmenter
    // parameters control threshold / hysteresis / min durations.
    VadSpeechGate(VoiceActivityDetector& detector, const SpeechSegmenter::Parameters& parameters);

    // Appends one block of canonical PCM frames. Frames are cut into
    // 512-sample VAD frames; a partial tail frame at the block boundary is
    // carried to the next feedSamples() call.
    void feedSamples(std::span<const float> samples);

    // The speech intervals detected so far, in frame units.
    [[nodiscard]] const std::vector<SpeechInterval>& intervals() const noexcept;

    // Builds the per-sample keep mask for the given number of samples. The
    // count may be any value; samples beyond the processed stream are treated
    // as silence. A sample at index i belongs to speech iff some interval
    // contains frame i / 512.
    [[nodiscard]] std::vector<bool> keepMask(std::size_t sampleCount) const;

    // Convenience one-shot: trims silence from the given sample block. It
    // resets the detector, feeds the whole block through the VAD, identifies
    // speech intervals, and returns only the samples inside those intervals
    // (concatenated in chronological order). An empty result means the block
    // contained no speech (or every detected run was shorter than minSpeech).
    // This is the main integration point with AudioCapture::transcribeLast().
    //
    // Limitation: the intervals are concatenated with all silence between them
    // removed, so two phrases separated by a pause longer than minSilenceFrames
    // arrive back-to-back ([phrase1][phrase2]) with no separator. That is the
    // intended "trim silence" behaviour, not "select the last question"; the
    // latter (keep only the final interval) is a deliberate next step.
    [[nodiscard]] std::vector<float> trimSilence(std::span<const float> samples);

    // The number of PCM samples consumed by feedSamples() so far, including the
    // partial frame currently held. Used to align the mask with the stream.
    [[nodiscard]] std::size_t consumedSamples() const noexcept;

    // Clears the detector state and all accumulated intervals/frames.
    void reset();

private:
    VoiceActivityDetector& detector_;
    SpeechSegmenter segmenter_;
    std::vector<SpeechInterval> intervals_;
    std::vector<float> probabilities_;  // one probability per fed 512-frame
    std::vector<float> pending_;        // samples not yet forming a full frame
    std::size_t consumedSamples_ = 0;
};

}  // namespace audio
