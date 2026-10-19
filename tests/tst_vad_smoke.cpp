// Integration smoke test for the Silero VAD model wiring.
//
// What it proves and why it is not VAD logic:
//  * The embedded model asset is reachable through the Qt resource system: the
//    resource library (audio-assistant-models) compiles models/silero_vad/
//    silero_vad.onnx with PREFIX/BASE, and the adapter reads
//    ":/models/silero_vad/silero_vad.onnx" through QFile. A successful
//    construction therefore proves the qrc wiring (rcc-compiled object,
//    resource keeper linked into this binary), not just that some model file
//    exists on disk.
//  * onnxruntime really runs: the model is executed on the CPU provider and
//    its per-frame speech probability is read back.
//  * The deterministic synthetic stimulus ("speech-like" tone burst) produces a
//    speech probability above a quiet floor, and a silent stimulus produces a
//    low one — the same qualitative behaviour the real capture uses, without a
//    microphone or any non-deterministic input.
//
// The stimulus is generated (no WAV file, no device, no network): a 440 Hz
// tone burst at 16 kHz is speech-like for a VAD, so the detector should
// report a high probability on the burst frames and a low one on the silence
// frames around it.
#include <QtTest>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

#include "audio/SileroVoiceActivityDetector.h"
#include "audio/VoiceActivityDetector.h"

namespace {

// The canonical capture stream (16 kHz, mono, f32).
constexpr std::size_t kSampleRate = 16000;
constexpr std::size_t kFrameSize = 512;
constexpr std::size_t kNumberOfFrames = 16;

// A speech-*like* stimulus. Silero VAD is trained to reject tonal noise: a pure
// sine (or DTMF/beep) is classified as non-speech, so the synthetic burst must
// carry the two cues real speech has — a harmonic stack with formant-like
// spectral shape and a syllabic amplitude envelope. This is deterministic: no
// RNG, no device, no file.
std::vector<float> makeSpeechLikeBurst() {
    std::vector<float> samples;
    const std::size_t kTotal = kNumberOfFrames * kFrameSize;
    samples.reserve(kTotal);
    constexpr double kFundamental = 140.0;  // Hz — bottom of the male voice range
    for (std::size_t i = 0; i < kTotal; ++i) {
        const double kT = static_cast<double>(i) / static_cast<double>(kSampleRate);
        // Syllabic envelope: ~4 Hz on/off modulation (speech syllable rate).
        const double kSyllable = 0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * 4.0 * kT);
        // Formant-like harmonic stack: F0 + a few voiced harmonics.
        double value = 0.0;
        for (int harmonic = 1; harmonic <= 4; ++harmonic) {
            const double kAmp = 1.0 / static_cast<double>(harmonic);
            value += kAmp * std::sin(2.0 * std::numbers::pi * kFundamental * static_cast<double>(harmonic) * kT);
        }
        // Gentle fricative-style high-frequency content so stops/plosives exist.
        value += 0.25 * std::sin(2.0 * std::numbers::pi * 2500.0 * kT);
        samples.push_back(0.45F * static_cast<float>(kSyllable * value) /
                          static_cast<float>(1.0 + 0.25 + 0.125 + 0.0625 + 0.25));
    }
    return samples;
}

std::vector<float> makeSilence() {
    return std::vector<float>(kNumberOfFrames * kFrameSize, 0.0F);
}

// Feeds the detector frame-by-frame and returns the per-frame probabilities.
std::vector<float> runDetector(audio::VoiceActivityDetector& detector, const std::vector<float>& samples) {
    std::vector<float> probabilities;
    probabilities.reserve(samples.size() / kFrameSize);
    const std::span<const float> kSamples(samples);
    for (std::size_t i = 0; i + kFrameSize <= kSamples.size(); i += kFrameSize) {
        probabilities.push_back(detector.process(kSamples.subspan(i, kFrameSize)));
    }
    return probabilities;
}

}  // namespace

class TestVadSmoke : public QObject {
    Q_OBJECT

public:
    explicit TestVadSmoke(QObject* parent = nullptr);

private slots:
    void test_modelLoadsFromResource();
    void test_toneBurstRaisesSpeechProbability();
    void test_silenceStaysBelowSpeechThreshold();
    void test_missingModelResourceThrows();
};

TestVadSmoke::TestVadSmoke(QObject* parent) : QObject(parent) {
}

void TestVadSmoke::test_modelLoadsFromResource() {
    // The default constructor reads the model from the Qt resource; if the
    // qrc is not compiled/linked, this throws (or the session fails).
    const audio::SileroVoiceActivityDetector kDetector;
    QVERIFY(true);  // reaching here means the model was loaded
}

void TestVadSmoke::test_toneBurstRaisesSpeechProbability() {
    audio::SileroVoiceActivityDetector detector;
    const std::vector<float> kBurst = makeSpeechLikeBurst();
    const std::vector<float> kProbabilities = runDetector(detector, kBurst);

    QCOMPARE(kProbabilities.size(), kNumberOfFrames);
    // The synthetic speech-like burst must be classified as speech by the real
    // model: a clear majority of frames above the default 0.5 threshold.
    std::size_t above = 0;
    for (const float kProbability : kProbabilities) {
        if (kProbability >= 0.5F) {
            ++above;
        }
    }
    QVERIFY2(above >= 10,
             qPrintable(QStringLiteral("expected most burst frames above 0.5, got %1/%2")
                                .arg(above)
                                .arg(kProbabilities.size())));
}

void TestVadSmoke::test_silenceStaysBelowSpeechThreshold() {
    audio::SileroVoiceActivityDetector detector;
    const std::vector<float> kSilence = makeSilence();
    const std::vector<float> kProbabilities = runDetector(detector, kSilence);

    QCOMPARE(kProbabilities.size(), kNumberOfFrames);
    for (const float kProbability : kProbabilities) {
        QVERIFY2(kProbability < 0.5F,
                 qPrintable(
                         QStringLiteral("expected silence below 0.5, got %1").arg(static_cast<double>(kProbability))));
    }
}

void TestVadSmoke::test_missingModelResourceThrows() {
    // A non-existent resource path is rejected loudly (no silent empty model).
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, audio::SileroVoiceActivityDetector(":/models/nonexistent.onnx"));
}

QTEST_GUILESS_MAIN(TestVadSmoke)

#include "tst_vad_smoke.moc"
