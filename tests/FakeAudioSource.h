#pragma once

#include "audio/AudioFormat.h"
#include "audio/AudioSource.h"

// The canonical capture stream: 16 kHz, mono, f32 (see AudioFormat.h). Shared by
// the capture tests so every double and every ring buffer describes the same
// stream.
[[nodiscard]] inline audio::AudioFormat canonicalFormat() {
    return {16000U, 1U, audio::SampleFormat::kFloat32};
}

// Double for the AudioSource role: counts the lifecycle commands and answers
// format() with the canonical stream without ever touching a device.
class FakeAudioSource final : public audio::AudioSource {
public:
    FakeAudioSource();
    ~FakeAudioSource() override;

    FakeAudioSource(const FakeAudioSource&) = delete;
    FakeAudioSource& operator=(const FakeAudioSource&) = delete;
    FakeAudioSource(FakeAudioSource&&) = delete;
    FakeAudioSource& operator=(FakeAudioSource&&) = delete;

    void start() override {
        ++startCount_;
    }
    void stop() override {
        ++stopCount_;
    }

    [[nodiscard]] audio::AudioFormat format() const noexcept override {
        return canonicalFormat();
    }

    [[nodiscard]] int startCount() const noexcept {
        return startCount_;
    }
    [[nodiscard]] int stopCount() const noexcept {
        return stopCount_;
    }

private:
    int startCount_ = 0;
    int stopCount_ = 0;
};
