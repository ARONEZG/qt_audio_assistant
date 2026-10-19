// Tests for the VAD framing/gating layer (VadSpeechGate).
//
// The gate cuts canonical PCM (16 kHz, mono, f32) into 512-sample frames,
// feeds them through the injected VoiceActivityDetector, and builds per-sample
// keep-masks / trimmed output for the captured stream. The detector is a
// deterministic double (FakeVoiceActivityDetector), so no ONNX model is loaded:
// the framing, partial-frame carry, and mask mapping are the contract under
// test.
#include <QtTest>
#include <span>
#include <vector>

#include "FakeVoiceActivityDetector.h"
#include "audio/SpeechSegmenter.h"
#include "audio/VadSpeechGate.h"
#include "audio/VoiceActivityDetector.h"

namespace {

audio::SpeechSegmenter::Parameters defaultParameters() {
    // Low thresholds so a constant-probability fake drives clean intervals.
    return {0.5F, 0.15F, 2, 2};
}

}  // namespace

class TestVadGate : public QObject {
    Q_OBJECT

public:
    explicit TestVadGate(QObject* parent = nullptr);

private slots:
    void test_feedSamplesCutsFramesAndReportsIntervals();
    void test_partialFrameIsCarriedToTheNextFeed();
    void test_keepMaskMarksSpeechSamples();
    void test_trimSilenceKeepsOnlySpeech();
    void test_trimSilenceWithNoSpeechReturnsEmpty();
    void test_resetClearsStateAndResetsDetector();
    void test_consumedSamplesCountsAllFeededSamples();
};

TestVadGate::TestVadGate(QObject* parent) : QObject(parent) {
}

void TestVadGate::test_feedSamplesCutsFramesAndReportsIntervals() {
    FakeVoiceActivityDetector detector;
    detector.setConstant(0.9F);  // every frame is speech
    audio::VadSpeechGate gate(detector, defaultParameters());

    // 3 full frames of speech.
    const std::vector<float> kSamples(3 * std::size_t{512}, 0.5F);
    gate.feedSamples(kSamples);

    QCOMPARE(detector.processCount(), 3);
    QCOMPARE(gate.intervals().size(), std::size_t{1});
    QCOMPARE(gate.intervals()[0].start(), std::size_t{0});
    QCOMPARE(gate.intervals()[0].end(), std::size_t{3});
}

void TestVadGate::test_partialFrameIsCarriedToTheNextFeed() {
    FakeVoiceActivityDetector detector;
    detector.setConstant(0.9F);
    audio::VadSpeechGate gate(detector, defaultParameters());

    // Feed 1.5 frames, then 0.5+2 more: the partial frame must be carried and
    // completed by the second feed, not lost.
    std::vector<float> first(std::size_t{512} + 256, 0.5F);
    gate.feedSamples(first);
    QCOMPARE(detector.processCount(), 1);  // only the complete frame so far

    std::vector<float> second(std::size_t{256} + 2 * std::size_t{512}, 0.5F);
    gate.feedSamples(second);
    // First feed held 256 samples as a partial frame; the second feed
    // completes it (256+256=512) plus 2 more full frames.
    QCOMPARE(detector.processCount(), 4);
}

void TestVadGate::test_keepMaskMarksSpeechSamples() {
    FakeVoiceActivityDetector detector;
    audio::VadSpeechGate gate(detector, defaultParameters());
    // Frames: speech, speech, silence (constant probabilities switched per frame
    // needs sequence mode; use 0.9 for first two frames and 0.05 after).
    detector.setSequence({0.9F, 0.9F, 0.05F, 0.05F, 0.05F, 0.05F});

    const std::vector<float> kSamples(4 * std::size_t{512}, 0.5F);
    gate.feedSamples(kSamples);

    const std::vector<bool> kMask = gate.keepMask(kSamples.size());
    QCOMPARE(kMask.size(), kSamples.size());
    // Frames 0..1 are speech -> samples 0..1023 kept; frames 2..3 silence.
    constexpr std::size_t kSpeechSamples = 2 * std::size_t{512};
    for (std::size_t i = 0; i < kSpeechSamples; ++i) {
        QVERIFY(kMask[i]);
    }
    for (std::size_t i = kSpeechSamples; i < kMask.size(); ++i) {
        QVERIFY(!kMask[i]);
    }
}

void TestVadGate::test_trimSilenceKeepsOnlySpeech() {
    FakeVoiceActivityDetector detector;
    audio::VadSpeechGate gate(detector, defaultParameters());
    detector.setSequence({0.05F, 0.9F, 0.9F, 0.05F, 0.05F, 0.05F});

    // 6 frames: silence, speech, speech, silence, silence, silence.
    const std::vector<float> kSamples(6 * std::size_t{512}, 0.5F);
    const std::vector<float> kTrimmed = gate.trimSilence(kSamples);

    // minSpeechFrames=2: the 2-frame speech run survives; the surrounding
    // silence (and trailing silence after the run) is removed.
    QCOMPARE(kTrimmed.size(), std::size_t{1024});
    QVERIFY(!kTrimmed.empty());
    for (const float kSample : kTrimmed) {
        QCOMPARE(kSample, 0.5F);
    }
}

void TestVadGate::test_trimSilenceWithNoSpeechReturnsEmpty() {
    FakeVoiceActivityDetector detector;
    detector.setConstant(0.05F);
    audio::VadSpeechGate gate(detector, defaultParameters());

    const std::vector<float> kSamples(8 * std::size_t{512}, 0.1F);
    const std::vector<float> kTrimmed = gate.trimSilence(kSamples);

    QVERIFY(kTrimmed.empty());
}

void TestVadGate::test_resetClearsStateAndResetsDetector() {
    FakeVoiceActivityDetector detector;
    detector.setConstant(0.9F);
    audio::VadSpeechGate gate(detector, defaultParameters());
    gate.feedSamples(std::vector<float>(3 * std::size_t{512}, 0.5F));
    QVERIFY(!gate.intervals().empty());

    gate.reset();

    QCOMPARE(detector.resetCount(), 1);
    QVERIFY(gate.intervals().empty());
    QCOMPARE(gate.consumedSamples(), std::size_t{0});
}

void TestVadGate::test_consumedSamplesCountsAllFeededSamples() {
    FakeVoiceActivityDetector detector;
    audio::VadSpeechGate gate(detector, defaultParameters());

    gate.feedSamples(std::vector<float>(1000, 0.1F));
    gate.feedSamples(std::vector<float>(2000, 0.1F));

    QCOMPARE(gate.consumedSamples(), std::size_t{3000});
}

QTEST_GUILESS_MAIN(TestVadGate)

#include "tst_vadgate.moc"
