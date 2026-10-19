#include "FakeVoiceActivityDetector.h"

#include <algorithm>
#include <utility>

FakeVoiceActivityDetector::~FakeVoiceActivityDetector() = default;

void FakeVoiceActivityDetector::setConstant(float probability) {
    probabilities_ = {probability};
    cursor_ = 0;
}

void FakeVoiceActivityDetector::setSequence(std::vector<float> probabilities) {
    probabilities_ = std::move(probabilities);
    cursor_ = 0;
}

float FakeVoiceActivityDetector::process(std::span<const float> frame) {
    (void)frame;
    ++processCount_;
    if (probabilities_.empty()) {
        return 0.0F;
    }
    const float kProbability = probabilities_[std::min(cursor_, probabilities_.size() - 1)];
    if (cursor_ < probabilities_.size() - 1) {
        ++cursor_;
    }
    return kProbability;
}

void FakeVoiceActivityDetector::reset() {
    ++resetCount_;
    cursor_ = 0;
}

int FakeVoiceActivityDetector::processCount() const noexcept {
    return processCount_;
}

int FakeVoiceActivityDetector::resetCount() const noexcept {
    return resetCount_;
}
