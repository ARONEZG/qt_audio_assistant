// Smoke test for the miniaudio dependency wiring.
//
// What it proves and why it is not capture logic:
//  * <miniaudio.h> is included here through the SYSTEM include directory
//    configured in CMakeLists.txt, so the strict -Weverything -Werror profile
//    still applies to this project-owned test code while diagnostics inside the
//    third-party header stay suppressed. A successful build therefore proves
//    the SYSTEM wiring, not any audio behaviour.
//  * ma_version_string() is defined in the implementation translation unit
//    owned by audio-assistant-audio, so referencing it forces that object into
//    the link and verifies that miniaudio's platform system libraries
//    (m/pthread/dl on Linux; CoreAudio/AudioUnit/CoreFoundation frameworks on
//    macOS) actually resolve.
//  * The assertions read compile-time version constants, the linked version
//    string, and a pure backend capability query (ma_is_loopback_supported).
//    No device is opened, enumerated, started or stopped, so the test stays
//    deterministic and never touches a real audio device.
#include <miniaudio.h>

#include <QString>
#include <QtTest>

namespace {

// The pinned miniaudio version (see `requires` in conanfile.py). Keeping the
// expectation here means a silently resolved different version fails the test
// instead of going unnoticed.
constexpr int kExpectedMajor = 0;
constexpr int kExpectedMinor = 11;
constexpr int kExpectedRevision = 25;

}  // namespace

class TestAudioSmoke : public QObject {
    Q_OBJECT

public:
    explicit TestAudioSmoke(QObject* parent = nullptr);

private slots:
    void test_versionConstantsMatchPinnedDependency();
    void test_implementationIsLinkedAndReportsVersion();
    void test_loopbackSupportMatchesDocumentedMatrix();
};

TestAudioSmoke::TestAudioSmoke(QObject* parent) : QObject(parent) {
}

void TestAudioSmoke::test_versionConstantsMatchPinnedDependency() {
    QCOMPARE(MA_VERSION_MAJOR, kExpectedMajor);
    QCOMPARE(MA_VERSION_MINOR, kExpectedMinor);
    QCOMPARE(MA_VERSION_REVISION, kExpectedRevision);
}

void TestAudioSmoke::test_implementationIsLinkedAndReportsVersion() {
    const QString kVersion = QString::fromLatin1(ma_version_string());
    QCOMPARE(kVersion, QStringLiteral("0.11.25"));
}

void TestAudioSmoke::test_loopbackSupportMatchesDocumentedMatrix() {
    // Documented in docs/ADR-audio-capture.md: native loopback is a WASAPI
    // (Windows) feature; PulseAudio and CoreAudio expose no native loopback
    // mode (Linux uses a PulseAudio/PipeWire-Pulse monitor source instead, and
    // macOS loopback is an open gap).
    QVERIFY(ma_is_loopback_supported(ma_backend_wasapi) == MA_TRUE);
    QVERIFY(ma_is_loopback_supported(ma_backend_pulseaudio) == MA_FALSE);
    QVERIFY(ma_is_loopback_supported(ma_backend_coreaudio) == MA_FALSE);
}

QTEST_GUILESS_MAIN(TestAudioSmoke)

#include "tst_audio_smoke.moc"
