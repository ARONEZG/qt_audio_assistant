// Tests for the concrete miniaudio capture source.
//
// They never open a real audio device:
//  * the forwarding contract (the real-time callback body) is driven directly
//    through the class's test seam with synthetic f32 frames;
//  * the lifecycle tests run against an explicit null-backend miniaudio context
//    so start()/stop() exercise the real miniaudio code path without touching
//    the microphone;
//  * the failure path is forced deterministically by passing a zero-initialised
//    context, which miniaudio rejects at device init.
// No sleep and no wall-clock dependency: everything is synchronous.
//
// Known coverage gap: the ma_device_start() failure branch (kDeviceStartFailed,
// including the ma_device_uninit() cleanup) is not exercised. miniaudio offers
// no public, deterministic way to make the null backend fail to start - its
// start always succeeds - and adding a production seam only to force that
// failure would test a stub rather than the backend integration. The branch is
// therefore kept as a defensive path: a device that initialised but failed to
// start is released without leaving the source half-open.
#include <miniaudio.h>

#include <QtTest>
#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

#include "audio/AudioFormat.h"
#include "audio/AudioSink.h"
#include "audio/MiniaudioCaptureSource.h"

namespace {

// Test double for the narrow AudioSink port: records every forwarded block in
// order. It only implements write(), exactly like a real consumer-side buffer.
class RecordingSink final : public audio::AudioSink {
public:
    void write(std::span<const float> frames) override {
        frames_.insert(frames_.end(), frames.begin(), frames.end());
    }

    [[nodiscard]] const std::vector<float>& frames() const noexcept {
        return frames_;
    }

private:
    std::vector<float> frames_;
};

// Owns a miniaudio context that only knows the null backend, so the capture
// source can be started and stopped without a real device. A failed context
// init aborts the test through an exception instead of being carried around as
// an extra status member (which would only add tail padding to the wrapper).
class NullBackendContext {
public:
    NullBackendContext() {
        const std::array<ma_backend, 1U> kBackends{ma_backend_null};
        if (ma_context_init(kBackends.data(), 1U, nullptr, &context_) != MA_SUCCESS) {
            throw std::runtime_error("null-backend miniaudio context init failed");
        }
    }

    ~NullBackendContext() {
        ma_context_uninit(&context_);
    }

    NullBackendContext(const NullBackendContext&) = delete;
    NullBackendContext& operator=(const NullBackendContext&) = delete;
    NullBackendContext(NullBackendContext&&) = delete;
    NullBackendContext& operator=(NullBackendContext&&) = delete;

    [[nodiscard]] ma_context* context() noexcept {
        return &context_;
    }

private:
    ma_context context_{};
};

// Starts the source and reports whether the device actually runs. Returning a
// bool (instead of asserting inside a helper) keeps QVERIFY in the test slot,
// where a failure aborts the test rather than only the helper.
bool startSource(audio::MiniaudioCaptureSource& source) {
    source.start();
    return source.isRunning();
}

// Runs a source until this function returns, which destroys it and therefore
// exercises the destructor. Returns whether the device was running before the
// destruction.
bool runSourceToDestruction(NullBackendContext& context, RecordingSink& sink) {
    audio::MiniaudioCaptureSource source(sink, context.context());
    return startSource(source);
}

}  // namespace

class TestMiniaudioCaptureSource : public QObject {
    Q_OBJECT

public:
    explicit TestMiniaudioCaptureSource(QObject* parent = nullptr);

private slots:
    void test_formatIsCanonical();
    void test_forwardsExactFrameBlock();
    void test_emptyFrameBlockKeepsSinkUnchanged();
    void test_startStopLifecycleIsIdempotent();
    void test_destructorStopsRunningDevice();
    void test_borrowedContextSurvivesSourceDestruction();
    void test_deviceInitFailureIsReported();
    void test_stopPreservesLastError();
};

TestMiniaudioCaptureSource::TestMiniaudioCaptureSource(QObject* parent) : QObject(parent) {
}

void TestMiniaudioCaptureSource::test_formatIsCanonical() {
    RecordingSink sink;
    const audio::MiniaudioCaptureSource kSource(sink);

    QCOMPARE(kSource.format(), audio::AudioFormat(16000U, 1U, audio::SampleFormat::kFloat32));
}

void TestMiniaudioCaptureSource::test_forwardsExactFrameBlock() {
    RecordingSink sink;
    const audio::MiniaudioCaptureSource kSource(sink);
    const std::vector<float> kFrames{0.0F, 0.5F, -0.5F, 1.0F, -1.0F};

    kSource.forwardFrames(kFrames);

    QCOMPARE(sink.frames().size(), kFrames.size());
    QVERIFY(sink.frames() == kFrames);
}

void TestMiniaudioCaptureSource::test_emptyFrameBlockKeepsSinkUnchanged() {
    RecordingSink sink;
    const audio::MiniaudioCaptureSource kSource(sink);
    const std::vector<float> kFirst{0.25F, -0.25F};
    kSource.forwardFrames(kFirst);

    kSource.forwardFrames(std::span<const float>{});

    QVERIFY(sink.frames() == kFirst);
}

void TestMiniaudioCaptureSource::test_startStopLifecycleIsIdempotent() {
    NullBackendContext context;
    RecordingSink sink;
    audio::MiniaudioCaptureSource source(sink, context.context());

    QVERIFY(!source.isRunning());
    QVERIFY(!source.lastError().has_value());

    source.start();
    QVERIFY(source.isRunning());
    QVERIFY(!source.lastError().has_value());
    source.start();  // redundant start is a no-op
    QVERIFY(source.isRunning());

    source.stop();
    QVERIFY(!source.isRunning());
    source.stop();  // redundant stop is a no-op
    QVERIFY(!source.isRunning());
}

void TestMiniaudioCaptureSource::test_destructorStopsRunningDevice() {
    NullBackendContext context;
    RecordingSink sink;

    // The destructor runs when the helper returns; a crash or leak would fail
    // the test under the sanitizer build.
    QVERIFY(runSourceToDestruction(context, sink));
}

void TestMiniaudioCaptureSource::test_borrowedContextSurvivesSourceDestruction() {
    NullBackendContext context;
    RecordingSink firstSink;
    QVERIFY(runSourceToDestruction(context, firstSink));

    // The context is borrowed, so it must still be usable after the first
    // source is gone; this proves the destructor did not release it.
    RecordingSink secondSink;
    audio::MiniaudioCaptureSource second(secondSink, context.context());
    QVERIFY(startSource(second));
    second.stop();
}

void TestMiniaudioCaptureSource::test_deviceInitFailureIsReported() {
    RecordingSink sink;
    ma_context uninitialized{};
    audio::MiniaudioCaptureSource source(sink, &uninitialized);

    source.start();

    QVERIFY(!source.isRunning());
    QCOMPARE(source.lastError().value_or(audio::CaptureError::kNone), audio::CaptureError::kDeviceInitFailed);

    source.stop();  // no-op: the device was never created
    QVERIFY(!source.isRunning());
}

void TestMiniaudioCaptureSource::test_stopPreservesLastError() {
    RecordingSink sink;
    ma_context uninitialized{};
    audio::MiniaudioCaptureSource source(sink, &uninitialized);
    source.start();

    source.stop();

    QCOMPARE(source.lastError().value_or(audio::CaptureError::kNone), audio::CaptureError::kDeviceInitFailed);
}

QTEST_GUILESS_MAIN(TestMiniaudioCaptureSource)

#include "tst_miniaudiocapturesource.moc"
