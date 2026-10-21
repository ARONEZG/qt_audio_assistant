#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QString>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <chrono>
#include <memory>
#include <utility>

#include "OpenAiMockServer.h"
#include "app/OpenAiCompatibleProvider.h"
#include "domain/Chat.h"
#include "domain/Chats.h"
#include "domain/Message.h"
#include "domain/NewChat.h"
#include "domain/ProviderConfigSource.h"
#include "domain/ProviderOffer.h"
#include "domain/ProviderSettings.h"
#include "domain/Providers.h"

namespace {

constexpr auto kReplyTimeout = std::chrono::seconds{5};

// Minimal SSE scripting for the OpenAI-compatible wire protocol. The mock
// server is the protocol double (tests/OpenAiMockServer); these helpers only
// write the same bytes a streaming endpoint would.
QByteArray contentChunk(const QByteArray& content) {
    const QJsonObject kDelta{{QStringLiteral("content"), QString::fromUtf8(content)}};
    const QJsonObject kChoice{{QStringLiteral("index"), 0},
                              {QStringLiteral("delta"), kDelta},
                              {QStringLiteral("finish_reason"), QJsonValue::Null}};
    const QJsonObject kChunk{{QStringLiteral("id"), QStringLiteral("chatcmpl-test")},
                             {QStringLiteral("object"), QStringLiteral("chat.completion.chunk")},
                             {QStringLiteral("choices"), QJsonArray{kChoice}}};
    return QJsonDocument(kChunk).toJson(QJsonDocument::Compact);
}

QByteArray finishChunk() {
    const QJsonObject kChoice{{QStringLiteral("index"), 0},
                              {QStringLiteral("delta"), QJsonObject{}},
                              {QStringLiteral("finish_reason"), QStringLiteral("stop")}};
    const QJsonObject kChunk{{QStringLiteral("id"), QStringLiteral("chatcmpl-test")},
                             {QStringLiteral("object"), QStringLiteral("chat.completion.chunk")},
                             {QStringLiteral("choices"), QJsonArray{kChoice}}};
    return QJsonDocument(kChunk).toJson(QJsonDocument::Compact);
}

void writeSseEvent(QTcpSocket& socket, const QByteArray& data) {
    const QByteArray kPayload = "data: " + data + "\r\n\r\n";
    socket.write(QByteArray::number(kPayload.size(), 16).toUpper() + "\r\n" + kPayload + "\r\n");
}

void streamText(QTcpSocket& socket, const QByteArray& text) {
    socket.write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\n\r\n");
    writeSseEvent(socket, contentChunk(text));
    writeSseEvent(socket, finishChunk());
    writeSseEvent(socket, "[DONE]");
    socket.write("0\r\n\r\n");
    socket.disconnectFromHost();
}

[[nodiscard]] QString readText(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

}  // namespace

// Covers ProviderSettings: the mutable, QML-facing configuration object with a
// single save command, its validation/persistence contract and the composition
// guarantee that a new chat is minted from the latest saved settings.
class TestProviderSettings : public QObject {
    Q_OBJECT

public:
    explicit TestProviderSettings(QObject* parent = nullptr);

private slots:
    void test_exposesInitialValues();
    void test_saveReplacesTheConfigurationAsAWhole();
    void test_savePersistsUrlModelAndKey();
    void test_saveSucceedsWithoutAnApiKey();
    void test_emptyApiKeyKeepsTheExistingSessionKey();
    void test_savingWithABlankFieldPersistsTheEnvironmentKey();
    void test_saveRejectsInvalidInput_data();
    void test_saveRejectsInvalidInput();
    void test_clearErrorClearsTheSaveError();
    void test_newChatUsesSavedSettings();
    void test_eachChatIsMintedFromTheLatestSavedSettings();
};

TestProviderSettings::TestProviderSettings(QObject* parent) : QObject(parent) {
}

void TestProviderSettings::test_exposesInitialValues() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));

    const ProviderSettings kSettings({QStringLiteral("https://initial.test/v1"), QStringLiteral("initial-model")},
                                     QStringLiteral("initial-key"),
                                     kFile);

    QCOMPARE(kSettings.baseUrl(), QStringLiteral("https://initial.test/v1"));
    QCOMPARE(kSettings.model(), QStringLiteral("initial-model"));
    QCOMPARE(kSettings.apiKey(), QStringLiteral("initial-key"));
    QVERIFY(kSettings.isKeyConfigured());
    QVERIFY(kSettings.lastError().isEmpty());
}

