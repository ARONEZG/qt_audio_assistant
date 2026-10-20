// Tests for CaptureSettings: the mutable, QML-facing configuration object with
// a single save command. Persistence goes to a temporary file, never the user's
// real configuration.
#include <QFile>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QtTest>

#include "domain/CaptureConfig.h"
#include "domain/CaptureConfigFile.h"
#include "domain/CapturePresets.h"
#include "domain/CaptureSettings.h"

namespace {

[[nodiscard]] QString readText(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

[[nodiscard]] CaptureConfig initialConfig() {
    return {30, QStringLiteral("openai/whisper-1"), QStringLiteral("en"), QString(), false, 1.0};
}

}  // namespace

class TestCaptureSettings : public QObject {
    Q_OBJECT

public:
    explicit TestCaptureSettings(QObject* parent = nullptr);

private slots:
    void test_exposesInitialValuesAndPresets();
    void test_saveReplacesTheConfigurationAsAWhole();
    void test_saveNormalisesTextFields();
    void test_savePersistsEveryField();
    void test_saveWithUnchangedValuesOnlyEmitsSaved();
    void test_saveRejectsInvalidInput_data();
    void test_saveRejectsInvalidInput();
    void test_rejectedSaveLeavesTheFileUntouched();
    void test_clearErrorClearsTheSaveError();
    void test_saveAcceptsEmptyLanguageAndPrompt();
};

TestCaptureSettings::TestCaptureSettings(QObject* parent) : QObject(parent) {
}

void TestCaptureSettings::test_exposesInitialValuesAndPresets() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    CapturePresets presets;
    const CaptureSettings kSettings(
            initialConfig(), CaptureConfigFile(kDirectory.filePath(QStringLiteral("capture.env"))), &presets);

    QCOMPARE(kSettings.captureDurationSeconds(), 30);
    QCOMPARE(kSettings.sttModel(), QStringLiteral("openai/whisper-1"));
    QCOMPARE(kSettings.sttLanguage(), QStringLiteral("en"));
    QVERIFY(kSettings.sttPrompt().isEmpty());
    QCOMPARE(kSettings.vadEnabled(), false);
    QCOMPARE(kSettings.minSpeechSeconds(), 1.0);
    QVERIFY(kSettings.lastError().isEmpty());
    // The presets model is borrowed, not copied.
    QCOMPARE(kSettings.presets(), &presets);
    QCOMPARE(kSettings.presets()->count(), 3);
}

void TestCaptureSettings::test_saveReplacesTheConfigurationAsAWhole() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    CapturePresets presets;
    CaptureSettings settings(
            initialConfig(), CaptureConfigFile(kDirectory.filePath(QStringLiteral("capture.env"))), &presets);
    const QSignalSpy kSavedSpy(&settings, &CaptureSettings::saved);
    const QSignalSpy kDurationSpy(&settings, &CaptureSettings::captureDurationSecondsChanged);
    const QSignalSpy kModelSpy(&settings, &CaptureSettings::sttModelChanged);
    const QSignalSpy kLanguageSpy(&settings, &CaptureSettings::sttLanguageChanged);
    const QSignalSpy kPromptSpy(&settings, &CaptureSettings::sttPromptChanged);
    const QSignalSpy kVadSpy(&settings, &CaptureSettings::vadEnabledChanged);
    const QSignalSpy kMinSpeechSpy(&settings, &CaptureSettings::minSpeechSecondsChanged);

    QVERIFY(settings.save(
            45, QStringLiteral("openai/whisper-large-v3"), QStringLiteral("de"), QStringLiteral("C++, Qt"), true, 1.5));

    QCOMPARE(kSavedSpy.count(), 1);
    QCOMPARE(kDurationSpy.count(), 1);
    QCOMPARE(kModelSpy.count(), 1);
    QCOMPARE(kLanguageSpy.count(), 1);
    QCOMPARE(kPromptSpy.count(), 1);
    QCOMPARE(kVadSpy.count(), 1);
    QCOMPARE(kMinSpeechSpy.count(), 1);
    QCOMPARE(settings.captureDurationSeconds(), 45);
    QCOMPARE(settings.sttModel(), QStringLiteral("openai/whisper-large-v3"));
    QCOMPARE(settings.sttLanguage(), QStringLiteral("de"));
    QCOMPARE(settings.sttPrompt(), QStringLiteral("C++, Qt"));
    QCOMPARE(settings.vadEnabled(), true);
    QCOMPARE(settings.minSpeechSeconds(), 1.5);
    QVERIFY(settings.lastError().isEmpty());
}

