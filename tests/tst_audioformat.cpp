#include <QtTest>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "audio/AudioFormat.h"

namespace {

// Forces a real by-value copy at the call boundary; this is the property the
// copy test is about, and it keeps clang-tidy from reading the copy as dead.
audio::AudioFormat copyOf(audio::AudioFormat format) {
    return format;
}

// True when building a format from these raw values is rejected. This is a
// hand-written try/catch on purpose: QVERIFY_THROWS_EXCEPTION expands to four
// levels of nesting, which trips readability-function-size's nesting threshold
// in an otherwise trivial test function.
bool rejectsFormat(std::uint32_t sampleRate, std::uint32_t channels) {
    try {
        static_cast<void>(audio::AudioFormat(sampleRate, channels, audio::SampleFormat::kFloat32));
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

}  // namespace

class TestAudioFormat : public QObject {
    Q_OBJECT

public:
    explicit TestAudioFormat(QObject* parent = nullptr);

private slots:
    void test_constructsWithAllFields();
    void test_exposesSampleRate();
    void test_exposesChannels();
    void test_exposesSampleFormat();
    void test_invariantsHoldForValidFormats();
    void test_equalFormatsCompareEqual();
    void test_differentSampleRateComparesUnequal();
    void test_differentChannelCountComparesUnequal();
    void test_copyEqualsOriginal();
    void test_minimalRateAndChannelsAreValid();
    void test_largestRateAndChannelsAreValid();
    void test_zeroSampleRateIsRejected();
    void test_zeroChannelsAreRejected();
};

TestAudioFormat::TestAudioFormat(QObject* parent) : QObject(parent) {
}

void TestAudioFormat::test_constructsWithAllFields() {
    // Canonical capture format: 16 kHz, mono, f32.
    const audio::AudioFormat kFormat(16000U, 1U, audio::SampleFormat::kFloat32);

    QCOMPARE(kFormat.sampleRate(), 16000U);
    QCOMPARE(kFormat.channels(), 1U);
    QCOMPARE(kFormat.sampleFormat(), audio::SampleFormat::kFloat32);
}

void TestAudioFormat::test_exposesSampleRate() {
    const audio::AudioFormat kFormat(48000U, 2U, audio::SampleFormat::kFloat32);

    QCOMPARE(kFormat.sampleRate(), 48000U);
}

void TestAudioFormat::test_exposesChannels() {
    const audio::AudioFormat kFormat(44100U, 2U, audio::SampleFormat::kFloat32);

    QCOMPARE(kFormat.channels(), 2U);
}

void TestAudioFormat::test_exposesSampleFormat() {
    const audio::AudioFormat kFormat(16000U, 1U, audio::SampleFormat::kFloat32);

    QCOMPARE(kFormat.sampleFormat(), audio::SampleFormat::kFloat32);
}

void TestAudioFormat::test_invariantsHoldForValidFormats() {
    const audio::AudioFormat kFormat(16000U, 1U, audio::SampleFormat::kFloat32);

    QVERIFY(kFormat.sampleRate() > 0U);
    QVERIFY(kFormat.channels() > 0U);
}

void TestAudioFormat::test_equalFormatsCompareEqual() {
    const audio::AudioFormat kFirst(16000U, 1U, audio::SampleFormat::kFloat32);
    const audio::AudioFormat kSecond(16000U, 1U, audio::SampleFormat::kFloat32);

    QVERIFY(kFirst == kSecond);
    QVERIFY(!(kFirst != kSecond));
}

void TestAudioFormat::test_differentSampleRateComparesUnequal() {
    const audio::AudioFormat kFirst(16000U, 1U, audio::SampleFormat::kFloat32);
    const audio::AudioFormat kSecond(48000U, 1U, audio::SampleFormat::kFloat32);

    QVERIFY(!(kFirst == kSecond));
    QVERIFY(kFirst != kSecond);
}

void TestAudioFormat::test_differentChannelCountComparesUnequal() {
    const audio::AudioFormat kFirst(16000U, 1U, audio::SampleFormat::kFloat32);
    const audio::AudioFormat kSecond(16000U, 2U, audio::SampleFormat::kFloat32);

    QVERIFY(!(kFirst == kSecond));
    QVERIFY(kFirst != kSecond);
}

void TestAudioFormat::test_copyEqualsOriginal() {
    const audio::AudioFormat kOriginal(16000U, 1U, audio::SampleFormat::kFloat32);
    const audio::AudioFormat kCopy = copyOf(kOriginal);

    QCOMPARE(kCopy, kOriginal);
    QCOMPARE(kCopy.sampleRate(), kOriginal.sampleRate());
    QCOMPARE(kCopy.channels(), kOriginal.channels());
    QCOMPARE(kCopy.sampleFormat(), kOriginal.sampleFormat());
}

void TestAudioFormat::test_minimalRateAndChannelsAreValid() {
    const audio::AudioFormat kFormat(1U, 1U, audio::SampleFormat::kFloat32);

    QCOMPARE(kFormat.sampleRate(), 1U);
    QCOMPARE(kFormat.channels(), 1U);
}

void TestAudioFormat::test_largestRateAndChannelsAreValid() {
    const std::uint32_t kMax = std::numeric_limits<std::uint32_t>::max();
    const audio::AudioFormat kFormat(kMax, kMax, audio::SampleFormat::kFloat32);

    QCOMPARE(kFormat.sampleRate(), kMax);
    QCOMPARE(kFormat.channels(), kMax);
}

void TestAudioFormat::test_zeroSampleRateIsRejected() {
    QVERIFY(rejectsFormat(0U, 1U));
}

void TestAudioFormat::test_zeroChannelsAreRejected() {
    QVERIFY(rejectsFormat(16000U, 0U));
}

QTEST_GUILESS_MAIN(TestAudioFormat)

#include "tst_audioformat.moc"
