#include <QtTest>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

#include "audio/AudioBuffer.h"
#include "audio/AudioFormat.h"

namespace {

// The canonical capture format (16 kHz, mono, f32) at a chosen sample rate;
// most buffer tests only vary the frame capacity, so this keeps them terse.
audio::AudioFormat monoFormat(std::uint32_t sampleRate) {
    return {sampleRate, 1U, audio::SampleFormat::kFloat32};
}

// True when building a buffer from these arguments throws std::invalid_argument.
// A hand-written try/catch (instead of QVERIFY_THROWS_EXCEPTION) keeps the small
// test functions below the readability-function-size nesting threshold, exactly
// like tst_audioformat.cpp does for AudioFormat.
template <typename Action>
bool rejectsConstruction(const Action& action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

// Compares a snapshot to the expected frames. Comparing sizes first reports a
// length mismatch on its own line; the element comparison stays loop-free so
// the QCOMPARE/QVERIFY macros do not push the helper over the nesting threshold.
void verifyFrames(const std::vector<float>& actual, const std::vector<float>& expected) {
    QCOMPARE(actual.size(), expected.size());
    QVERIFY(actual == expected);
}

// Writes chunkCount chunks of chunkFrames strictly increasing frames into the
// buffer; used as the producer body of the concurrency test.
void writeRamp(audio::AudioBuffer& buffer, std::size_t chunkFrames, std::size_t chunkCount) {
    float next = 0.0F;
    for (std::size_t chunk = 0; chunk < chunkCount; ++chunk) {
        std::vector<float> frames(chunkFrames, 0.0F);
        for (float& sample : frames) {
            next += 1.0F;
            sample = next;
        }
        buffer.write(frames);
    }
}

// True when every frame is exactly one more than its predecessor, i.e. the
// snapshot is a single uninterrupted run of the producer's ramp. The difference
// is checked with a tolerance because -Wfloat-equal rejects a raw ==/!=.
bool isConsecutiveRamp(const std::vector<float>& frames) {
    for (std::size_t i = 1; i < frames.size(); ++i) {
        if (std::abs(frames[i] - (frames[i - 1] + 1.0F)) > 0.5F) {
            return false;
        }
    }
    return true;
}

}  // namespace

class TestAudioBuffer : public QObject {
    Q_OBJECT

public:
    explicit TestAudioBuffer(QObject* parent = nullptr);

private slots:
    void test_explicitCapacityIsReported();
    void test_newBufferHasEmptySnapshot();
    void test_writeShorterThanCapacityKeepsOrder();
    void test_writeExactlyCapacityKeepsOrder();
    void test_writeBeyondCapacityKeepsNewestFrames();
    void test_writeManyTimesCapacityKeepsNewestFrames();
    void test_wrapAroundPreservesChronologicalOrder();
    void test_emptyWriteIsIgnored();
    void test_repeatedSnapshotsDoNotMutateBuffer();
    void test_snapshotIsIndependentCopy();
    void test_singleFrameCapacityOverwritesEachTime();
    void test_isUsableThroughAudioSink();
    void test_capacityFromDurationUsesSampleRate();
    void test_capacityFromDurationUsesChannels();
    void test_fractionalDurationRoundsUp();
    void test_subFrameDurationRoundsUpToOneFrame();
    void test_zeroCapacityIsRejected();
    void test_zeroDurationIsRejected();
    void test_negativeDurationIsRejected();
    void test_overflowingDurationIsRejected();
    void test_concurrentWriteAndSnapshotStayConsistent();
};

TestAudioBuffer::TestAudioBuffer(QObject* parent) : QObject(parent) {
}

void TestAudioBuffer::test_explicitCapacityIsReported() {
    const audio::AudioBuffer kBuffer(8);

    QCOMPARE(kBuffer.capacity(), std::size_t{8});
}

void TestAudioBuffer::test_newBufferHasEmptySnapshot() {
    const audio::AudioBuffer kBuffer(4);

    QVERIFY(kBuffer.snapshot().empty());
}

void TestAudioBuffer::test_writeShorterThanCapacityKeepsOrder() {
    audio::AudioBuffer buffer(4);

    buffer.write(std::array{1.0F, 2.0F, 3.0F});

    verifyFrames(buffer.snapshot(), {1.0F, 2.0F, 3.0F});
}

void TestAudioBuffer::test_writeExactlyCapacityKeepsOrder() {
    audio::AudioBuffer buffer(4);

    buffer.write(std::array{1.0F, 2.0F, 3.0F, 4.0F});

    verifyFrames(buffer.snapshot(), {1.0F, 2.0F, 3.0F, 4.0F});
}

void TestAudioBuffer::test_writeBeyondCapacityKeepsNewestFrames() {
    audio::AudioBuffer buffer(4);

    buffer.write(std::array{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F});

    verifyFrames(buffer.snapshot(), {3.0F, 4.0F, 5.0F, 6.0F});
}

void TestAudioBuffer::test_writeManyTimesCapacityKeepsNewestFrames() {
    audio::AudioBuffer buffer(3);

    buffer.write(std::array{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F, 9.0F, 10.0F});

    verifyFrames(buffer.snapshot(), {8.0F, 9.0F, 10.0F});
}

void TestAudioBuffer::test_wrapAroundPreservesChronologicalOrder() {
    audio::AudioBuffer buffer(5);

    buffer.write(std::array{1.0F, 2.0F, 3.0F});
    buffer.write(std::array{4.0F, 5.0F, 6.0F});

    verifyFrames(buffer.snapshot(), {2.0F, 3.0F, 4.0F, 5.0F, 6.0F});
}

void TestAudioBuffer::test_emptyWriteIsIgnored() {
    audio::AudioBuffer buffer(4);

    buffer.write(std::span<const float>{});
    QVERIFY(buffer.snapshot().empty());

    buffer.write(std::array{1.0F, 2.0F});
    buffer.write(std::span<const float>{});
    verifyFrames(buffer.snapshot(), {1.0F, 2.0F});
}

void TestAudioBuffer::test_repeatedSnapshotsDoNotMutateBuffer() {
    audio::AudioBuffer buffer(4);
    buffer.write(std::array{1.0F, 2.0F, 3.0F});

    const std::vector<float> kFirst = buffer.snapshot();
    const std::vector<float> kSecond = buffer.snapshot();

    verifyFrames(kFirst, {1.0F, 2.0F, 3.0F});
    verifyFrames(kSecond, {1.0F, 2.0F, 3.0F});
}

void TestAudioBuffer::test_snapshotIsIndependentCopy() {
    audio::AudioBuffer buffer(4);
    buffer.write(std::array{1.0F, 2.0F, 3.0F});

    std::vector<float> snapshot = buffer.snapshot();
    snapshot[0] = 99.0F;
    snapshot.push_back(4.0F);

    verifyFrames(buffer.snapshot(), {1.0F, 2.0F, 3.0F});
}

void TestAudioBuffer::test_singleFrameCapacityOverwritesEachTime() {
    audio::AudioBuffer buffer(1);

    buffer.write(std::array{1.0F});
    verifyFrames(buffer.snapshot(), {1.0F});

    buffer.write(std::array{2.0F});
    verifyFrames(buffer.snapshot(), {2.0F});

    buffer.write(std::array{1.0F, 2.0F, 3.0F});
    verifyFrames(buffer.snapshot(), {3.0F});
}

void TestAudioBuffer::test_isUsableThroughAudioSink() {
    audio::AudioBuffer buffer(4);
    audio::AudioSink& sink = buffer;

    sink.write(std::array{1.0F, 2.0F});

    verifyFrames(buffer.snapshot(), {1.0F, 2.0F});
}

void TestAudioBuffer::test_capacityFromDurationUsesSampleRate() {
    const audio::AudioBuffer kBuffer(std::chrono::seconds(1), monoFormat(16000U));

    QCOMPARE(kBuffer.capacity(), std::size_t{16000});
}

void TestAudioBuffer::test_capacityFromDurationUsesChannels() {
    const audio::AudioFormat kStereo(16000U, 2U, audio::SampleFormat::kFloat32);

    const audio::AudioBuffer kBuffer(std::chrono::seconds(1), kStereo);

    QCOMPARE(kBuffer.capacity(), std::size_t{32000});
}

void TestAudioBuffer::test_fractionalDurationRoundsUp() {
    const audio::AudioBuffer kBuffer(std::chrono::duration<double>(1.05), monoFormat(10U));

    QCOMPARE(kBuffer.capacity(), std::size_t{11});
}

void TestAudioBuffer::test_subFrameDurationRoundsUpToOneFrame() {
    const audio::AudioBuffer kBuffer(std::chrono::duration<double>(1e-9), monoFormat(16000U));

    QCOMPARE(kBuffer.capacity(), std::size_t{1});
}

void TestAudioBuffer::test_zeroCapacityIsRejected() {
    QVERIFY(rejectsConstruction([] { static_cast<void>(audio::AudioBuffer(std::size_t{0})); }));
}

void TestAudioBuffer::test_zeroDurationIsRejected() {
    QVERIFY(rejectsConstruction(
            [] { static_cast<void>(audio::AudioBuffer(std::chrono::seconds(0), monoFormat(16000U))); }));
}

void TestAudioBuffer::test_negativeDurationIsRejected() {
    QVERIFY(rejectsConstruction(
            [] { static_cast<void>(audio::AudioBuffer(std::chrono::seconds(-1), monoFormat(16000U))); }));
}

void TestAudioBuffer::test_overflowingDurationIsRejected() {
    QVERIFY(rejectsConstruction(
            [] { static_cast<void>(audio::AudioBuffer(std::chrono::duration<double>(1e30), monoFormat(16000U))); }));
}

void TestAudioBuffer::test_concurrentWriteAndSnapshotStayConsistent() {
    constexpr std::size_t kCapacity = 256;
    // 100000 * 16 frames keeps every ramp value exact in f32 (< 2^24), so the
    // final contiguity check below is exact rather than approximate.
    constexpr std::size_t kChunkFrames = 16;
    constexpr std::size_t kChunkCount = 100000;
    constexpr std::size_t kTotalFrames = kChunkFrames * kChunkCount;

    audio::AudioBuffer buffer(kCapacity);
    std::atomic<bool> finished{false};

    std::thread producer([&buffer, &finished] {
        writeRamp(buffer, kChunkFrames, kChunkCount);
        finished.store(true, std::memory_order_release);
    });

    bool onlyBoundedSnapshots = true;
    while (!finished.load(std::memory_order_acquire)) {
        const std::vector<float> kSnapshot = buffer.snapshot();
        onlyBoundedSnapshots = onlyBoundedSnapshots && kSnapshot.size() <= kCapacity;
    }
    producer.join();

    QVERIFY(onlyBoundedSnapshots);

    // The settled window is the newest capacity_ frames, in order: no torn or
    // out-of-order frames survived the concurrent writes.
    const std::vector<float> kFinal = buffer.snapshot();
    QCOMPARE(kFinal.size(), kCapacity);
    QVERIFY(isConsecutiveRamp(kFinal));
    QCOMPARE(kFinal.back(), static_cast<float>(kTotalFrames));
}

QTEST_GUILESS_MAIN(TestAudioBuffer)

#include "tst_audiobuffer.moc"
