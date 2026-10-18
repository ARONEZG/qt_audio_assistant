// Tests for the STL-only WAV encoder.
//
// They are pure and synchronous: no device, no network, no clock. The canonical
// 16 kHz / mono format is checked byte-for-byte (every RIFF field), plus a
// non-canonical format to prove the header is derived from AudioFormat rather
// than hardcoded. The f32 -> int16 mapping is pinned at both rails, at zero, at
// half scale, and beyond the rails (clamping).
#include <QtTest>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "audio/AudioFormat.h"
#include "audio/WavEncoder.h"

namespace {

// Returns the canonical capture format. A function, not a namespace-scope
// constant: AudioFormat is not constexpr, so a global value would require a
// global constructor, which the strict profile rejects.
audio::AudioFormat canonicalFormat() {
    return {16000U, 1U, audio::SampleFormat::kFloat32};
}

// Reads a little-endian unsigned integer from the byte vector. `.at()` is used
// instead of operator[] so the strict -Wunsafe-buffer-usage profile stays clean.
std::uint16_t readLe16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes.at(offset)) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes.at(offset + 1U)) << 8U));
}

std::uint32_t readLe32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes.at(offset)) | (static_cast<std::uint32_t>(bytes.at(offset + 1U)) << 8U) |
           (static_cast<std::uint32_t>(bytes.at(offset + 2U)) << 16U) |
           (static_cast<std::uint32_t>(bytes.at(offset + 3U)) << 24U);
}

// Reads a four-character chunk id as a std::string for readable comparisons.
std::string tagAt(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::string tag;
    tag.reserve(4U);
    for (std::size_t i = 0; i < 4U; ++i) {
        tag.push_back(static_cast<char>(bytes.at(offset + i)));
    }
    return tag;
}

// The int16 sample stored at data-relative index (two bytes per sample).
std::int16_t sampleAt(const std::vector<std::uint8_t>& bytes, std::size_t index) {
    return static_cast<std::int16_t>(readLe16(bytes, 44U + index * 2U));
}

}  // namespace

class TestWavEncoder : public QObject {
    Q_OBJECT

public:
    explicit TestWavEncoder(QObject* parent = nullptr);

private slots:
    void test_encodesRiffHeader();
    void test_headerIsDerivedFromFormat();
    void test_emptyInputHasValidHeaderWithZeroData();
    void test_mapsFullScaleSamples();
    void test_mapsZeroAndHalfScale();
    void test_clampsBeyondFullScale();
    void test_nonFiniteSamplesBecomeSilence();
    void test_writesLittleEndianSamples();
};

TestWavEncoder::TestWavEncoder(QObject* parent) : QObject(parent) {
}

void TestWavEncoder::test_encodesRiffHeader() {
    const std::vector<float> kSamples{0.0F, 0.0F};
    const std::vector<std::uint8_t> kWav = audio::encodeWavPcm16(canonicalFormat(), kSamples);

    QCOMPARE(kWav.size(), std::size_t{44U + 4U});
    QCOMPARE(tagAt(kWav, 0U), std::string("RIFF"));
    // chunkSize covers everything after the first 8 bytes: 36 + data.
    QCOMPARE(readLe32(kWav, 4U), 36U + 4U);
    QCOMPARE(tagAt(kWav, 8U), std::string("WAVE"));
    QCOMPARE(tagAt(kWav, 12U), std::string("fmt "));
    QCOMPARE(readLe32(kWav, 16U), 16U);
    QCOMPARE(readLe16(kWav, 20U), 1U);  // PCM
    QCOMPARE(readLe16(kWav, 22U), 1U);  // mono
    QCOMPARE(readLe32(kWav, 24U), 16000U);
    QCOMPARE(readLe32(kWav, 28U), 32000U);  // byteRate = 16000 * 1 channel * 2 bytes
    QCOMPARE(readLe16(kWav, 32U), 2U);      // blockAlign
    QCOMPARE(readLe16(kWav, 34U), 16U);     // bitsPerSample
    QCOMPARE(tagAt(kWav, 36U), std::string("data"));
    QCOMPARE(readLe32(kWav, 40U), 4U);
}

