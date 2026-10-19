// Tests for the VAD post-processing layer (SpeechSegmenter).
//
// The segmenter is pure C++/STL: it classifies a stream of per-frame speech
// probabilities into speech intervals with threshold + hysteresis +
// min-duration. No detector, no ONNX, no Qt beyond the test framework — the
// probabilities are the input directly, so every branch of the state machine
// is driven deterministically.
#include <QtTest>
#include <stdexcept>
#include <vector>

#include "audio/SpeechSegmenter.h"

namespace {

using audio::SpeechSegmenter;

// Default parameters, matching the real VAD post-processing defaults.
SpeechSegmenter::Parameters defaultParameters() {
    return {0.5F, 0.15F, 8, 4};
}

std::vector<float> frames(std::size_t count, float probability) {
    return std::vector<float>(count, probability);
}

}  // namespace

class TestSpeechSegmenter : public QObject {
    Q_OBJECT

public:
    explicit TestSpeechSegmenter(QObject* parent = nullptr);

private slots:
    void test_emptyInputProducesNoIntervals();
    void test_allSilenceProducesNoIntervals();
    void test_singleSpeechBurstProducesOneInterval();
    void test_burstShorterThanMinSpeechIsRejected();
    void test_gapShorterThanMinSilenceIsBridged();
    void test_gapLongerThanMinSilenceSplitsTheUtterance();
    void test_hysteresisKeepsDipsInsideTheRun();
    void test_hysteresisEndsTheRunAfterSustainedDip();
    void test_trailingSpeechIsFlushed();
    void test_leadingAndTrailingSilenceIsTrimmed();
    void test_parametersAreValidated();
    void test_intervalsAreOrderedAndMerged();
};

TestSpeechSegmenter::TestSpeechSegmenter(QObject* parent) : QObject(parent) {
}

void TestSpeechSegmenter::test_emptyInputProducesNoIntervals() {
    const SpeechSegmenter kSegmenter(defaultParameters());
    QVERIFY(kSegmenter.segment({}).empty());
}

void TestSpeechSegmenter::test_allSilenceProducesNoIntervals() {
    const SpeechSegmenter kSegmenter(defaultParameters());
    const auto kResult = kSegmenter.segment(frames(100, 0.05F));
    QVERIFY(kResult.empty());
}

void TestSpeechSegmenter::test_singleSpeechBurstProducesOneInterval() {
    const SpeechSegmenter kSegmenter(defaultParameters());
    // 10 frames of speech, preceded and followed by silence.
    std::vector<float> probabilities;
    probabilities.insert(probabilities.end(), 5, 0.05F);
    probabilities.insert(probabilities.end(), 10, 0.9F);
    probabilities.insert(probabilities.end(), 5, 0.05F);

    const auto kResult = kSegmenter.segment(probabilities);

    QCOMPARE(kResult.size(), std::size_t{1});
    QCOMPARE(kResult[0].start(), std::size_t{5});
    QCOMPARE(kResult[0].end(), std::size_t{15});
    QCOMPARE(kResult[0].frameCount(), std::size_t{10});
}

void TestSpeechSegmenter::test_burstShorterThanMinSpeechIsRejected() {
    const SpeechSegmenter kSegmenter(defaultParameters());
    // 3 frames of speech (< minSpeechFrames=8).
    std::vector<float> probabilities;
    probabilities.insert(probabilities.end(), 3, 0.05F);
    probabilities.insert(probabilities.end(), 3, 0.9F);
    probabilities.insert(probabilities.end(), 3, 0.05F);

    QVERIFY(kSegmenter.segment(probabilities).empty());
}

void TestSpeechSegmenter::test_gapShorterThanMinSilenceIsBridged() {
    const SpeechSegmenter kSegmenter(defaultParameters());
    // Speech, then a gap of 2 frames (< minSilenceFrames=4), then speech again.
    std::vector<float> probabilities;
    probabilities.insert(probabilities.end(), 10, 0.9F);
    probabilities.insert(probabilities.end(), 2, 0.05F);
    probabilities.insert(probabilities.end(), 10, 0.9F);

    const auto kResult = kSegmenter.segment(probabilities);

    // One merged interval covering both bursts and the gap.
    QCOMPARE(kResult.size(), std::size_t{1});
    QCOMPARE(kResult[0].start(), std::size_t{0});
    QCOMPARE(kResult[0].end(), std::size_t{22});
}