void TestCaptureSettings::test_saveNormalisesTextFields() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    CapturePresets presets;
    CaptureSettings settings({}, CaptureConfigFile(kDirectory.filePath(QStringLiteral("capture.env"))), &presets);

    QVERIFY(settings.save(20,
                          QStringLiteral("  openai/whisper-medium  "),
                          QStringLiteral("  ru  "),
                          QStringLiteral("  hint  "),
                          true,
                          0.75));

    QCOMPARE(settings.sttModel(), QStringLiteral("openai/whisper-medium"));
    QCOMPARE(settings.sttLanguage(), QStringLiteral("ru"));
    QCOMPARE(settings.sttPrompt(), QStringLiteral("hint"));
}

void TestCaptureSettings::test_savePersistsEveryField() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile(kDirectory.filePath(QStringLiteral("capture.env")));
    CapturePresets presets;
    CaptureSettings settings({}, kFile, &presets);

    QVERIFY(settings.save(
            60, QStringLiteral("openai/whisper-large-v3"), QStringLiteral("en"), QStringLiteral("hint"), false, 2.0));

    const CaptureConfigFile::LoadResult kLoaded = kFile.load();
    QVERIFY(kLoaded.ok());
    QCOMPARE(kLoaded.config.captureDurationSeconds, 60);
    QCOMPARE(kLoaded.config.sttModel, QStringLiteral("openai/whisper-large-v3"));
    QCOMPARE(kLoaded.config.sttLanguage, QStringLiteral("en"));
    QCOMPARE(kLoaded.config.sttPrompt, QStringLiteral("hint"));
    QCOMPARE(kLoaded.config.vadEnabled, false);
    QCOMPARE(kLoaded.config.minSpeechSeconds, 2.0);

    const QString kText = readText(kFile.path());
    QVERIFY(kText.contains(QStringLiteral("AA_CAPTURE_SECONDS=60")));
    QVERIFY(kText.contains(QStringLiteral("AA_VAD_ENABLED=false")));
}

void TestCaptureSettings::test_saveWithUnchangedValuesOnlyEmitsSaved() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    CapturePresets presets;
    CaptureSettings settings({}, CaptureConfigFile(kDirectory.filePath(QStringLiteral("capture.env"))), &presets);
    QVERIFY(settings.save(20, QStringLiteral("openai/whisper-1"), QStringLiteral("ru"), QString(), true, 0.75));
    const QSignalSpy kDurationSpy(&settings, &CaptureSettings::captureDurationSecondsChanged);
    const QSignalSpy kModelSpy(&settings, &CaptureSettings::sttModelChanged);
    const QSignalSpy kSavedSpy(&settings, &CaptureSettings::saved);

    // The same values again: only the completion of the command is observable.
    QVERIFY(settings.save(20, QStringLiteral("openai/whisper-1"), QStringLiteral("ru"), QString(), true, 0.75));

    QCOMPARE(kDurationSpy.count(), 0);
    QCOMPARE(kModelSpy.count(), 0);
    QCOMPARE(kSavedSpy.count(), 1);
}

void TestCaptureSettings::test_saveRejectsInvalidInput_data() {
    QTest::addColumn<int>("duration");
    QTest::addColumn<QString>("model");
    QTest::addColumn<QString>("prompt");
    QTest::addColumn<double>("minSpeech");

    QTest::newRow("duration below range") << 4 << QStringLiteral("openai/whisper-1") << QString() << 0.75;
    QTest::newRow("duration above range") << 121 << QStringLiteral("openai/whisper-1") << QString() << 0.75;
    QTest::newRow("empty model") << 20 << QString() << QString() << 0.75;
    QTest::newRow("multiline model") << 20 << QStringLiteral("whisper\nlarge") << QString() << 0.75;
    QTest::newRow("multiline prompt") << 20 << QStringLiteral("openai/whisper-1") << QStringLiteral("a\nb") << 0.75;
    QTest::newRow("min speech below range") << 20 << QStringLiteral("openai/whisper-1") << QString() << 0.1;
    QTest::newRow("min speech above range") << 20 << QStringLiteral("openai/whisper-1") << QString() << 4.0;
}

