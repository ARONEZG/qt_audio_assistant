#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "audio/AudioFormat.h"
#include "audio/AudioSink.h"

namespace audio {

// A bounded ring buffer that keeps the most recent PCM frames written into it.
// It is the concrete AudioSink of the capture pipeline: a source writes decoded
// frames here, and the consumer asks for a stable copy of the last N seconds
// (for transcription) without ever touching a real device.
//
// Storage and capacity are fixed at construction (the constructor's capacity is
// allocated once and never resized), so write() performs no heap allocation and
// takes no lock - it is safe to call from a real-time audio callback. When more
// frames arrive than the capacity holds, the oldest frames are overwritten:
// after write() the buffer always holds the newest min(total, capacity) frames
// in chronological order, newest last.
//
// Thread-safety model - single-producer / single-consumer (SPSC):
//  * exactly one writer thread (the capture callback) calls write();
//  * exactly one consumer thread reads snapshots();
//  * construction and destruction happen while no other thread is using the
//    object.
// The writer never blocks and never waits for a reader. The reader uses a
// seqlock: write() publishes an odd sequence counter before touching the data
// and an even one (with release semantics) after, while snapshot() reads the
// counter with acquire semantics before and after copying and retries if it
// changed or was odd. A snapshot is therefore a consistent copy of one window,
// never a half-updated mix. The cost of this is that a snapshot() call can spin
// while the writer is mid-update; that is acceptable because it runs on the
// consumer thread, not in the audio callback.
//
// Every value type here is STL-only (no Qt, no platform code): the contract is
// identical on Linux, Windows and macOS.
class AudioBuffer final : public AudioSink {
public:
    // Precondition (rejected with std::invalid_argument): frameCapacity > 0.
    // A frame is one interleaved sample slot, i.e. one element of the span
    // passed to write().
    explicit AudioBuffer(std::size_t frameCapacity);

    // Capacity expressed as a duration: the buffer holds
    // ceil(duration * sampleRate * channels) frames, so "the last N seconds" is
    // correct for any channel count. Precondition (rejected with
    // std::invalid_argument): duration > 0 and the resulting frame count is
    // representable as std::size_t. AudioFormat is passed by value because it
    // is a small trivially copyable value type.
    AudioBuffer(std::chrono::duration<double> duration, AudioFormat format);

    // Appends frames to the ring; on overflow the oldest frames are dropped.
    // Non-blocking, allocation-free and lock-free. The span is borrowed for the
    // duration of the call only - the samples are copied into the ring.
    void write(std::span<const float> frames) override;

    // Returns a copy of the accumulated frames in chronological order (oldest
    // first, newest last). The copy is independent of the buffer: mutating it
    // does not affect the buffer, and the buffer is not modified by the call.
    [[nodiscard]] std::vector<float> snapshot() const;

    // The fixed frame capacity chosen at construction.
    [[nodiscard]] std::size_t capacity() const noexcept;

private:
    std::size_t capacity_;
    std::vector<float> storage_;
    std::size_t writeIndex_ = 0;
    std::size_t size_ = 0;
    mutable std::atomic<std::uint64_t> sequence_{0};
};

}  // namespace audio
