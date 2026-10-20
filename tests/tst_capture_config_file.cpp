// Tests for CaptureConfigFile: the KEY=VALUE store of the capture and
// transcription settings. No real user configuration is touched: every path is
// a temporary directory.
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QtTest>

#include "domain/CaptureConfig.h"
#include "domain/CaptureConfigFile.h"

namespace {

[[nodiscard]] bool writeText(const QString& path, const QByteArray& text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    return file.write(text) == text.size();
}

[[nodiscard]] QString readText(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

[[nodiscard]] CaptureConfig sampleConfig() {
    return {45,
            QStringLiteral("openai/whisper-large-v3"),
            QStringLiteral("en"),
            QStringLiteral("C++, Qt"),
            false,
            1.25};
}

}  // namespace

class TestCaptureConfigFile : public QObject {
    Q_OBJECT

public:
    explicit TestCaptureConfigFile(QObject* parent = nullptr);

private slots:
    void test_missingFileYieldsTheDefaults();
    void test_roundTripPersistsAllFields();
    void test_savedFileIsEnvCompatibleAndLocaleIndependent();
    void test_saveCreatesTheMissingParentDirectory();
    void test_loadSkipsCommentsAndUnknownKeys();
    void test_loadKeepsDefaultsForMissingKeys();
    void test_loadRejectsMalformedLine();
    void test_loadRejectsInvalidNumericValue();
    void test_loadRejectsInvalidBooleanValue();
    void test_saveRejectsInvalidConfig();
    void test_emptyPathIsReportedHonestly();
};

TestCaptureConfigFile::TestCaptureConfigFile(QObject* parent) : QObject(parent) {
}

void TestCaptureConfigFile::test_missingFileYieldsTheDefaults() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile(kDirectory.filePath(QStringLiteral("capture.env")));

    const CaptureConfigFile::LoadResult kLoaded = kFile.load();

    QVERIFY(kLoaded.ok());
    const CaptureConfig kDefaults;
    QCOMPARE(kLoaded.config.captureDurationSeconds, kDefaults.captureDurationSeconds);
    QCOMPARE(kLoaded.config.sttModel, kDefaults.sttModel);
    QCOMPARE(kLoaded.config.sttLanguage, kDefaults.sttLanguage);
    QCOMPARE(kLoaded.config.sttPrompt, kDefaults.sttPrompt);
    QCOMPARE(kLoaded.config.vadEnabled, kDefaults.vadEnabled);
    QCOMPARE(kLoaded.config.minSpeechSeconds, kDefaults.minSpeechSeconds);
}

void TestCaptureConfigFile::test_roundTripPersistsAllFields() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile(kDirectory.filePath(QStringLiteral("capture.env")));
    const CaptureConfig kConfig = sampleConfig();

    QVERIFY(kFile.save(kConfig).ok());
    const CaptureConfigFile::LoadResult kLoaded = kFile.load();

    QVERIFY(kLoaded.ok());
    QCOMPARE(kLoaded.config.captureDurationSeconds, kConfig.captureDurationSeconds);
    QCOMPARE(kLoaded.config.sttModel, kConfig.sttModel);
    QCOMPARE(kLoaded.config.sttLanguage, kConfig.sttLanguage);
    QCOMPARE(kLoaded.config.sttPrompt, kConfig.sttPrompt);
    QCOMPARE(kLoaded.config.vadEnabled, kConfig.vadEnabled);
    QCOMPARE(kLoaded.config.minSpeechSeconds, kConfig.minSpeechSeconds);
}

void TestCaptureConfigFile::test_savedFileIsEnvCompatibleAndLocaleIndependent() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile(kDirectory.filePath(QStringLiteral("capture.env")));

    QVERIFY(kFile.save(sampleConfig()).ok());
    const QString kText = readText(kFile.path());

    QVERIFY(kText.contains(QStringLiteral("POLZA_STT_MODEL=openai/whisper-large-v3")));
    QVERIFY(kText.contains(QStringLiteral("POLZA_STT_LANGUAGE=en")));
    QVERIFY(kText.contains(QStringLiteral("POLZA_STT_PROMPT=C++, Qt")));
    QVERIFY(kText.contains(QStringLiteral("AA_CAPTURE_SECONDS=45")));
    QVERIFY(kText.contains(QStringLiteral("AA_VAD_ENABLED=false")));
    // Always '.', never a locale decimal comma.
    QVERIFY(kText.contains(QStringLiteral("AA_MIN_SPEECH_SECONDS=1.25")));
}

void TestCaptureConfigFile::test_saveCreatesTheMissingParentDirectory() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const QString kPath = QDir(kDirectory.path()).filePath(QStringLiteral("nested/config/capture.env"));
    const CaptureConfigFile kFile(kPath);

    QVERIFY(kFile.save(sampleConfig()).ok());

    QVERIFY(QFile::exists(kPath));
}