void TestProviderSettings::test_saveReplacesTheConfigurationAsAWhole() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    ProviderSettings settings({}, {}, ProviderConfigFile(kDirectory.filePath(QStringLiteral("provider.env"))));
    const QSignalSpy kSavedSpy(&settings, &ProviderSettings::saved);
    const QSignalSpy kUrlSpy(&settings, &ProviderSettings::baseUrlChanged);
    const QSignalSpy kModelSpy(&settings, &ProviderSettings::modelChanged);
    const QSignalSpy kKeySpy(&settings, &ProviderSettings::isKeyConfiguredChanged);

    QVERIFY(settings.save(QStringLiteral("  https://changed.test/v1  "),
                          QStringLiteral("  changed-model  "),
                          QStringLiteral("  changed-key  ")));

    QCOMPARE(kSavedSpy.count(), 1);
    QCOMPARE(kUrlSpy.count(), 1);
    QCOMPARE(kModelSpy.count(), 1);
    QCOMPARE(kKeySpy.count(), 1);
    QCOMPARE(settings.baseUrl(), QStringLiteral("https://changed.test/v1"));
    QCOMPARE(settings.model(), QStringLiteral("changed-model"));
    QCOMPARE(settings.apiKey(), QStringLiteral("changed-key"));
    QVERIFY(settings.isKeyConfigured());

    // Saving the same values again is a no-op for the change notifications; only
    // the command completion is observable.
    QVERIFY(settings.save(QStringLiteral("https://changed.test/v1"), QStringLiteral("changed-model"), QString()));
    QCOMPARE(kUrlSpy.count(), 1);
    QCOMPARE(kModelSpy.count(), 1);
    QCOMPARE(kSavedSpy.count(), 2);
}

void TestProviderSettings::test_savePersistsUrlModelAndKey() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));
    ProviderSettings settings({}, {}, kFile);

    QVERIFY(settings.save(
            QStringLiteral("https://saved.test/v1"), QStringLiteral("saved-model"), QStringLiteral("super-secret")));

    QVERIFY(settings.lastError().isEmpty());
    const QString kText = readText(kFile.path());
    QVERIFY(kText.contains(QStringLiteral("POLZA_BASE_URL=https://saved.test/v1")));
    QVERIFY(kText.contains(QStringLiteral("POLZA_MODEL=saved-model")));
    QVERIFY(kText.contains(QStringLiteral("POLZA_API_KEY=super-secret")));
    // The key is also available in memory for the running session.
    QCOMPARE(settings.apiKey(), QStringLiteral("super-secret"));
    QVERIFY(settings.isKeyConfigured());
}

void TestProviderSettings::test_saveSucceedsWithoutAnApiKey() {
    // Saving the endpoint/model must not depend on a key: an empty key field
    // means "no key yet", and the provider reports a missing key honestly when
    // a chat is actually sent.
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));
    ProviderSettings settings({}, {}, kFile);

    QVERIFY(settings.save(QStringLiteral("https://no-key.test/v1"), QStringLiteral("no-key-model"), QString()));

    QVERIFY(settings.lastError().isEmpty());
    QVERIFY(!settings.isKeyConfigured());
    QVERIFY(QFile::exists(kFile.path()));
    const QString kText = readText(kFile.path());
    QVERIFY(kText.contains(QStringLiteral("POLZA_BASE_URL=https://no-key.test/v1")));
    QVERIFY(kText.contains(QStringLiteral("POLZA_MODEL=no-key-model")));
    // No key was provided, so the persisted key value is empty.
    QVERIFY(kText.contains(QStringLiteral("POLZA_API_KEY=\n")));
}

