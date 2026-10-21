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
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <vector>

#include "FakeAudioSource.h"
#include "FakeTranscriber.h"
#include "FakeVoiceActivityDetector.h"
#include "audio/AudioBuffer.h"
#include "audio/AudioFormat.h"
#include "audio/SpeechSegmenter.h"
#include "audio/VadSpeechGate.h"
#include "domain/AudioCapture.h"
#include "domain/ProviderError.h"

namespace {

// The VAD frame size in samples (32 ms at 16 kHz).
constexpr std::size_t kFrameSize = 512;

// Every sample of frame N carries the value N, so the returned block can be
// checked against the exact source range it was copied from.
std::vector<float> frameValues(std::size_t frameCount) {
    std::vector<float> samples(frameCount * kFrameSize);
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        std::fill_n(samples.begin() + static_cast<std::ptrdiff_t>(frame * kFrameSize),
                    kFrameSize,
                    static_cast<float>(frame));
    }
    return samples;
}

// The ring buffer capacity used by the delegation tests: 100 ms at the
// canonical 16 kHz stream (1600 samples).
constexpr auto kBufferDuration = std::chrono::milliseconds{100};

// The VAD tests need the full synthetic window to survive the ring buffer, so
// they use a generously large duration (2 s = 32000 samples) instead.
constexpr auto kVadBufferDuration = std::chrono::seconds{2};

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
    void test_vadGateSendsOnlyTheLastSpeechBlock();
    void test_vadGateWithNoSpeechFailsWithoutTranscriber();
    void test_vadGateShortFragmentFailsWithoutTranscriber();
    void test_vadGateLongEnoughFragmentTranscribes();
    void test_noVadGateSendsWholeBuffer();
    void test_captureWindowKeepsOnlyTheNewestSamples();
    void test_vadDisabledSendsTheWholeSnapshot();
    void test_minSpeechSecondsLowerBoundAllowsShorterSpeech();
    void test_minSpeechSecondsUpperBoundRejectsLongerSpeech();
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

// With a VAD gate attached, only the last detected utterance reaches the
// transcriber, extended by a 300 ms pre-roll and a 200 ms post-roll so word
// onsets and endings are not clipped. Earlier, genuinely old speech is dropped;
// an intra-phrase pause short enough to be the same utterance is kept (covered
// by tst_vadgate).
void TestAudioCapture::test_vadGateSendsOnlyTheLastSpeechBlock() {
    // 3 s buffer: 90 frames (46080 samples) leave room for a gap wider than the
    // 1.5 s utterance merge window between the two utterances.
    audio::AudioBuffer buffer(std::chrono::seconds{3}, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);
    FakeVoiceActivityDetector detector;
    audio::VadSpeechGate gate(detector, {0.5F, 0.15F, 2, 2});
    capture.setVadGate(gate);

    // Two utterances: frames 0..1 (an old remark) and 56..79 (24 frames =
    // 12288 speech samples, above the 12000 minimum). The 54-frame gap
    // (1.728 s) exceeds the utterance merge window, so only the second one
    // (plus padding) may be sent.
    constexpr std::size_t kFrameCount = 90;
    std::vector<float> probabilities(kFrameCount, 0.05F);
    probabilities[0] = 0.9F;
    probabilities[1] = 0.9F;
    for (std::size_t frame = 56; frame < 80; ++frame) {
        probabilities[frame] = 0.9F;
    }
    detector.setSequence(std::move(probabilities));
    buffer.write(frameValues(kFrameCount));

    capture.transcribeLast();

    QTRY_COMPARE(transcriber.callCount(), 1);
    // Last interval [56*512, 80*512) = [28672, 40960). Pre-roll 4800 reaches
    // 23872 (the silence after the first utterance), post-roll 3200 ends at
    // 44160, so the sent block is 20288 samples rather than the whole 46080.
    constexpr std::size_t kExpectedStart = 56 * kFrameSize - 4800;
    constexpr std::size_t kExpectedEnd = 80 * kFrameSize + 3200;
    QCOMPARE(transcriber.lastSamples().size(), kExpectedEnd - kExpectedStart);
    QCOMPARE(transcriber.lastSamples().front(), 46.0F);
    QCOMPARE(transcriber.lastSamples().back(), 86.0F);
}