void TestSpeechSegmenter::test_gapLongerThanMinSilenceSplitsTheUtterance() {
    const SpeechSegmenter kSegmenter(defaultParameters());
    // Speech, then a gap of 6 frames (>= minSilenceFrames=4), then speech.
    std::vector<float> probabilities;
    probabilities.insert(probabilities.end(), 10, 0.9F);
    probabilities.insert(probabilities.end(), 6, 0.05F);
    probabilities.insert(probabilities.end(), 10, 0.9F);

    const auto kResult = kSegmenter.segment(probabilities);

    QCOMPARE(kResult.size(), std::size_t{2});
    QCOMPARE(kResult[0].start(), std::size_t{0});
    QCOMPARE(kResult[0].end(), std::size_t{10});
    QCOMPARE(kResult[1].start(), std::size_t{16});
    QCOMPARE(kResult[1].end(), std::size_t{26});
}

void TestSpeechSegmenter::test_hysteresisKeepsDipsInsideTheRun() {
    const SpeechSegmenter kSegmenter(defaultParameters());
    // A single frame dipping below the threshold but above threshold-hysteresis
    // (0.35) must not split the utterance.
    std::vector<float> probabilities;
    probabilities.insert(probabilities.end(), 10, 0.9F);
    probabilities.push_back(0.4F);  // in the hysteresis band
    probabilities.insert(probabilities.end(), 10, 0.9F);

    const auto kResult = kSegmenter.segment(probabilities);

    QCOMPARE(kResult.size(), std::size_t{1});
    QCOMPARE(kResult[0].start(), std::size_t{0});
    QCOMPARE(kResult[0].end(), std::size_t{21});
}

void TestSpeechSegmenter::test_hysteresisEndsTheRunAfterSustainedDip() {
    const SpeechSegmenter kSegmenter(defaultParameters());
    // A long dip below the threshold but above 0.35 — the gap counter only
    // starts once the signal leaves the hysteresis band, so a 4-frame dip at
    // 0.4 must NOT close the run (hysteresis absorbs it).
    std::vector<float> probabilities;
    probabilities.insert(probabilities.end(), 10, 0.9F);
    probabilities.insert(probabilities.end(), 4, 0.4F);
    probabilities.insert(probabilities.end(), 10, 0.9F);

    const auto kResult = kSegmenter.segment(probabilities);

    QCOMPARE(kResult.size(), std::size_t{1});
    QCOMPARE(kResult[0].frameCount(), std::size_t{24});
}

void TestSpeechSegmenter::test_trailingSpeechIsFlushed() {
    const SpeechSegmenter kSegmenter(defaultParameters());
    // Speech continues to the end of the stream: the run must still be emitted.
    std::vector<float> probabilities;
    probabilities.insert(probabilities.end(), 5, 0.05F);
    probabilities.insert(probabilities.end(), 12, 0.9F);

    const auto kResult = kSegmenter.segment(probabilities);

    QCOMPARE(kResult.size(), std::size_t{1});
    QCOMPARE(kResult[0].start(), std::size_t{5});
    QCOMPARE(kResult[0].end(), std::size_t{17});
}

void TestSpeechSegmenter::test_leadingAndTrailingSilenceIsTrimmed() {
    const SpeechSegmenter kSegmenter(defaultParameters());
    std::vector<float> probabilities;
    probabilities.insert(probabilities.end(), 20, 0.02F);
    probabilities.insert(probabilities.end(), 10, 0.95F);
    probabilities.insert(probabilities.end(), 20, 0.02F);

    const auto kResult = kSegmenter.segment(probabilities);

    QCOMPARE(kResult.size(), std::size_t{1});
    QCOMPARE(kResult[0].start(), std::size_t{20});
    QCOMPARE(kResult[0].end(), std::size_t{30});
}

void TestSpeechSegmenter::test_parametersAreValidated() {
    // Threshold must be in (0, 1).
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, SpeechSegmenter({0.0F, 0.15F, 8, 4}));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, SpeechSegmenter({1.0F, 0.15F, 8, 4}));
    // Hysteresis must be in (0, threshold).
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, SpeechSegmenter({0.5F, 0.0F, 8, 4}));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, SpeechSegmenter({0.5F, 0.5F, 8, 4}));
    // minSpeechFrames must be > 0.
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, SpeechSegmenter({0.5F, 0.15F, 0, 4}));
}

void TestSpeechSegmenter::test_intervalsAreOrderedAndMerged() {
    const SpeechSegmenter kSegmenter(defaultParameters());
    // Three bursts separated by gaps longer than minSilence: three ordered,
    // non-overlapping intervals.
    std::vector<float> probabilities;
    for (int burst = 0; burst < 3; ++burst) {
        probabilities.insert(probabilities.end(), 10, 0.95F);
        probabilities.insert(probabilities.end(), 6, 0.05F);
    }

    const auto kResult = kSegmenter.segment(probabilities);

    QCOMPARE(kResult.size(), std::size_t{3});
    for (std::size_t i = 1; i < kResult.size(); ++i) {
        QVERIFY(kResult[i - 1].end() <= kResult[i].start());
    }
}

QTEST_GUILESS_MAIN(TestSpeechSegmenter)

#include "tst_speechsegmenter.moc"
