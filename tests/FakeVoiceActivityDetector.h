#pragma once

#include <span>
#include <vector>

#include "audio/VoiceActivityDetector.h"

// Deterministic double for the VoiceActivityDetector role: it returns the
// configured probability for every processed frame and records how many frames
// were fed, so tests can assert the framing/gating behaviour without an ONNX
// model.
//
// The probability list is consumed in order: when it runs out, the last value
// keeps being returned. reset() restarts at the beginning of the list and
// records the reset.
class FakeVoiceActivityDetector final : public audio::VoiceActivityDetector {
public:
    FakeVoiceActivityDetector() = default;
    ~FakeVoiceActivityDetector() override;

    FakeVoiceActivityDetector(const FakeVoiceActivityDetector&) = delete;
    FakeVoiceActivityDetector& operator=(const FakeVoiceActivityDetector&) = delete;
    FakeVoiceActivityDetector(FakeVoiceActivityDetector&&) = delete;
    FakeVoiceActivityDetector& operator=(FakeVoiceActivityDetector&&) = delete;

    // A single probability, used for every processed frame.
    void setConstant(float probability);

    // A per-frame probability list (element i is the answer for frame i).
    void setSequence(std::vector<float> probabilities);

    // The frame size each process() call must receive (512 for the real model).
    // The fake honours it: it counts the fed frames and, when mismatchChecking
    // is enabled, fails the caller's expectation is expressed through
    // frameSize().
    [[nodiscard]] std::size_t frameSize() const noexcept {
        return 512;
    }

    float process(std::span<const float> frame) override;
    void reset() override;

    [[nodiscard]] int processCount() const noexcept;
    [[nodiscard]] int resetCount() const noexcept;

private:
    std::vector<float> probabilities_;
    std::size_t cursor_ = 0;
    int processCount_ = 0;
    int resetCount_ = 0;
};
