#include "audio/MiniaudioCaptureSource.h"

#include <miniaudio.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>

namespace audio {

namespace {

// The canonical capture format of the pipeline (see AudioSink.h / AudioFormat.h):
// miniaudio resamples and downmixes the device stream to this on its side.
constexpr std::uint32_t kSampleRate = 16000U;
constexpr std::uint32_t kChannels = 1U;

// Body of miniaudio's real-time data callback. For a capture device `input` is
// the captured PCM (f32, mono, canonical rate) and `output` is null; the pimpl
// owner is passed through pUserData. It must stay branch-free apart from the
// forward call: no allocation, no locking, no logging.
void onFrames(ma_device* device, void* output, const void* input, ma_uint32 frameCount) {
    (void)output;
    const auto* source = static_cast<const MiniaudioCaptureSource*>(device->pUserData);
    source->forwardFrames(
            std::span<const float>(static_cast<const float*>(input), static_cast<std::size_t>(frameCount)));
}

}  // namespace

struct MiniaudioCaptureSource::Impl {
    Impl(AudioSink& sink, ma_context* context)
        : sink_(&sink), context_(context), format_(kSampleRate, kChannels, SampleFormat::kFloat32) {
    }

private:
    friend class MiniaudioCaptureSource;

    AudioSink* sink_;
    ma_context* context_;
    std::unique_ptr<ma_device> device_;
    AudioFormat format_;
    CaptureError error_ = CaptureError::kNone;
};

MiniaudioCaptureSource::MiniaudioCaptureSource(AudioSink& sink) : MiniaudioCaptureSource(sink, nullptr) {
}

MiniaudioCaptureSource::MiniaudioCaptureSource(AudioSink& sink, ma_context* context)
    : impl_(std::make_unique<Impl>(sink, context)) {
}

MiniaudioCaptureSource::~MiniaudioCaptureSource() {
    stop();
}

void MiniaudioCaptureSource::start() {
    if (impl_->device_ != nullptr) {
        return;  // already running; start() is idempotent
    }

    impl_->error_ = CaptureError::kNone;

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.sampleRate = kSampleRate;
    config.capture.format = ma_format_f32;
    config.capture.channels = kChannels;
    config.dataCallback = &onFrames;
    config.pUserData = this;

    auto device = std::make_unique<ma_device>();
    if (ma_device_init(impl_->context_, &config, device.get()) != MA_SUCCESS) {
        impl_->error_ = CaptureError::kDeviceInitFailed;
        return;
    }

    if (ma_device_start(device.get()) != MA_SUCCESS) {
        ma_device_uninit(device.get());
        impl_->error_ = CaptureError::kDeviceStartFailed;
        return;
    }

    impl_->device_ = std::move(device);
}

void MiniaudioCaptureSource::stop() {
    if (impl_->device_ == nullptr) {
        return;  // never started or already stopped; stop() is idempotent
    }

    ma_device_stop(impl_->device_.get());
    ma_device_uninit(impl_->device_.get());
    impl_->device_.reset();
}

AudioFormat MiniaudioCaptureSource::format() const noexcept {
    return impl_->format_;
}

bool MiniaudioCaptureSource::isRunning() const noexcept {
    return impl_->device_ != nullptr && ma_device_is_started(impl_->device_.get()) == MA_TRUE;
}

std::optional<CaptureError> MiniaudioCaptureSource::lastError() const noexcept {
    if (impl_->error_ == CaptureError::kNone) {
        return std::nullopt;
    }
    return impl_->error_;
}

void MiniaudioCaptureSource::forwardFrames(std::span<const float> frames) const {
    impl_->sink_->write(frames);
}

}  // namespace audio
