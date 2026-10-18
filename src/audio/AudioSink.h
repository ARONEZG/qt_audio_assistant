#pragma once

#include <span>

namespace audio {

// The port an audio source writes captured PCM into. It is the seam between the
// real-time capture side (microphone / system loopback, later backed by
// miniaudio) and the consumer (ring buffer, transcriber): the source knows only
// "here is a buffer of frames", it never owns or drives the consumer.
//
// Contract for implementations of write():
//  * non-blocking - it is called from the real-time audio callback;
//  * no heap allocation and no locking in that callback;
//  * the frames buffer is borrowed for the duration of the call only and must
//    not be retained; copy what is needed instead.
//
// Frames are raw decoded PCM in the source's format() (canonical: 16 kHz, mono,
// f32 in [-1, 1]).
class AudioSink {
public:
    AudioSink() = default;
    virtual ~AudioSink();

    AudioSink(const AudioSink&) = delete;
    AudioSink& operator=(const AudioSink&) = delete;
    AudioSink(AudioSink&&) = delete;
    AudioSink& operator=(AudioSink&&) = delete;

    virtual void write(std::span<const float> frames) = 0;
};

}  // namespace audio
