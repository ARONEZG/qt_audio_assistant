#pragma once

#include <span>

namespace audio {

// The port a voice-activity detector (VAD) implements. It consumes the
// canonical capture stream — 16 kHz, mono, f32 — one frame at a time and
// reports the speech probability for each frame. The domain code (the audio
// pipeline, the transcriber gate) depends only on this narrow role interface;
// ONNX / Silero / any other inference backend stays hidden behind it.
//
// Contract:
//  * process() accepts frames in chronological order and returns a probability
//    in [0, 1] that speech is present in the delivered frame. It is a stateful
//    stream call: consecutive calls are a single continuous stream.
//  * reset() starts a new independent stream: it clears all internal state
//    (model state, chunking buffer, context tail) so the next process() call is
//    the first frame of a brand-new utterance. It is safe to call at any time
//    and is a no-op on a fresh detector.
//  * Implementations are not required to be thread-safe: a single owner thread
//    drives the detector.
//
// A "frame" here is one VAD-sized unit (for the Silero adapter: 512 samples),
// not one PCM sample. The adapter is responsible for cutting the stream into
// frames and carrying the context (64-sample tail) between them.
class VoiceActivityDetector {
public:
    VoiceActivityDetector() = default;
    virtual ~VoiceActivityDetector();

    VoiceActivityDetector(const VoiceActivityDetector&) = delete;
    VoiceActivityDetector& operator=(const VoiceActivityDetector&) = delete;
    VoiceActivityDetector(VoiceActivityDetector&&) = delete;
    VoiceActivityDetector& operator=(VoiceActivityDetector&&) = delete;

    virtual float process(std::span<const float> frame) = 0;
    virtual void reset() = 0;
};

}  // namespace audio