void TestCaptureConfigFile::test_loadSkipsCommentsAndUnknownKeys() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile(kDirectory.filePath(QStringLiteral("capture.env")));
    QVERIFY(writeText(kFile.path(),
                      QByteArrayLiteral("# comment\n"
                                        "\n"
                                        "SOME_OTHER_KEY=ignored\n"
                                        "POLZA_STT_MODEL=from-file\n"
                                        "POLZA_STT_LANGUAGE=de\n"
                                        "POLZA_STT_PROMPT=hint\n"
                                        "AA_CAPTURE_SECONDS=30\n"
                                        "AA_VAD_ENABLED=true\n"
                                        "AA_MIN_SPEECH_SECONDS=0.5\n")));

    const CaptureConfigFile::LoadResult kLoaded = kFile.load();

    QVERIFY(kLoaded.ok());
    QCOMPARE(kLoaded.config.sttModel, QStringLiteral("from-file"));
    QCOMPARE(kLoaded.config.sttLanguage, QStringLiteral("de"));
    QCOMPARE(kLoaded.config.sttPrompt, QStringLiteral("hint"));
    QCOMPARE(kLoaded.config.captureDurationSeconds, 30);
    QCOMPARE(kLoaded.config.vadEnabled, true);
    QCOMPARE(kLoaded.config.minSpeechSeconds, 0.5);
}

void TestCaptureConfigFile::test_loadKeepsDefaultsForMissingKeys() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile(kDirectory.filePath(QStringLiteral("capture.env")));
    QVERIFY(writeText(kFile.path(), QByteArrayLiteral("AA_CAPTURE_SECONDS=30\n")));

    const CaptureConfigFile::LoadResult kLoaded = kFile.load();

    QVERIFY(kLoaded.ok());
    const CaptureConfig kDefaults;
    QCOMPARE(kLoaded.config.captureDurationSeconds, 30);
    QCOMPARE(kLoaded.config.sttModel, kDefaults.sttModel);
    QCOMPARE(kLoaded.config.sttLanguage, kDefaults.sttLanguage);
    QCOMPARE(kLoaded.config.sttPrompt, kDefaults.sttPrompt);
    QCOMPARE(kLoaded.config.vadEnabled, kDefaults.vadEnabled);
    QCOMPARE(kLoaded.config.minSpeechSeconds, kDefaults.minSpeechSeconds);
}

void TestCaptureConfigFile::test_loadRejectsMalformedLine() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile(kDirectory.filePath(QStringLiteral("capture.env")));
    QVERIFY(writeText(kFile.path(), QByteArrayLiteral("this line has no separator\n")));

    const CaptureConfigFile::LoadResult kLoaded = kFile.load();

    QVERIFY(!kLoaded.ok());
    QVERIFY(kLoaded.error.contains(QStringLiteral("line 1")));
}

void TestCaptureConfigFile::test_loadRejectsInvalidNumericValue() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile(kDirectory.filePath(QStringLiteral("capture.env")));
    QVERIFY(writeText(kFile.path(), QByteArrayLiteral("AA_CAPTURE_SECONDS=twenty\n")));

    const CaptureConfigFile::LoadResult kLoaded = kFile.load();

    QVERIFY(!kLoaded.ok());
    QVERIFY(kLoaded.error.contains(QStringLiteral("line 1")));
}

void TestCaptureConfigFile::test_loadRejectsInvalidBooleanValue() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile(kDirectory.filePath(QStringLiteral("capture.env")));
    QVERIFY(writeText(kFile.path(), QByteArrayLiteral("AA_VAD_ENABLED=yes\n")));

    const CaptureConfigFile::LoadResult kLoaded = kFile.load();

    QVERIFY(!kLoaded.ok());
    QVERIFY(kLoaded.error.contains(QStringLiteral("line 1")));
}

void TestCaptureConfigFile::test_saveRejectsInvalidConfig() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const CaptureConfigFile kFile(kDirectory.filePath(QStringLiteral("capture.env")));
    CaptureConfig config;
    config.captureDurationSeconds = 500;

    const CaptureConfigFile::SaveResult kSaved = kFile.save(config);

    QVERIFY(!kSaved.ok());
    QVERIFY(!kSaved.error.isEmpty());
    QVERIFY(!QFile::exists(kFile.path()));
}

void TestCaptureConfigFile::test_emptyPathIsReportedHonestly() {
    const CaptureConfigFile kFile{QString()};

    QVERIFY(!kFile.load().ok());
    QVERIFY(!kFile.save(sampleConfig()).ok());
}

QTEST_GUILESS_MAIN(TestCaptureConfigFile)

#include "tst_capture_config_file.moc"
