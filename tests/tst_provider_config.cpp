#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QString>
#include <QTemporaryDir>
#include <QtTest>

#include "domain/ProviderConfig.h"
#include "domain/ProviderConfigFile.h"
#include "domain/ProviderConfigSource.h"

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

[[nodiscard]] QProcessEnvironment emptyEnvironment() {
    return {};
}

}  // namespace

// Covers the env-compatible (KEY=VALUE) provider configuration file and the
// file + environment resolution the composition root performs at startup.
class TestProviderConfig : public QObject {
    Q_OBJECT

public:
    explicit TestProviderConfig(QObject* parent = nullptr);

private slots:
    void test_missingFileIsAnEmptyConfiguration();
    void test_roundTripPersistsBaseUrlAndModel();
    void test_savedFileIsEnvCompatibleAndHasNoApiKey();
    void test_saveCreatesTheMissingParentDirectory();
    void test_loadSkipsCommentsAndUnknownKeys();
    void test_loadRejectsMalformedLine();
    void test_saveRejectsInvalidConfig_data();
    void test_saveRejectsInvalidConfig();
    void test_saveRejectsLineBreaks();
    void test_emptyPathIsReportedHonestly();

    void test_environmentOverridesFilePerKey();
    void test_apiKeyComesOnlyFromTheEnvironment();
    void test_blankEnvironmentDoesNotOverrideTheFile();
    void test_sourceReportsFileErrors();
};

TestProviderConfig::TestProviderConfig(QObject* parent) : QObject(parent) {
}

void TestProviderConfig::test_missingFileIsAnEmptyConfiguration() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));

    const ProviderConfigFile::LoadResult kLoaded = kFile.load();

    QVERIFY(kLoaded.ok());
    QVERIFY(kLoaded.config.baseUrl.isEmpty());
    QVERIFY(kLoaded.config.model.isEmpty());
}

void TestProviderConfig::test_roundTripPersistsBaseUrlAndModel() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));
    const ProviderConfig kConfig{QStringLiteral("https://example.test/api/v1"), QStringLiteral("some-model")};

    QVERIFY(kFile.save(kConfig).ok());
    const ProviderConfigFile::LoadResult kLoaded = kFile.load();

    QVERIFY(kLoaded.ok());
    QCOMPARE(kLoaded.config.baseUrl, kConfig.baseUrl);
    QCOMPARE(kLoaded.config.model, kConfig.model);
}

void TestProviderConfig::test_savedFileIsEnvCompatibleAndHasNoApiKey() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));

    QVERIFY(kFile.save({QStringLiteral("https://example.test/v1"), QStringLiteral("model-x")}).ok());
    const QString kText = readText(kFile.path());

    QVERIFY(kText.contains(QStringLiteral("POLZA_BASE_URL=https://example.test/v1")));
    QVERIFY(kText.contains(QStringLiteral("POLZA_MODEL=model-x")));
    // The file is user-readable and is never a secret store: the API key is not
    // persisted at all, and the header must not even name the variable.
    QVERIFY(!kText.contains(QStringLiteral("POLZA_API_KEY")));
}

void TestProviderConfig::test_saveCreatesTheMissingParentDirectory() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const QString kPath = QDir(kDirectory.path()).filePath(QStringLiteral("nested/config/provider.env"));
    const ProviderConfigFile kFile(kPath);

    QVERIFY(kFile.save({QStringLiteral("https://example.test/v1"), QStringLiteral("model-x")}).ok());

    QVERIFY(QFile::exists(kPath));
}

void TestProviderConfig::test_loadSkipsCommentsAndUnknownKeys() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));
    QVERIFY(writeText(kFile.path(),
                      QByteArrayLiteral("# comment\n"
                                        "\n"
                                        "SOME_OTHER_KEY=ignored\n"
                                        "POLZA_MODEL=from-file\n"
                                        "POLZA_BASE_URL=https://file.test/v1\n")));

    const ProviderConfigFile::LoadResult kLoaded = kFile.load();

    QVERIFY(kLoaded.ok());
    QCOMPARE(kLoaded.config.baseUrl, QStringLiteral("https://file.test/v1"));
    QCOMPARE(kLoaded.config.model, QStringLiteral("from-file"));
}

void TestProviderConfig::test_loadRejectsMalformedLine() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));
    QVERIFY(writeText(kFile.path(), QByteArrayLiteral("this line has no separator\n")));

    const ProviderConfigFile::LoadResult kLoaded = kFile.load();

    QVERIFY(!kLoaded.ok());
    QVERIFY(kLoaded.error.contains(QStringLiteral("line 1")));
}