// When the VAD-trimmed speech fragment is shorter than the minimum meaningful
// threshold (~0.75s = 12000 samples), the guard prevents calling the transcriber
// and reports a user-friendly error instead.
void TestAudioCapture::test_vadGateShortFragmentFailsWithoutTranscriber() {
    audio::AudioBuffer buffer(kVadBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);
    FakeVoiceActivityDetector detector;
    audio::VadSpeechGate gate(detector, {0.5F, 0.15F, 2, 2});
    capture.setVadGate(gate);

    // 6 frames: silence, speech, speech, silence...
    // The detected speech is frames 1..2 = 1024 samples; the padded block would
    // only reach 3072 samples, so the guard must judge the speech itself, not
    // the padding, and reject it as too short.
    detector.setSequence({0.05F, 0.9F, 0.9F, 0.05F, 0.05F, 0.05F});
    buffer.write(std::vector<float>(6 * std::size_t{512}, 0.5F));

    const QSignalSpy kFailedSpy(&capture, &AudioCapture::failed);
    capture.transcribeLast();

    QTRY_COMPARE(kFailedSpy.count(), 1);
    QCOMPARE(kFailedSpy.at(0).at(0).value<ProviderError>(), ProviderError::kInvalidResponse);
    // The transcriber must NOT be called for a fragment below the threshold.
    QCOMPARE(transcriber.callCount(), 0);
    // Error text is exposed on the view model.
    QVERIFY(capture.hasError());
    QVERIFY(!capture.errorText().isEmpty());
}

// When the VAD-trimmed speech fragment meets the minimum threshold, the
// transcriber is called normally (the guard must not block valid-length audio).
void TestAudioCapture::test_vadGateLongEnoughFragmentTranscribes() {
    audio::AudioBuffer buffer(kVadBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);
    FakeVoiceActivityDetector detector;
    audio::VadSpeechGate gate(detector, {0.5F, 0.15F, 2, 2});
    capture.setVadGate(gate);

    // 27 frames: silence, 25 speech frames, silence. The last interval is
    // [1*512, 26*512); the 300 ms pre-roll reaches sample 0 and the 200 ms
    // post-roll is clipped at the block end, so the whole 27-frame block (13824
    // samples, >= 12000) is forwarded.
    detector.setSequence({0.05F, 0.9F, 0.9F, 0.9F, 0.9F, 0.9F, 0.9F, 0.9F, 0.9F, 0.9F, 0.9F, 0.9F, 0.9F, 0.9F,
                          0.9F,  0.9F, 0.9F, 0.9F, 0.9F, 0.9F, 0.9F, 0.9F, 0.9F, 0.9F, 0.9F, 0.9F, 0.05F});
    buffer.write(std::vector<float>(27 * std::size_t{512}, 0.5F));

    const QSignalSpy kTranscribedSpy(&capture, &AudioCapture::transcribed);
    capture.transcribeLast();

    QTRY_COMPARE(kTranscribedSpy.count(), 1);
    QCOMPARE(transcriber.callCount(), 1);
    // The last speech block with its padding covers all 27 frames.
    QCOMPARE(transcriber.lastSamples().size(), 27 * std::size_t{512});
    QVERIFY(!capture.hasError());
}

// When VAD finds no speech at all, there is nothing to recognise. The request
// is reported locally as a failure instead of sending an empty block to the
// network, so the user gets an honest reason and no pointless round-trip.
void TestAudioCapture::test_vadGateWithNoSpeechFailsWithoutTranscriber() {
    audio::AudioBuffer buffer(kVadBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);
    FakeVoiceActivityDetector detector;
    audio::VadSpeechGate gate(detector, {0.5F, 0.15F, 2, 2});
    capture.setVadGate(gate);

    detector.setConstant(0.05F);
    buffer.write(std::vector<float>(4 * std::size_t{512}, 0.1F));

    const QSignalSpy kFailedSpy(&capture, &AudioCapture::failed);
    capture.transcribeLast();

    QTRY_COMPARE(kFailedSpy.count(), 1);
    QCOMPARE(kFailedSpy.at(0).at(0).value<ProviderError>(), ProviderError::kInvalidResponse);
    QCOMPARE(transcriber.callCount(), 0);
    QVERIFY(capture.hasError());
    QVERIFY(!capture.errorText().isEmpty());
}

// Without a gate the historical behaviour holds: the whole buffer is handed to
// the transcriber unchanged.
void TestAudioCapture::test_noVadGateSendsWholeBuffer() {
    audio::AudioBuffer buffer(kVadBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);

    const std::vector<float> kFrames(3 * std::size_t{512}, 0.5F);
    buffer.write(kFrames);

    capture.transcribeLast();

    QTRY_COMPARE(transcriber.callCount(), 1);
    QVERIFY(transcriber.lastSamples() == kFrames);
}

