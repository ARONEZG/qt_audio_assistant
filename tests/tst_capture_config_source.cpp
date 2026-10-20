// Tests for CaptureConfigSource: the file + environment resolution the
// composition root performs at startup. The environment is injected, so the
// real process environment is never touched.
#include <QByteArray>
#include <QFile>
#include <QProcessEnvironment>
#include <QString>
#include <QTemporaryDir>
#include <QtTest>

#include "domain/CaptureConfig.h"
#include "domain/CaptureConfigFile.h"
#include "domain/CaptureConfigSource.h"

namespace {

[[nodiscard]] bool writeText(const QString& path, const QByteArray& text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    return file.write(text) == text.size();
}

[[nodiscard]] CaptureConfigFile captureFileAt(const QString& directory) {
    return CaptureConfigFile(QStringLiteral("%1/capture.env").arg(directory));
}

}  // namespace

class TestCaptureConfigSource : public QObject {
    Q_OBJECT

public:
    explicit TestCaptureConfigSource(QObject* parent = nullptr);

private slots:
    void test_missingFileAndEmptyEnvironmentYieldTheDefaults();
    void test_environmentOverridesEverySpeechField();
    void test_blankModelDoesNotOverrideTheFile();
    void test_absentLanguageMeansRussian();
    void test_absentLanguageKeepsTheFileValue();
    void test_emptyLanguageOverridesTheFile();
    void test_emptyLanguageMeansAutodetect();
    void test_absentPromptKeepsTheFileValue();
    void test_emptyPromptKeepsTheFileValue();
    void test_promptIsTrimmed();
    void test_captureWindowComesOnlyFromTheFile();
    void test_reportsFileErrors();
};

TestCaptureConfigSource::TestCaptureConfigSource(QObject* parent) : QObject(parent) {
}

void TestCaptureConfigSource::test_missingFileAndEmptyEnvironmentYieldTheDefaults() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile = captureFileAt(kDirectory.path());

    const CaptureConfigSource::Result kResolved = CaptureConfigSource(kFile, QProcessEnvironment{}).resolve();

    QVERIFY(kResolved.ok());
    const CaptureConfig kDefaults;
    QCOMPARE(kResolved.config.captureDurationSeconds, kDefaults.captureDurationSeconds);
    QCOMPARE(kResolved.config.sttModel, kDefaults.sttModel);
    QCOMPARE(kResolved.config.sttLanguage, kDefaults.sttLanguage);
    QCOMPARE(kResolved.config.sttPrompt, kDefaults.sttPrompt);
    QCOMPARE(kResolved.config.vadEnabled, kDefaults.vadEnabled);
    QCOMPARE(kResolved.config.minSpeechSeconds, kDefaults.minSpeechSeconds);
}

void TestCaptureConfigSource::test_environmentOverridesEverySpeechField() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile = captureFileAt(kDirectory.path());
    QVERIFY(writeText(kFile.path(),
                      QByteArrayLiteral("POLZA_STT_MODEL=file-model\n"
                                        "POLZA_STT_LANGUAGE=en\n"
                                        "POLZA_STT_PROMPT=file-prompt\n")));
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("POLZA_STT_MODEL"), QStringLiteral("env-model"));
    environment.insert(QStringLiteral("POLZA_STT_LANGUAGE"), QStringLiteral("de"));
    environment.insert(QStringLiteral("POLZA_STT_PROMPT"), QStringLiteral("env-prompt"));

    const CaptureConfigSource::Result kResolved = CaptureConfigSource(kFile, environment).resolve();

    QVERIFY(kResolved.ok());
    QCOMPARE(kResolved.config.sttModel, QStringLiteral("env-model"));
    QCOMPARE(kResolved.config.sttLanguage, QStringLiteral("de"));
    QCOMPARE(kResolved.config.sttPrompt, QStringLiteral("env-prompt"));
}

void TestCaptureConfigSource::test_blankModelDoesNotOverrideTheFile() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile = captureFileAt(kDirectory.path());
    QVERIFY(writeText(kFile.path(), QByteArrayLiteral("POLZA_STT_MODEL=file-model\n")));
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("POLZA_STT_MODEL"), QStringLiteral("   "));

    const CaptureConfigSource::Result kResolved = CaptureConfigSource(kFile, environment).resolve();

    QVERIFY(kResolved.ok());
    QCOMPARE(kResolved.config.sttModel, QStringLiteral("file-model"));
}

void TestCaptureConfigSource::test_absentLanguageMeansRussian() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());

    const CaptureConfigSource::Result kResolved =
            CaptureConfigSource(captureFileAt(kDirectory.path()), QProcessEnvironment{}).resolve();

    QVERIFY(kResolved.ok());
    QCOMPARE(kResolved.config.sttLanguage, QStringLiteral("ru"));
}