void TestProviderConfig::test_saveRejectsInvalidConfig_data() {
    QTest::addColumn<QString>("baseUrl");
    QTest::addColumn<QString>("model");

    QTest::newRow("empty url") << QString() << QStringLiteral("model-x");
    QTest::newRow("blank url") << QStringLiteral("   ") << QStringLiteral("model-x");
    QTest::newRow("relative url") << QStringLiteral("example.test/v1") << QStringLiteral("model-x");
    QTest::newRow("non-http url") << QStringLiteral("ftp://example.test/v1") << QStringLiteral("model-x");
    QTest::newRow("empty model") << QStringLiteral("https://example.test/v1") << QString();
    QTest::newRow("blank model") << QStringLiteral("https://example.test/v1") << QStringLiteral("  ");
}

void TestProviderConfig::test_saveRejectsInvalidConfig() {
    QFETCH(QString, baseUrl);  // NOLINT(misc-const-correctness)
    QFETCH(QString, model);    // NOLINT(misc-const-correctness)

    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));

    const ProviderConfigFile::SaveResult kSaved = kFile.save({baseUrl, model});

    QVERIFY(!kSaved.ok());
    QVERIFY(!kSaved.error.isEmpty());
    QVERIFY(!QFile::exists(kFile.path()));
}

void TestProviderConfig::test_saveRejectsLineBreaks() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));

    QVERIFY(!kFile.save({QStringLiteral("https://example.test/v1"), QStringLiteral("bad\nmodel")}).ok());
    QVERIFY(!kFile.save({QStringLiteral("https://example.test/v1\ninjected"), QStringLiteral("model")}).ok());
    QVERIFY(!QFile::exists(kFile.path()));
}

void TestProviderConfig::test_emptyPathIsReportedHonestly() {
    const ProviderConfigFile kFile{QString()};

    QVERIFY(!kFile.load().ok());
    QVERIFY(!kFile.save({QStringLiteral("https://example.test/v1"), QStringLiteral("model")}).ok());
}

void TestProviderConfig::test_environmentOverridesFilePerKey() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));
    QVERIFY(kFile.save({QStringLiteral("https://file.test/v1"), QStringLiteral("file-model")}).ok());

    QProcessEnvironment environment = emptyEnvironment();
    environment.insert(QStringLiteral("POLZA_BASE_URL"), QStringLiteral("https://env.test/v1"));
    const ProviderConfigSource::Result kResolved = ProviderConfigSource(kFile, environment).resolve();

    QVERIFY(kResolved.ok());
    QCOMPARE(kResolved.config.baseUrl, QStringLiteral("https://env.test/v1"));
    QCOMPARE(kResolved.config.model, QStringLiteral("file-model"));
}

void TestProviderConfig::test_apiKeyComesOnlyFromTheEnvironment() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));
    QVERIFY(writeText(kFile.path(),
                      QByteArrayLiteral("POLZA_BASE_URL=https://file.test/v1\n"
                                        "POLZA_MODEL=file-model\n"
                                        "POLZA_API_KEY=key-in-file-must-be-ignored\n")));

    QProcessEnvironment environment = emptyEnvironment();
    environment.insert(QStringLiteral("POLZA_API_KEY"), QStringLiteral("session-key"));
    const ProviderConfigSource::Result kResolved = ProviderConfigSource(kFile, environment).resolve();

    QVERIFY(kResolved.ok());
    QCOMPARE(kResolved.apiKey, QStringLiteral("session-key"));
    QCOMPARE(kResolved.config.model, QStringLiteral("file-model"));
}

void TestProviderConfig::test_blankEnvironmentDoesNotOverrideTheFile() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));
    QVERIFY(kFile.save({QStringLiteral("https://file.test/v1"), QStringLiteral("file-model")}).ok());

    QProcessEnvironment environment = emptyEnvironment();
    environment.insert(QStringLiteral("POLZA_BASE_URL"), QStringLiteral("   "));
    environment.insert(QStringLiteral("POLZA_MODEL"), QString());
    const ProviderConfigSource::Result kResolved = ProviderConfigSource(kFile, environment).resolve();

    QVERIFY(kResolved.ok());
    QCOMPARE(kResolved.config.baseUrl, QStringLiteral("https://file.test/v1"));
    QCOMPARE(kResolved.config.model, QStringLiteral("file-model"));
}

void TestProviderConfig::test_sourceReportsFileErrors() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));
    QVERIFY(writeText(kFile.path(), QByteArrayLiteral("garbage\n")));

    const ProviderConfigSource::Result kResolved = ProviderConfigSource(kFile, emptyEnvironment()).resolve();

    QVERIFY(!kResolved.ok());
}

QTEST_GUILESS_MAIN(TestProviderConfig)

#include "tst_provider_config.moc"