void TestProviderSettings::test_emptyApiKeyKeepsTheExistingSessionKey() {
    // The form never displays a stored key, so an empty key field means "keep
    // the key from the environment / previous save" rather than "erase it".
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));
    ProviderSettings settings({}, {}, kFile);
    QVERIFY(settings.save(
            QStringLiteral("https://one.test/v1"), QStringLiteral("model-one"), QStringLiteral("env-key")));

    QVERIFY(settings.save(QStringLiteral("https://two.test/v1"), QStringLiteral("model-two"), QStringLiteral("   ")));

    QCOMPARE(settings.apiKey(), QStringLiteral("env-key"));
    QVERIFY(settings.isKeyConfigured());
    QCOMPARE(settings.baseUrl(), QStringLiteral("https://two.test/v1"));
    QCOMPARE(settings.model(), QStringLiteral("model-two"));
    // The kept key is persisted again, not erased by the empty field.
    QVERIFY(readText(kFile.path()).contains(QStringLiteral("POLZA_API_KEY=env-key")));
}

void TestProviderSettings::test_savingWithABlankFieldPersistsTheEnvironmentKey() {
    // The settings object receives the startup resolution, where POLZA_API_KEY
    // may come from the environment. Saving the endpoint/model with an empty key
    // field keeps that key and writes it to provider.env, so an environment-only
    // secret becomes a persisted one. This is the documented behaviour.
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));

    QProcessEnvironment environment;
    environment.insert(QStringLiteral("POLZA_API_KEY"), QStringLiteral("env-only-key"));
    const ProviderConfigSource::Result kResolved = ProviderConfigSource(kFile, environment).resolve();
    QVERIFY(kResolved.ok());
    QCOMPARE(kResolved.apiKey, QStringLiteral("env-only-key"));

    ProviderSettings settings(kResolved.config, kResolved.apiKey, kFile);
    QVERIFY(settings.save(QStringLiteral("https://saved.test/v1"), QStringLiteral("saved-model"), QString()));

    QCOMPARE(settings.apiKey(), QStringLiteral("env-only-key"));
    QVERIFY(readText(kFile.path()).contains(QStringLiteral("POLZA_API_KEY=env-only-key")));
}

void TestProviderSettings::test_saveRejectsInvalidInput_data() {
    QTest::addColumn<QString>("baseUrl");
    QTest::addColumn<QString>("model");

    QTest::newRow("empty url") << QString() << QStringLiteral("model");
    QTest::newRow("relative url") << QStringLiteral("example.test/v1") << QStringLiteral("model");
    QTest::newRow("empty model") << QStringLiteral("https://example.test/v1") << QString();
}

void TestProviderSettings::test_saveRejectsInvalidInput() {
    QFETCH(QString, baseUrl);  // NOLINT(misc-const-correctness)
    QFETCH(QString, model);    // NOLINT(misc-const-correctness)

    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));
    ProviderSettings settings({}, {}, kFile);
    QVERIFY(settings.save(QStringLiteral("https://kept.test/v1"), QStringLiteral("kept-model"), QString()));
    const QString kPreviousText = readText(kFile.path());

    QVERIFY(!settings.save(baseUrl, model, QString()));

    // A rejected save is atomic: neither the stored value nor the file changes.
    QVERIFY(!settings.lastError().isEmpty());
    QCOMPARE(settings.baseUrl(), QStringLiteral("https://kept.test/v1"));
    QCOMPARE(settings.model(), QStringLiteral("kept-model"));
    QCOMPARE(readText(kFile.path()), kPreviousText);
}

