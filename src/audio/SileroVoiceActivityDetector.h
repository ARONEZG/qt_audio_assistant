#pragma once

#include <memory>
#include <span>

#include "audio/VoiceActivityDetector.h"

namespace audio {

// Concrete VoiceActivityDetector that runs the Silero VAD v6.2.1 ONNX model in
// onnxruntime (the CPU provider). It consumes the canonical capture stream —
// 16 kHz, mono, f32 — in 512-sample frames (32 ms), prepends the 64-sample
// context tail of the previous frame (576 samples on the model input), carries
// the LSTM state between frames and reports the speech probability for each
// frame.
//
// The model bytes come from a Qt resource embedded in the application
// (":/models/silero_vad/silero_vad.onnx", declared in CMakeLists.txt's
// audio-assistant-models resource library). The model is a data asset of the
// repository — it is never downloaded at runtime. The full bytes are copied
// into the session at construction, so the resource buffer is not retained.
//
// onnxruntime is a third-party C++ library and its headers must never leak into
// this public interface: the pimpl below keeps every Ort::* object inside the
// .cpp, exactly like MiniaudioCaptureSource keeps miniaudio.h inside its .cpp.
// A consumer of the audio library only sees the narrow VoiceActivityDetector
// contract.
//
// Lifecycle: the detector has a valid model from construction or the
// constructor throws std::runtime_error. reset() returns it to its initial
// state (zero LSTM state, zero context tail) so a new independent stream can
// start. Not thread-safe: drive it from a single owner thread.
class SileroVoiceActivityDetector final : public VoiceActivityDetector {
public:
    // Loads the model from the embedded Qt resource. Throws std::runtime_error
    // if the resource is missing or onnxruntime fails to create a session.
    SileroVoiceActivityDetector();

    // Same as above but loads the model from the given absolute/relative path
    // instead of the Qt resource. The path is borrowed for the constructor
    // only. Used by the integration smoke test, which may run without the Qt
    // resource initialized; production code uses the default constructor.
    explicit SileroVoiceActivityDetector(const char* modelPath);

    ~SileroVoiceActivityDetector() override;

    SileroVoiceActivityDetector(const SileroVoiceActivityDetector&) = delete;
    SileroVoiceActivityDetector& operator=(const SileroVoiceActivityDetector&) = delete;
    SileroVoiceActivityDetector(SileroVoiceActivityDetector&&) = delete;
    SileroVoiceActivityDetector& operator=(SileroVoiceActivityDetector&&) = delete;

    // Processes one 512-sample frame (16 kHz, mono, f32) and returns the speech
    // probability in [0, 1]. The 64-sample tail of the previous frame is carried
    // internally and prepended on the model input. Precondition (rejected with
    // std::invalid_argument): frame.size() == 512.
    float process(std::span<const float> frame) override;

    // Clears the LSTM state and the context tail; the next process() call is the
    // first frame of a new independent stream.
    void reset() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audio
