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
#include <QSignalSpy>
#include <QString>
#include <QtTest>
#include <chrono>
#include <vector>

#include "FakeAudioSource.h"
#include "FakeTranscriber.h"
#include "audio/AudioBuffer.h"
#include "audio/AudioFormat.h"
#include "domain/AudioCapture.h"
#include "domain/ProviderError.h"

namespace {

constexpr auto kBufferDuration = std::chrono::milliseconds{100};

}  // namespace

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
    void test_failedTranscriptionExposesErrorText();
    void test_newTranscribeClearsTheError();
    void test_cancelledFailureLeavesNoError();
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

// A failed request leaves a user-facing reason on the facade (the view mirrors
// it), the same way Chat surfaces a failed reply.
void TestAudioCapture::test_failedTranscriptionExposesErrorText() {
    audio::AudioBuffer buffer(kBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);
    QCOMPARE(capture.hasError(), false);
    transcriber.failWith(ProviderError::kAuthentication, QStringLiteral("bad key"));
    const QSignalSpy kErrorSpy(&capture, &AudioCapture::errorTextChanged);

    capture.transcribeLast();

    QTRY_COMPARE(capture.hasError(), true);
    QVERIFY(capture.errorText().contains(QStringLiteral("bad key")));
    QCOMPARE(kErrorSpy.count(), 1);
}

void TestAudioCapture::test_newTranscribeClearsTheError() {
    audio::AudioBuffer buffer(kBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);
    transcriber.failWith(ProviderError::kRateLimit, QStringLiteral("slow down"));
    capture.transcribeLast();
    QTRY_COMPARE(capture.hasError(), true);

    // A new request supersedes the previous failure (mirrors Chat::send).
    transcriber.succeedWith(QStringLiteral("ok"));
    const QSignalSpy kTranscribedSpy(&capture, &AudioCapture::transcribed);
    capture.transcribeLast();

    QCOMPARE(capture.hasError(), false);
    QCOMPARE(capture.errorText(), QString());
    QTRY_COMPARE(kTranscribedSpy.count(), 1);
}

// Cancellation is not a failure: it is relayed but leaves no error on the view.
void TestAudioCapture::test_cancelledFailureLeavesNoError() {
    audio::AudioBuffer buffer(kBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);
    transcriber.failWith(ProviderError::kCancelled, QStringLiteral("cancelled"));
    const QSignalSpy kFailedSpy(&capture, &AudioCapture::failed);

    capture.transcribeLast();

    QTRY_COMPARE(kFailedSpy.count(), 1);
    QCOMPARE(capture.hasError(), false);
    QCOMPARE(capture.errorText(), QString());
}

QTEST_GUILESS_MAIN(TestAudioCapture)

#include "tst_audiocapture.moc"