// The ring buffer may hold more audio than the configured capture window (it is
// sized for the largest selectable window), so a request must send only the
// newest captureWindowSeconds worth of samples.
void TestAudioCapture::test_captureWindowKeepsOnlyTheNewestSamples() {
    audio::AudioBuffer buffer(std::chrono::seconds(5), canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);
    capture.setCaptureOptions(1, true, 0.75);

    // Two seconds of canonical audio; each sample carries its own index so the
    // kept tail is unambiguous.
    constexpr std::size_t kSampleCount = 2 * std::size_t{16000};
    std::vector<float> samples(kSampleCount);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        samples[index] = static_cast<float>(index);
    }
    buffer.write(samples);

    capture.transcribeLast();

    QTRY_COMPARE(transcriber.callCount(), 1);
    const std::vector<float>& kSent = transcriber.lastSamples();
    QCOMPARE(kSent.size(), std::size_t{16000});
    QCOMPARE(kSent.front(), 16000.0F);
    QCOMPARE(kSent.back(), 31999.0F);
}

// With VAD disabled the gate is bypassed: the whole (window-trimmed) snapshot
// is sent even though the detector would report no speech.
void TestAudioCapture::test_vadDisabledSendsTheWholeSnapshot() {
    audio::AudioBuffer buffer(kVadBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);
    FakeVoiceActivityDetector detector;
    audio::VadSpeechGate gate(detector, {0.5F, 0.15F, 2, 2});
    capture.setVadGate(gate);
    capture.setCaptureOptions(20, false, 0.75);

    detector.setConstant(0.05F);
    const std::vector<float> kFrames(3 * std::size_t{512}, 0.5F);
    buffer.write(kFrames);

    capture.transcribeLast();

    QTRY_COMPARE(transcriber.callCount(), 1);
    QVERIFY(transcriber.lastSamples() == kFrames);
    QVERIFY(!capture.hasError());
}

// A lower minimum speech duration lets a fragment through that the default
// 0.75 s guard would reject.
void TestAudioCapture::test_minSpeechSecondsLowerBoundAllowsShorterSpeech() {
    audio::AudioBuffer buffer(kVadBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);
    FakeVoiceActivityDetector detector;
    audio::VadSpeechGate gate(detector, {0.5F, 0.15F, 2, 2});
    capture.setVadGate(gate);
    capture.setCaptureOptions(20, true, 0.25);

    // Silence, 10 speech frames (5120 speech samples), silence. 5120 is above
    // 0.25 s (4000 samples) but below the default 0.75 s (12000 samples).
    constexpr std::size_t kFrameCount = 12;
    std::vector<float> probabilities(kFrameCount, 0.05F);
    for (std::size_t frame = 1; frame <= 10; ++frame) {
        probabilities[frame] = 0.9F;
    }
    detector.setSequence(std::move(probabilities));
    buffer.write(std::vector<float>(kFrameCount * kFrameSize, 0.5F));

    capture.transcribeLast();

    QTRY_COMPARE(transcriber.callCount(), 1);
    QVERIFY(!capture.hasError());
}

// A higher minimum speech duration rejects a fragment that the default guard
// would have accepted.
void TestAudioCapture::test_minSpeechSecondsUpperBoundRejectsLongerSpeech() {
    audio::AudioBuffer buffer(kVadBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);
    FakeVoiceActivityDetector detector;
    audio::VadSpeechGate gate(detector, {0.5F, 0.15F, 2, 2});
    capture.setVadGate(gate);
    capture.setCaptureOptions(20, true, 3.0);

    // Silence, 39 speech frames (19968 speech samples), silence. That is above
    // the default 12000-sample guard but below 3.0 s (48000 samples).
    constexpr std::size_t kFrameCount = 41;
    std::vector<float> probabilities(kFrameCount, 0.05F);
    for (std::size_t frame = 1; frame <= 39; ++frame) {
        probabilities[frame] = 0.9F;
    }
    detector.setSequence(std::move(probabilities));
    buffer.write(std::vector<float>(kFrameCount * kFrameSize, 0.5F));

    const QSignalSpy kFailedSpy(&capture, &AudioCapture::failed);
    capture.transcribeLast();

    QTRY_COMPARE(kFailedSpy.count(), 1);
    QCOMPARE(kFailedSpy.at(0).at(0).value<ProviderError>(), ProviderError::kInvalidResponse);
    QCOMPARE(transcriber.callCount(), 0);
    QVERIFY(capture.hasError());
}

QTEST_GUILESS_MAIN(TestAudioCapture)

#include "tst_audiocapture.moc"
