#pragma once

#include <memory>
#include <optional>
#include <span>

#include "audio/AudioSink.h"
#include "audio/AudioSource.h"

// Forward declaration of miniaudio's context type. miniaudio.h is a
// third-party C header (a ~95k-line amalgamation) and must never leak into this
// public interface: the pimpl below keeps every miniaudio symbol inside the
// .cpp, so a consumer of the audio subsystem does not have to include it.
// NOLINTNEXTLINE(readability-identifier-naming): name is fixed by the C library.
struct ma_context;

namespace audio {

// Why the most recent start() attempt failed. It is a closed, strongly typed
// set instead of a raw ma_result value so callers neither depend on miniaudio
// nor compare against magic integers.
//
// The default int base is deliberate: narrowing it to one byte would introduce
// padding into the pimpl (an AudioFormat value plus this enum must fill an
// 8-byte-aligned tail exactly), which -Wpadded rejects and which buys nothing.
// NOLINTNEXTLINE(performance-enum-size)
enum class CaptureError {
    kNone,  // no failure recorded (start() was never attempted or succeeded)
    kDeviceInitFailed,
    kDeviceStartFailed,
};

// Concrete AudioSource that captures microphone audio through miniaudio and
// forwards it into the injected AudioSink. miniaudio normalises the device
// stream to the canonical capture format (16 kHz, mono, f32), so format() is a
// pure query and the sink never has to resample or convert.
//
// The frames are delivered from miniaudio's real-time data callback, therefore
// the sink must honour the AudioSink contract (non-blocking, allocation-free
// and no locking). The sink is borrowed: it must outlive the source. The
// destructor stops the device before the source is destroyed, so declaring the
// sink before the source (so that it is destroyed after it) is safe; the
// reverse order would leave the callback writing into a destroyed sink.
//
// Lifecycle: start() is idempotent (a second call while running is a no-op),
// stop() is idempotent and safe without a prior start(), and the destructor
// always stops and releases the device. start() reports failure through
// lastError() rather than throwing, since the AudioSource contract returns void.
class MiniaudioCaptureSource final : public AudioSource {
public:
    explicit MiniaudioCaptureSource(AudioSink& sink);

    // Seam used by tests to capture through an already-initialised miniaudio
    // context (for example a null-backend context) instead of the process
    // default. The context is borrowed: it must outlive the source and is never
    // released here. Production code uses the single-argument constructor.
    MiniaudioCaptureSource(AudioSink& sink, ma_context* context);

    ~MiniaudioCaptureSource() override;

    MiniaudioCaptureSource(const MiniaudioCaptureSource&) = delete;
    MiniaudioCaptureSource& operator=(const MiniaudioCaptureSource&) = delete;
    MiniaudioCaptureSource(MiniaudioCaptureSource&&) = delete;
    MiniaudioCaptureSource& operator=(MiniaudioCaptureSource&&) = delete;

    void start() override;
    void stop() override;

    [[nodiscard]] AudioFormat format() const noexcept override;

    // True while a device is open and running; false before start() and after
    // stop().
    [[nodiscard]] bool isRunning() const noexcept;

    // The failure of the most recent start() attempt, or nullopt when start()
    // has not failed. Only start() changes this; stop() leaves it untouched.
    [[nodiscard]] std::optional<CaptureError> lastError() const noexcept;

    // Seam for tests: the exact body of the real-time capture callback, exposed
    // so the sink-forwarding contract can be driven with synthetic frames
    // without opening a device. Production code calls it only from onFrames().
    void forwardFrames(std::span<const float> frames) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audio
