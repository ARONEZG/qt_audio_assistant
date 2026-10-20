// Tests for CapturePresets: the immutable, QML-facing list of capture schemes.
// The model is exercised directly; no view and no settings object is involved.
#include <QByteArray>
#include <QModelIndex>
#include <QString>
#include <QtTest>
#include <set>

#include "domain/CaptureConfig.h"
#include "domain/CapturePresets.h"

class TestCapturePresets : public QObject {
    Q_OBJECT

public:
    explicit TestCapturePresets(QObject* parent = nullptr);

private slots:
    void test_exposesThreePresets();
    void test_idsAreUnique();
    void test_balancedPreset();
    void test_longPreset();
    void test_quickPreset();
    void test_rolesExposeIdAndDisplayName();
    void test_outOfRangeQueriesReturnNothing();
};

TestCapturePresets::TestCapturePresets(QObject* parent) : QObject(parent) {
}

void TestCapturePresets::test_exposesThreePresets() {
    const CapturePresets kPresets;

    QCOMPARE(kPresets.count(), 3);
    QCOMPARE(kPresets.rowCount(), 3);
    // A list model has no child rows under a valid parent index.
    QCOMPARE(kPresets.rowCount(kPresets.index(0, 0)), 0);
}

void TestCapturePresets::test_idsAreUnique() {
    const CapturePresets kPresets;
    std::set<QString> ids;

    for (int index = 0; index < kPresets.count(); ++index) {
        ids.insert(kPresets.idAt(index));
    }

    QCOMPARE(ids.size(), static_cast<std::size_t>(kPresets.count()));
}

void TestCapturePresets::test_balancedPreset() {
    const CapturePresets kPresets;
    constexpr int kIndex = 0;

    QCOMPARE(kPresets.idAt(kIndex), QStringLiteral("balanced"));
    QCOMPARE(kPresets.displayNameAt(kIndex), QStringLiteral("Balanced"));
    QCOMPARE(kPresets.captureDurationSecondsAt(kIndex), 20);
    QCOMPARE(kPresets.sttModelAt(kIndex), QStringLiteral("openai/whisper-1"));
    QCOMPARE(kPresets.sttLanguageAt(kIndex), QStringLiteral("ru"));
    QVERIFY(kPresets.sttPromptAt(kIndex).isEmpty());
    QCOMPARE(kPresets.vadEnabledAt(kIndex), true);
    QCOMPARE(kPresets.minSpeechSecondsAt(kIndex), 0.75);

    const CaptureConfig kConfig = kPresets.configAt(kIndex);
    QCOMPARE(kConfig.captureDurationSeconds, 20);
    QCOMPARE(kConfig.sttModel, QStringLiteral("openai/whisper-1"));
    QCOMPARE(kConfig.sttLanguage, QStringLiteral("ru"));
    QVERIFY(kConfig.sttPrompt.isEmpty());
    QCOMPARE(kConfig.vadEnabled, true);
    QCOMPARE(kConfig.minSpeechSeconds, 0.75);
}

void TestCapturePresets::test_longPreset() {
    const CapturePresets kPresets;
    constexpr int kIndex = 1;

    QCOMPARE(kPresets.idAt(kIndex), QStringLiteral("long"));
    QCOMPARE(kPresets.displayNameAt(kIndex), QStringLiteral("Long recordings"));
    QCOMPARE(kPresets.captureDurationSecondsAt(kIndex), 120);
    QCOMPARE(kPresets.minSpeechSecondsAt(kIndex), 0.75);
    QCOMPARE(kPresets.configAt(kIndex).captureDurationSeconds, 120);
}

void TestCapturePresets::test_quickPreset() {
    const CapturePresets kPresets;
    constexpr int kIndex = 2;

    QCOMPARE(kPresets.idAt(kIndex), QStringLiteral("quick"));
    QCOMPARE(kPresets.displayNameAt(kIndex), QStringLiteral("Quick commands"));
    QCOMPARE(kPresets.captureDurationSecondsAt(kIndex), 10);
    QCOMPARE(kPresets.minSpeechSecondsAt(kIndex), 0.25);
    QCOMPARE(kPresets.configAt(kIndex).minSpeechSeconds, 0.25);
}

void TestCapturePresets::test_rolesExposeIdAndDisplayName() {
    const CapturePresets kPresets;
    const QHash<int, QByteArray> kRoles = kPresets.roleNames();

    QCOMPARE(kRoles.value(CapturePresets::kIdRole), QByteArrayLiteral("id"));
    QCOMPARE(kRoles.value(CapturePresets::kDisplayNameRole), QByteArrayLiteral("displayName"));
    QCOMPARE(kPresets.data(kPresets.index(0, 0), CapturePresets::kIdRole).toString(), QStringLiteral("balanced"));
    QCOMPARE(kPresets.data(kPresets.index(0, 0), CapturePresets::kDisplayNameRole).toString(),
             QStringLiteral("Balanced"));
    // An unknown role and an invalid index both yield nothing.
    QVERIFY(!kPresets.data(kPresets.index(0, 0), Qt::DisplayRole).isValid());
    QVERIFY(!kPresets.data(QModelIndex(), CapturePresets::kIdRole).isValid());
}

void TestCapturePresets::test_outOfRangeQueriesReturnNothing() {
    const CapturePresets kPresets;

    QVERIFY(kPresets.idAt(-1).isNull());
    QVERIFY(kPresets.displayNameAt(kPresets.count()).isNull());
    QCOMPARE(kPresets.captureDurationSecondsAt(-1), 0);
    QVERIFY(kPresets.sttModelAt(99).isNull());
    QVERIFY(kPresets.sttLanguageAt(99).isNull());
    QVERIFY(kPresets.sttPromptAt(99).isNull());
    QCOMPARE(kPresets.vadEnabledAt(99), false);
    QCOMPARE(kPresets.minSpeechSecondsAt(99), 0.0);

    const CaptureConfig kFallback = kPresets.configAt(99);
    const CaptureConfig kDefaults;
    QCOMPARE(kFallback.captureDurationSeconds, kDefaults.captureDurationSeconds);
    QCOMPARE(kFallback.sttModel, kDefaults.sttModel);
}

QTEST_GUILESS_MAIN(TestCapturePresets)

#include "tst_capture_presets.moc"