void TestProviderSettings::test_clearErrorClearsTheSaveError() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    ProviderSettings settings({}, {}, ProviderConfigFile(kDirectory.filePath(QStringLiteral("provider.env"))));
    const QSignalSpy kErrorSpy(&settings, &ProviderSettings::lastErrorChanged);

    QVERIFY(!settings.save(QString(), QStringLiteral("model"), QString()));
    QVERIFY(!settings.lastError().isEmpty());

    settings.clearError();

    QVERIFY(settings.lastError().isEmpty());
    QCOMPARE(kErrorSpy.count(), 2);
}

void TestProviderSettings::test_newChatUsesSavedSettings() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket& socket, const QByteArray&) { streamText(socket, "hello"); });

    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));
    ProviderSettings settings({}, {}, kFile);
    QVERIFY(settings.save(server.baseUrl(), QStringLiteral("entered-model"), QStringLiteral("entered-key")));

    const Providers kProviders(
            {ProviderOffer(QStringLiteral("openai-compatible"), QStringLiteral("OpenAI-compatible"), [&settings] {
                return std::make_unique<OpenAiCompatibleProvider>(
                        settings.baseUrl(), settings.apiKey(), settings.model());
            })});
    Chats chats;
    NewChat newChat(chats, kProviders);

    Chat* chat = newChat.open(QStringLiteral("openai-compatible"));
    QVERIFY(chat != nullptr);
    QSignalSpy kReplySpy(chat, &Chat::replyReceived);
    chat->send(QStringLiteral("hi"));
    QVERIFY(kReplySpy.wait(kReplyTimeout));

    // The request reached the endpoint the user entered and carried the entered
    // model, so both persisted values reached the minted provider.
    QCOMPARE(server.requestCount(), 1);
    const QJsonObject kPayload = QJsonDocument::fromJson(server.lastRequestBody()).object();
    QCOMPARE(kPayload.value(QStringLiteral("model")).toString(), QStringLiteral("entered-model"));
    QCOMPARE(chat->messages().size(), std::size_t{2});
}

void TestProviderSettings::test_eachChatIsMintedFromTheLatestSavedSettings() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket& socket, const QByteArray&) { streamText(socket, "ok"); });

    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));
    ProviderSettings settings({}, {}, kFile);
    QVERIFY(settings.save(server.baseUrl(), QStringLiteral("model-one"), QStringLiteral("key")));

    const Providers kProviders(
            {ProviderOffer(QStringLiteral("openai-compatible"), QStringLiteral("OpenAI-compatible"), [&settings] {
                return std::make_unique<OpenAiCompatibleProvider>(
                        settings.baseUrl(), settings.apiKey(), settings.model());
            })});
    Chats chats;
    NewChat newChat(chats, kProviders);

    Chat* first = newChat.open(QStringLiteral("openai-compatible"));
    QVERIFY(first != nullptr);
    QSignalSpy firstReplySpy(first, &Chat::replyReceived);
    first->send(QStringLiteral("one"));
    QVERIFY(firstReplySpy.wait(kReplyTimeout));
    QCOMPARE(QJsonDocument::fromJson(server.lastRequestBody()).object().value(QStringLiteral("model")).toString(),
             QStringLiteral("model-one"));

    // A new chat is minted after the settings changed, so it must use the new
    // model - the recipe reads the settings at fresh() time, not at startup. The
    // empty key means the current key is kept.
    QVERIFY(settings.save(server.baseUrl(), QStringLiteral("model-two"), QString()));
    Chat* second = newChat.open(QStringLiteral("openai-compatible"));
    QVERIFY(second != nullptr);
    QSignalSpy secondReplySpy(second, &Chat::replyReceived);
    second->send(QStringLiteral("two"));
    QVERIFY(secondReplySpy.wait(kReplyTimeout));
    QCOMPARE(QJsonDocument::fromJson(server.lastRequestBody()).object().value(QStringLiteral("model")).toString(),
             QStringLiteral("model-two"));
    QCOMPARE(settings.apiKey(), QStringLiteral("key"));
}

QTEST_GUILESS_MAIN(TestProviderSettings)

#include "tst_provider_settings.moc"
