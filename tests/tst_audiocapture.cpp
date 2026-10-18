// Tests for the capture-pipeline facade (AudioCapture).
//
// AudioCapture only delegates to its collaborators and relays the transcriber's
// terminal signals as its own, so both wide interfaces are replaced with
// doubles: a no-op source (the narrow AudioSource role) and a transcriber that
// records the samples it was handed and reports its terminal signal from the
// event loop, exactly as the TranscriptionProvider contract requires. The ring
// buffer is the real AudioBuffer, because the "snapshot into transcribe()"
// hand-off is the contract under test.
//
// No device, no network, no sleep: the only asynchrony is the transcriber's
// queued emission, awaited with QTRY_*.
#include <QMetaObject>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QtTest>
#include <chrono>
#include <utility>
#include <vector>

#include "app/AudioCapture.h"
#include "audio/AudioBuffer.h"
#include "audio/AudioFormat.h"
#include "audio/AudioSource.h"
#include "domain/ProviderError.h"
#include "domain/TranscriptionProvider.h"

namespace {

constexpr auto kBufferDuration = std::chrono::milliseconds{100};

// The canonical capture stream: 16 kHz, mono, f32 (see AudioFormat.h).
audio::AudioFormat canonicalFormat() {
    return {16000U, 1U, audio::SampleFormat::kFloat32};
}

// Double for the AudioSource role: counts the lifecycle commands and answers
// format() with the canonical stream without ever touching a device.
class FakeAudioSource final : public audio::AudioSource {
public:
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

}  // namespace

// Double for the TranscriptionProvider role: records the samples it was handed
// and emits exactly one terminal signal, always from the event loop (never
// synchronously inside transcribe()), mirroring the real network adapter.
class FakeTranscriber final : public TranscriptionProvider {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(FakeTranscriber)

    // Ordered widest-first so the class carries no internal padding.
    std::vector<float> lastSamples_;
    QString text_;
    QString message_;
    int callCount_ = 0;
    ProviderError error_ = ProviderError::kUnknown;
    bool succeeds_ = true;

public:
    explicit FakeTranscriber(QObject* parent = nullptr) : TranscriptionProvider(parent) {
    }
    ~FakeTranscriber() override = default;

    void transcribe(std::vector<float> samples) override {
        lastSamples_ = std::move(samples);
        ++callCount_;
        // The queued string invocation defers to the event loop without
        // allocating a callable on the heap (which clang-analyzer misreports as
        // a leak); the terminal signal is emitted from emitPendingResult().
        QMetaObject::invokeMethod(this, "emitPendingResult", Qt::QueuedConnection);
    }

    // Configures the outcome the queued emission reports.
    void succeedWith(QString text) {
        succeeds_ = true;
        text_ = std::move(text);
    }
    void failWith(ProviderError error, QString message) {
        succeeds_ = false;
        error_ = error;
        message_ = std::move(message);
    }

    [[nodiscard]] int callCount() const noexcept {
        return callCount_;
    }
    [[nodiscard]] const std::vector<float>& lastSamples() const noexcept {
        return lastSamples_;
    }

private slots:
    // Emits the configured terminal signal from the event loop, so transcribe()
    // never emits synchronously (the TranscriptionProvider contract).
    void emitPendingResult() {
        if (succeeds_) {
            emit transcribed(text_);
        } else {
            emit failed(error_, message_);
        }
    }
};

class TestAudioCapture : public QObject {
    Q_OBJECT

public:
    explicit TestAudioCapture(QObject* parent = nullptr);

private slots:
    void test_transcribeLastHandsTheSnapshotToTheTranscriber();
    void test_emptyBufferTranscribesEmptyVector();
    void test_transcribedIsRelayed();
    void test_failedIsRelayed();
    void test_terminalSignalIsNotSynchronous();
    void test_startStopDelegateToSource();
    void test_formatDelegatesToSource();
};

TestAudioCapture::TestAudioCapture(QObject* parent) : QObject(parent) {
}

void TestAudioCapture::test_transcribeLastHandsTheSnapshotToTheTranscriber() {
    audio::AudioBuffer buffer(kBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);
    const std::vector<float> kFrames{0.0F, 0.5F, -0.5F, 1.0F, -1.0F};
    buffer.write(kFrames);

    capture.transcribeLast();

    QTRY_COMPARE(transcriber.callCount(), 1);
    // Exactly the frames the buffer holds, in order, and nothing else.
    QVERIFY(transcriber.lastSamples() == buffer.snapshot());
}

void TestAudioCapture::test_emptyBufferTranscribesEmptyVector() {
    audio::AudioBuffer buffer(kBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);

    capture.transcribeLast();

    // An empty recording is still a valid request: the transcriber is called
    // with an empty block rather than skipped.
    QTRY_COMPARE(transcriber.callCount(), 1);
    QVERIFY(transcriber.lastSamples().empty());
}

void TestAudioCapture::test_transcribedIsRelayed() {
    audio::AudioBuffer buffer(kBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);
    transcriber.succeedWith(QStringLiteral("привет мир"));
    const QSignalSpy kTranscribedSpy(&capture, &AudioCapture::transcribed);
    const QSignalSpy kFailedSpy(&capture, &AudioCapture::failed);

    capture.transcribeLast();

    QTRY_COMPARE(kTranscribedSpy.count(), 1);
    QCOMPARE(kTranscribedSpy.at(0).at(0).toString(), QStringLiteral("привет мир"));
    QCOMPARE(kFailedSpy.count(), 0);
}

void TestAudioCapture::test_failedIsRelayed() {
    audio::AudioBuffer buffer(kBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);
    transcriber.failWith(ProviderError::kNetwork, QStringLiteral("boom"));
    const QSignalSpy kTranscribedSpy(&capture, &AudioCapture::transcribed);
    const QSignalSpy kFailedSpy(&capture, &AudioCapture::failed);

    capture.transcribeLast();

    QTRY_COMPARE(kFailedSpy.count(), 1);
    QCOMPARE(kFailedSpy.at(0).at(0).value<ProviderError>(), ProviderError::kNetwork);
    QCOMPARE(kFailedSpy.at(0).at(1).toString(), QStringLiteral("boom"));
    QCOMPARE(kTranscribedSpy.count(), 0);
}

void TestAudioCapture::test_terminalSignalIsNotSynchronous() {
    audio::AudioBuffer buffer(kBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);
    transcriber.succeedWith(QStringLiteral("later"));
    const QSignalSpy kTranscribedSpy(&capture, &AudioCapture::transcribed);

    capture.transcribeLast();

    // transcribeLast() returns before the terminal signal: the facade must not
    // emit synchronously on behalf of the transcriber.
    QCOMPARE(kTranscribedSpy.count(), 0);
    QTRY_COMPARE(kTranscribedSpy.count(), 1);
}

void TestAudioCapture::test_startStopDelegateToSource() {
    audio::AudioBuffer buffer(kBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);

    capture.start();
    QCOMPARE(source.startCount(), 1);
    QCOMPARE(source.stopCount(), 0);

    capture.stop();
    QCOMPARE(source.startCount(), 1);
    QCOMPARE(source.stopCount(), 1);
}

void TestAudioCapture::test_formatDelegatesToSource() {
    audio::AudioBuffer buffer(kBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    const AudioCapture kCapture(buffer, source, transcriber);

    QCOMPARE(kCapture.format(), canonicalFormat());
}

QTEST_GUILESS_MAIN(TestAudioCapture)

#include "tst_audiocapture.moc"