void TestWavEncoder::test_headerIsDerivedFromFormat() {
    const audio::AudioFormat kStereo(48000U, 2U, audio::SampleFormat::kFloat32);
    const std::vector<std::uint8_t> kWav = audio::encodeWavPcm16(kStereo, std::vector<float>{});

    QCOMPARE(readLe16(kWav, 22U), 2U);       // channels
    QCOMPARE(readLe32(kWav, 24U), 48000U);   // sampleRate
    QCOMPARE(readLe16(kWav, 32U), 4U);       // blockAlign = 2 channels * 2 bytes
    QCOMPARE(readLe32(kWav, 28U), 192000U);  // byteRate = 48000 * 4
}

void TestWavEncoder::test_emptyInputHasValidHeaderWithZeroData() {
    const std::vector<std::uint8_t> kWav = audio::encodeWavPcm16(canonicalFormat(), std::vector<float>{});

    QCOMPARE(kWav.size(), std::size_t{44U});
    QCOMPARE(readLe32(kWav, 4U), 36U);
    QCOMPARE(readLe32(kWav, 40U), 0U);
}

void TestWavEncoder::test_mapsFullScaleSamples() {
    const std::vector<float> kSamples{1.0F, -1.0F};
    const std::vector<std::uint8_t> kWav = audio::encodeWavPcm16(canonicalFormat(), kSamples);

    QCOMPARE(sampleAt(kWav, 0U), static_cast<std::int16_t>(32767));
    QCOMPARE(sampleAt(kWav, 1U), static_cast<std::int16_t>(-32768));
}

void TestWavEncoder::test_mapsZeroAndHalfScale() {
    const std::vector<float> kSamples{0.0F, 0.5F, -0.5F};
    const std::vector<std::uint8_t> kWav = audio::encodeWavPcm16(canonicalFormat(), kSamples);

    QCOMPARE(sampleAt(kWav, 0U), static_cast<std::int16_t>(0));
    QCOMPARE(sampleAt(kWav, 1U), static_cast<std::int16_t>(16384));
    QCOMPARE(sampleAt(kWav, 2U), static_cast<std::int16_t>(-16384));
}

void TestWavEncoder::test_clampsBeyondFullScale() {
    const std::vector<float> kSamples{2.0F, -2.0F};
    const std::vector<std::uint8_t> kWav = audio::encodeWavPcm16(canonicalFormat(), kSamples);

    QCOMPARE(sampleAt(kWav, 0U), static_cast<std::int16_t>(32767));
    QCOMPARE(sampleAt(kWav, 1U), static_cast<std::int16_t>(-32768));
}

void TestWavEncoder::test_nonFiniteSamplesBecomeSilence() {
    // NaN/inf are corrupt input: they must not become a full-scale sample
    // (std::clamp(NaN) stays NaN and lround(NaN) is implementation-defined).
    const std::vector<float> kSamples{std::numeric_limits<float>::quiet_NaN(),
                                      std::numeric_limits<float>::infinity(),
                                      -std::numeric_limits<float>::infinity()};
    const std::vector<std::uint8_t> kWav = audio::encodeWavPcm16(canonicalFormat(), kSamples);

    QCOMPARE(sampleAt(kWav, 0U), static_cast<std::int16_t>(0));
    QCOMPARE(sampleAt(kWav, 1U), static_cast<std::int16_t>(0));
    QCOMPARE(sampleAt(kWav, 2U), static_cast<std::int16_t>(0));
}

void TestWavEncoder::test_writesLittleEndianSamples() {
    // +1.0 -> 0x7FFF -> bytes FF 7F (little-endian), the low byte first.
    const std::vector<std::uint8_t> kWav = audio::encodeWavPcm16(canonicalFormat(), std::vector<float>{1.0F});

    QCOMPARE(kWav.at(44U), static_cast<std::uint8_t>(0xFFU));
    QCOMPARE(kWav.at(45U), static_cast<std::uint8_t>(0x7FU));
}

QTEST_GUILESS_MAIN(TestWavEncoder)

#include "tst_wavencoder.moc"
