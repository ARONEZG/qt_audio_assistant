// Tests for the VAD framing/gating layer (VadSpeechGate).
//
// The gate cuts canonical PCM (16 kHz, mono, f32) into 512-sample frames,
// feeds them through the injected VoiceActivityDetector, and builds per-sample
// keep-masks / trimmed output for the captured stream. The detector is a
// deterministic double (FakeVoiceActivityDetector), so no ONNX model is loaded:
// the framing, partial-frame carry, and mask mapping are the contract under
// test.
#include <QtTest>
#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

#include "FakeVoiceActivityDetector.h"
#include "audio/SpeechSegmenter.h"
#include "audio/VadSpeechGate.h"
#include "audio/VoiceActivityDetector.h"

namespace {

constexpr std::size_t kFrameSize = 512;

audio::SpeechSegmenter::Parameters defaultParameters() {
    // Low thresholds so a constant-probability fake drives clean intervals.
    return {0.5F, 0.15F, 2, 2};
}

// Every sample of frame N carries the value N, so a returned block can be
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

// A probability stream of `frameCount` silence frames with the given speech
// runs marked at 0.9.
std::vector<float> sequenceWithSpeech(std::size_t frameCount, const std::vector<std::size_t>& speechFrames) {
    std::vector<float> sequence(frameCount, 0.05F);
    for (const std::size_t kFrame : speechFrames) {
        sequence[kFrame] = 0.9F;
    }
    return sequence;
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
    void test_lastSpeechBlockSelectsTheLastInterval();
    void test_lastSpeechBlockClipsPreRollAtBlockStart();
    void test_lastSpeechBlockClipsPostRollAtBlockEnd();
    void test_lastSpeechBlockWithNoSpeechReturnsEmpty();
    void test_lastSpeechBlockWithEmptyInputReturnsEmpty();
    void test_lastSpeechBlockPreRollDoesNotReachThePreviousIntervalAtProductionParams();
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

    const std::vector<float> kSamples(8 * kFrameSize, 0.1F);
    const std::vector<float> kTrimmed = gate.trimSilence(kSamples);

    QVERIFY(kTrimmed.empty());
}

// The whole point of lastSpeechBlock: only the final utterance is sent, not the
// concatenation of every speech interval in the capture window.
void TestVadGate::test_lastSpeechBlockSelectsTheLastInterval() {
    FakeVoiceActivityDetector detector;
    audio::VadSpeechGate gate(detector, defaultParameters());
    // Two separated utterances: frames 0..1 and frames 12..14. A pause of ten
    // silent frames is longer than minSilenceFrames, so they are distinct.
    constexpr std::size_t kFrameCount = 20;
    detector.setSequence(sequenceWithSpeech(kFrameCount, {0, 1, 12, 13, 14}));

    const std::vector<float> kSamples = frameValues(kFrameCount);
    const std::vector<float> kBlock = gate.lastSpeechBlock(kSamples);

    // The last interval starts at sample 12*512 = 6144; the 300 ms pre-roll
    // reaches back to 1344 (inside frame 2), past the first utterance. The
    // 200 ms post-roll would reach 10880, but is clipped at the block end
    // (20 frames = 10240 samples).
    constexpr std::size_t kExpectedStart = 12 * kFrameSize - 4800;
    constexpr std::size_t kExpectedEnd = kFrameCount * kFrameSize;
    QCOMPARE(kBlock.size(), kExpectedEnd - kExpectedStart);
    QCOMPARE(kBlock.front(), kSamples[kExpectedStart]);
    QCOMPARE(kBlock.back(), kSamples[kExpectedEnd - 1]);
    // The first utterance (frames 0..1) is not part of the selected block.
    QCOMPARE(kBlock.front(), 2.0F);
}

void TestVadGate::test_lastSpeechBlockClipsPreRollAtBlockStart() {
    FakeVoiceActivityDetector detector;
    audio::VadSpeechGate gate(detector, defaultParameters());
    // The only utterance starts at sample 0, so the 300 ms pre-roll cannot be
    // honoured in full and must be clipped to the block start.
    constexpr std::size_t kFrameCount = 20;
    detector.setSequence(sequenceWithSpeech(kFrameCount, {0, 1}));

    const std::vector<float> kSamples = frameValues(kFrameCount);
    const std::vector<float> kBlock = gate.lastSpeechBlock(kSamples);

    constexpr std::size_t kExpectedEnd = 2 * kFrameSize + 3200;
    QCOMPARE(kBlock.size(), kExpectedEnd);
    QCOMPARE(kBlock.front(), kSamples.front());
    QCOMPARE(kBlock.back(), kSamples[kExpectedEnd - 1]);
}

void TestVadGate::test_lastSpeechBlockClipsPostRollAtBlockEnd() {
    FakeVoiceActivityDetector detector;
    audio::VadSpeechGate gate(detector, defaultParameters());
    // The only utterance ends at the last frame, so the 200 ms post-roll is
    // clipped to the block end.
    constexpr std::size_t kFrameCount = 20;
    detector.setSequence(sequenceWithSpeech(kFrameCount, {18, 19}));

    const std::vector<float> kSamples = frameValues(kFrameCount);
    const std::vector<float> kBlock = gate.lastSpeechBlock(kSamples);

    constexpr std::size_t kExpectedStart = 18 * kFrameSize - 4800;
    QCOMPARE(kBlock.size(), kSamples.size() - kExpectedStart);
    QCOMPARE(kBlock.front(), kSamples[kExpectedStart]);
    QCOMPARE(kBlock.back(), kSamples.back());
}

void TestVadGate::test_lastSpeechBlockWithNoSpeechReturnsEmpty() {
    FakeVoiceActivityDetector detector;
    detector.setConstant(0.05F);
    audio::VadSpeechGate gate(detector, defaultParameters());

    const std::vector<float> kSamples(8 * kFrameSize, 0.1F);
    const std::vector<float> kBlock = gate.lastSpeechBlock(kSamples);

    QVERIFY(kBlock.empty());
}

void TestVadGate::test_lastSpeechBlockWithEmptyInputReturnsEmpty() {
    FakeVoiceActivityDetector detector;
    detector.setConstant(0.9F);
    audio::VadSpeechGate gate(detector, defaultParameters());
    const std::vector<float> kEmpty;

    const std::vector<float> kBlock = gate.lastSpeechBlock(kEmpty);

    QVERIFY(kBlock.empty());
    QCOMPARE(detector.processCount(), 0);
}

// Pins the production coupling: with minSilenceFrames = 16 (512 ms) the 300 ms
// pre-roll is shorter than the gap, so selecting the last interval cannot reach
// back into the previous utterance. The boundary frame values make an overlap
// visible even if the size assertion changes.
void TestVadGate::test_lastSpeechBlockPreRollDoesNotReachThePreviousIntervalAtProductionParams() {
    FakeVoiceActivityDetector detector;
    audio::VadSpeechGate gate(detector, {0.5F, 0.15F, 8, 16});
    constexpr std::size_t kFrameCount = 45;

    std::vector<std::size_t> speechFrames;
    for (std::size_t frame = 0; frame < 8; ++frame) {
        speechFrames.push_back(frame);
    }
    for (std::size_t frame = 24; frame < 32; ++frame) {
        speechFrames.push_back(frame);
    }
    detector.setSequence(sequenceWithSpeech(kFrameCount, speechFrames));

    const std::vector<float> kSamples = frameValues(kFrameCount);
    const std::vector<float> kBlock = gate.lastSpeechBlock(kSamples);

    QCOMPARE(gate.intervals().size(), std::size_t{2});
    QCOMPARE(gate.intervals().back().start(), std::size_t{24});
    QCOMPARE(gate.lastSpeechSamples(), 8 * kFrameSize);
    // Last interval starts at 24*512 = 12288; the pre-roll reaches 7488, which
    // is inside the 16-frame silence (frames 8..23), not the first utterance.
    constexpr std::size_t kExpectedStart = 24 * kFrameSize - 4800;
    constexpr std::size_t kExpectedEnd = 32 * kFrameSize + 3200;
    QCOMPARE(kBlock.size(), kExpectedEnd - kExpectedStart);
    QCOMPARE(kBlock.front(), 14.0F);
    QCOMPARE(kBlock.back(), kSamples[kExpectedEnd - 1]);
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
