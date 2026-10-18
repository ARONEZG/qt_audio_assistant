#pragma once

#include "audio/AudioFormat.h"

namespace audio {

// Role interface of an audio capture source (microphone or system loopback).
//
// start()/stop() are commands: they change whether the source is producing
// frames, and are intended to be driven from the owning thread / event loop,
// not from the capture callback. format() is the query a consumer uses before
// starting to size its buffers.
//
// A concrete source does not implement the consumer side: it is constructed
// with (or otherwise given) the AudioSink its capture callback writes into, so
// the dependency is a constructor parameter rather than a global or a lookup.
class AudioSource {
public:
    AudioSource() = default;
    virtual ~AudioSource();

    AudioSource(const AudioSource&) = delete;
    AudioSource& operator=(const AudioSource&) = delete;
    AudioSource(AudioSource&&) = delete;
    AudioSource& operator=(AudioSource&&) = delete;

    virtual void start() = 0;
    virtual void stop() = 0;

    [[nodiscard]] virtual AudioFormat format() const noexcept = 0;
};

}  // namespace audio
