#include "audio/AudioBuffer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

// Validates the explicit-capacity invariant before the storage is allocated, so
// an invalid argument fails without touching the heap. A free function in an
// anonymous namespace keeps the check out of the class (no static member).
std::size_t checkedFrameCapacity(std::size_t frameCapacity) {
    if (frameCapacity == 0U) {
        throw std::invalid_argument("AudioBuffer requires a positive frame capacity");
    }
    return frameCapacity;
}

// Converts a duration into a frame count for the given format. Rounds up so the
// buffer holds at least the requested span of time. Rejects a non-positive
// duration and a count that would not fit in std::size_t; an infinite product is
// rejected by the same upper-bound comparison, and a NaN duration is caught by
// the positivity guard, so no separate finiteness check is needed.
std::size_t frameCapacityFor(std::chrono::duration<double> duration, audio::AudioFormat format) {
    const double kSeconds = duration.count();
    if (!(kSeconds > 0.0)) {
        throw std::invalid_argument("AudioBuffer requires a positive duration");
    }
    const double kExactFrames =
            kSeconds * static_cast<double>(format.sampleRate()) * static_cast<double>(format.channels());
    const auto kMaxFrames = static_cast<double>(std::numeric_limits<std::size_t>::max());
    if (kExactFrames > kMaxFrames) {
        throw std::invalid_argument("AudioBuffer duration exceeds the addressable frame capacity");
    }
    return static_cast<std::size_t>(std::ceil(kExactFrames));
}

}  // namespace

namespace audio {

AudioBuffer::AudioBuffer(std::size_t frameCapacity)
    : capacity_(checkedFrameCapacity(frameCapacity)), storage_(capacity_, 0.0F) {
}

AudioBuffer::AudioBuffer(std::chrono::duration<double> duration, AudioFormat format)
    : AudioBuffer(frameCapacityFor(duration, format)) {
}

void AudioBuffer::write(std::span<const float> frames) {
    if (frames.empty()) {
        return;
    }

    // Open the seqlock: an odd sequence tells a concurrent snapshot() to retry
    // while this update is in flight (see the class comment for the protocol).
    sequence_.fetch_add(1U, std::memory_order_relaxed);

    if (frames.size() >= capacity_) {
        // Only the newest capacity_ frames survive; trimming here means the
        // index arithmetic below wraps at most once.
        frames = frames.last(capacity_);
    }

    std::size_t index = writeIndex_;
    for (const float kSample : frames) {
        storage_[index] = kSample;
        index = (index + 1U) % capacity_;
    }

    writeIndex_ = index;
    size_ = std::min(size_ + frames.size(), capacity_);

    // Close the seqlock with release semantics: the frame writes above
    // happen-before any reader that observes the new sequence value.
    sequence_.fetch_add(1U, std::memory_order_release);
}

std::vector<float> AudioBuffer::snapshot() const {
    std::vector<float> result(capacity_);
    for (;;) {
        const std::uint64_t kSequence = sequence_.load(std::memory_order_acquire);
        if ((kSequence & 1U) != 0U) {
            continue;  // writer mid-update: retry with a fresh sequence
        }

        const std::size_t kSize = size_;
        std::size_t index = (writeIndex_ + capacity_ - kSize) % capacity_;
        for (std::size_t i = 0; i < kSize; ++i) {
            result[i] = storage_[index];
            index = (index + 1U) % capacity_;
        }

        if (sequence_.load(std::memory_order_acquire) == kSequence) {
            result.resize(kSize);
            return result;
        }
        // A write landed while copying; retry to return a consistent window.
    }
}

std::size_t AudioBuffer::capacity() const noexcept {
    return capacity_;
}

}  // namespace audio