void TestCaptureSettings::test_saveRejectsInvalidInput() {
    QFETCH(int, duration);      // NOLINT(misc-const-correctness) - QFETCH cannot be const
    QFETCH(QString, model);     // NOLINT(misc-const-correctness) - QFETCH cannot be const
    QFETCH(QString, prompt);    // NOLINT(misc-const-correctness) - QFETCH cannot be const
    QFETCH(double, minSpeech);  // NOLINT(misc-const-correctness) - QFETCH cannot be const

    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile(kDirectory.filePath(QStringLiteral("capture.env")));
    CapturePresets presets;
    CaptureSettings settings(initialConfig(), kFile, &presets);
    QVERIFY(settings.save(30, QStringLiteral("openai/whisper-1"), QStringLiteral("en"), QString(), false, 1.0));
    const QString kPreviousText = readText(kFile.path());
    const QSignalSpy kSavedSpy(&settings, &CaptureSettings::saved);
    const QSignalSpy kDurationSpy(&settings, &CaptureSettings::captureDurationSecondsChanged);

    QVERIFY(!settings.save(duration, model, QStringLiteral("de"), prompt, true, minSpeech));

    // A rejected save is atomic: neither the stored value nor the file changes,
    // and no success signal is emitted.
    QVERIFY(!settings.lastError().isEmpty());
    QCOMPARE(kSavedSpy.count(), 0);
    QCOMPARE(kDurationSpy.count(), 0);
    QCOMPARE(settings.captureDurationSeconds(), 30);
    QCOMPARE(settings.sttModel(), QStringLiteral("openai/whisper-1"));
    QCOMPARE(settings.sttLanguage(), QStringLiteral("en"));
    QCOMPARE(settings.vadEnabled(), false);
    QCOMPARE(readText(kFile.path()), kPreviousText);
}

void TestCaptureSettings::test_rejectedSaveLeavesTheFileUntouched() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile(kDirectory.filePath(QStringLiteral("capture.env")));
    CapturePresets presets;
    CaptureSettings settings({}, kFile, &presets);

    // Nothing has been saved yet, so a rejected save must not create the file.
    QVERIFY(!settings.save(500, QStringLiteral("openai/whisper-1"), QStringLiteral("ru"), QString(), true, 0.75));

    QVERIFY(!QFile::exists(kFile.path()));
    QCOMPARE(settings.captureDurationSeconds(), 20);
}

void TestCaptureSettings::test_clearErrorClearsTheSaveError() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    CapturePresets presets;
    CaptureSettings settings({}, CaptureConfigFile(kDirectory.filePath(QStringLiteral("capture.env"))), &presets);
    const QSignalSpy kErrorSpy(&settings, &CaptureSettings::lastErrorChanged);

    QVERIFY(!settings.save(500, QStringLiteral("openai/whisper-1"), QStringLiteral("ru"), QString(), true, 0.75));
    QVERIFY(!settings.lastError().isEmpty());

    settings.clearError();

    QVERIFY(settings.lastError().isEmpty());
    // One change when the error appeared, one when it was cleared.
    QCOMPARE(kErrorSpy.count(), 2);

    // Clearing an already empty error is a no-op.
    settings.clearError();
    QCOMPARE(kErrorSpy.count(), 2);
}

void TestCaptureSettings::test_saveAcceptsEmptyLanguageAndPrompt() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile(kDirectory.filePath(QStringLiteral("capture.env")));
    CapturePresets presets;
    CaptureSettings settings({}, kFile, &presets);

    QVERIFY(settings.save(20, QStringLiteral("openai/whisper-1"), QString(), QString(), true, 0.75));

    QVERIFY(settings.sttLanguage().isEmpty());
    QVERIFY(settings.sttPrompt().isEmpty());
    const CaptureConfigFile::LoadResult kLoaded = kFile.load();
    QVERIFY(kLoaded.ok());
    QVERIFY(kLoaded.config.sttLanguage.isEmpty());
    QVERIFY(kLoaded.config.sttPrompt.isEmpty());
}

QTEST_GUILESS_MAIN(TestCaptureSettings)

#include "tst_capture_settings.moc"