// The user's saved language must survive a restart: an absent environment
// variable no longer overwrites it with the default.
void TestCaptureConfigSource::test_absentLanguageKeepsTheFileValue() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile = captureFileAt(kDirectory.path());
    QVERIFY(writeText(kFile.path(), QByteArrayLiteral("POLZA_STT_LANGUAGE=en\n")));

    const CaptureConfigSource::Result kResolved = CaptureConfigSource(kFile, QProcessEnvironment{}).resolve();

    QVERIFY(kResolved.ok());
    QCOMPARE(kResolved.config.sttLanguage, QStringLiteral("en"));
}

// An explicitly empty language still overrides the file and means autodetect.
void TestCaptureConfigSource::test_emptyLanguageOverridesTheFile() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile = captureFileAt(kDirectory.path());
    QVERIFY(writeText(kFile.path(), QByteArrayLiteral("POLZA_STT_LANGUAGE=en\n")));
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("POLZA_STT_LANGUAGE"), QString());

    const CaptureConfigSource::Result kResolved = CaptureConfigSource(kFile, environment).resolve();

    QVERIFY(kResolved.ok());
    QVERIFY(kResolved.config.sttLanguage.isEmpty());
}

void TestCaptureConfigSource::test_emptyLanguageMeansAutodetect() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("POLZA_STT_LANGUAGE"), QString());

    const CaptureConfigSource::Result kResolved =
            CaptureConfigSource(captureFileAt(kDirectory.path()), environment).resolve();

    QVERIFY(kResolved.ok());
    QVERIFY(kResolved.config.sttLanguage.isEmpty());
}

// An absent prompt variable must not erase the context the user saved.
void TestCaptureConfigSource::test_absentPromptKeepsTheFileValue() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile = captureFileAt(kDirectory.path());
    QVERIFY(writeText(kFile.path(), QByteArrayLiteral("POLZA_STT_PROMPT=file-prompt\n")));

    const CaptureConfigSource::Result kResolved = CaptureConfigSource(kFile, QProcessEnvironment{}).resolve();

    QVERIFY(kResolved.ok());
    QCOMPARE(kResolved.config.sttPrompt, QStringLiteral("file-prompt"));
}

// An explicitly empty prompt also keeps the file value (only a non-empty
// environment value overrides it).
void TestCaptureConfigSource::test_emptyPromptKeepsTheFileValue() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile = captureFileAt(kDirectory.path());
    QVERIFY(writeText(kFile.path(), QByteArrayLiteral("POLZA_STT_PROMPT=file-prompt\n")));
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("POLZA_STT_PROMPT"), QString());

    const CaptureConfigSource::Result kResolved = CaptureConfigSource(kFile, environment).resolve();

    QVERIFY(kResolved.ok());
    QCOMPARE(kResolved.config.sttPrompt, QStringLiteral("file-prompt"));
}

void TestCaptureConfigSource::test_promptIsTrimmed() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("POLZA_STT_PROMPT"), QStringLiteral("  C++, Qt  "));

    const CaptureConfigSource::Result kResolved =
            CaptureConfigSource(captureFileAt(kDirectory.path()), environment).resolve();

    QVERIFY(kResolved.ok());
    QCOMPARE(kResolved.config.sttPrompt, QStringLiteral("C++, Qt"));
}

void TestCaptureConfigSource::test_captureWindowComesOnlyFromTheFile() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile = captureFileAt(kDirectory.path());
    QVERIFY(writeText(kFile.path(),
                      QByteArrayLiteral("AA_CAPTURE_SECONDS=45\n"
                                        "AA_VAD_ENABLED=false\n"
                                        "AA_MIN_SPEECH_SECONDS=1.5\n")));
    // Capture-window fields have no environment variables: unrelated variables
    // must not influence them.
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("AA_CAPTURE_SECONDS"), QStringLiteral("10"));
    environment.insert(QStringLiteral("AA_VAD_ENABLED"), QStringLiteral("true"));
    environment.insert(QStringLiteral("AA_MIN_SPEECH_SECONDS"), QStringLiteral("0.25"));

    const CaptureConfigSource::Result kResolved = CaptureConfigSource(kFile, environment).resolve();

    QVERIFY(kResolved.ok());
    QCOMPARE(kResolved.config.captureDurationSeconds, 45);
    QCOMPARE(kResolved.config.vadEnabled, false);
    QCOMPARE(kResolved.config.minSpeechSeconds, 1.5);
}

void TestCaptureConfigSource::test_reportsFileErrors() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile = captureFileAt(kDirectory.path());
    QVERIFY(writeText(kFile.path(), QByteArrayLiteral("garbage\n")));

    const CaptureConfigSource::Result kResolved = CaptureConfigSource(kFile, QProcessEnvironment{}).resolve();

    QVERIFY(!kResolved.ok());
}

QTEST_GUILESS_MAIN(TestCaptureConfigSource)

#include "tst_capture_config_source.moc"
