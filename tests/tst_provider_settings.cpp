#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
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

// Covers ProviderSettings: the mutable, QML-facing configuration object, its
// validation/persistence contract and the composition guarantee that a new chat
// is minted from the latest saved settings.
class TestProviderSettings : public QObject {
    Q_OBJECT

public:
    explicit TestProviderSettings(QObject* parent = nullptr);

private slots:
    void test_exposesInitialValues();
    void test_settersEmitSignalsAndExposeValues();
    void test_savePersistsUrlAndModelButNotTheKey();
    void test_saveRejectsInvalidInput_data();
    void test_saveRejectsInvalidInput();
    void test_editingClearsTheSaveError();
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
    QVERIFY(kSettings.lastError().isEmpty());
}

void TestProviderSettings::test_settersEmitSignalsAndExposeValues() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    ProviderSettings settings({}, {}, ProviderConfigFile(kDirectory.filePath(QStringLiteral("provider.env"))));
    const QSignalSpy kUrlSpy(&settings, &ProviderSettings::baseUrlChanged);
    const QSignalSpy kModelSpy(&settings, &ProviderSettings::modelChanged);
    const QSignalSpy kKeySpy(&settings, &ProviderSettings::apiKeyChanged);

    settings.setBaseUrl(QStringLiteral("https://changed.test/v1"));
    settings.setModel(QStringLiteral("changed-model"));
    settings.setApiKey(QStringLiteral("changed-key"));

    QCOMPARE(kUrlSpy.count(), 1);
    QCOMPARE(kModelSpy.count(), 1);
    QCOMPARE(kKeySpy.count(), 1);
    QCOMPARE(settings.baseUrl(), QStringLiteral("https://changed.test/v1"));
    QCOMPARE(settings.model(), QStringLiteral("changed-model"));
    QCOMPARE(settings.apiKey(), QStringLiteral("changed-key"));

    // Idempotent setters do not spam signals.
    settings.setBaseUrl(QStringLiteral("https://changed.test/v1"));
    QCOMPARE(kUrlSpy.count(), 1);
}

void TestProviderSettings::test_savePersistsUrlAndModelButNotTheKey() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));
    ProviderSettings settings({}, {}, kFile);
    const QSignalSpy kSavedSpy(&settings, &ProviderSettings::saved);

    settings.setBaseUrl(QStringLiteral("https://saved.test/v1"));
    settings.setModel(QStringLiteral("saved-model"));
    settings.setApiKey(QStringLiteral("super-secret"));

    QVERIFY(settings.save());

    QCOMPARE(kSavedSpy.count(), 1);
    QVERIFY(settings.lastError().isEmpty());
    const QString kText = readText(kFile.path());
    QVERIFY(kText.contains(QStringLiteral("POLZA_BASE_URL=https://saved.test/v1")));
    QVERIFY(kText.contains(QStringLiteral("POLZA_MODEL=saved-model")));
    QVERIFY(!kText.contains(QStringLiteral("super-secret")));
    QVERIFY(!kText.contains(QStringLiteral("POLZA_API_KEY")));
    // The key is still available in memory for the session.
    QCOMPARE(settings.apiKey(), QStringLiteral("super-secret"));
}

void TestProviderSettings::test_saveRejectsInvalidInput_data() {
    QTest::addColumn<QString>("baseUrl");
    QTest::addColumn<QString>("model");
    QTest::addColumn<QString>("apiKey");

    QTest::newRow("empty url") << QString() << QStringLiteral("model") << QStringLiteral("key");
    QTest::newRow("relative url") << QStringLiteral("example.test/v1") << QStringLiteral("model")
                                  << QStringLiteral("key");
    QTest::newRow("empty model") << QStringLiteral("https://example.test/v1") << QString() << QStringLiteral("key");
    QTest::newRow("empty key") << QStringLiteral("https://example.test/v1") << QStringLiteral("model") << QString();
    QTest::newRow("blank key") << QStringLiteral("https://example.test/v1") << QStringLiteral("model")
                               << QStringLiteral("   ");
}

void TestProviderSettings::test_saveRejectsInvalidInput() {
    QFETCH(QString, baseUrl);  // NOLINT(misc-const-correctness)
    QFETCH(QString, model);    // NOLINT(misc-const-correctness)
    QFETCH(QString, apiKey);   // NOLINT(misc-const-correctness)

    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));
    ProviderSettings settings({}, {}, kFile);
    settings.setBaseUrl(baseUrl);
    settings.setModel(model);
    settings.setApiKey(apiKey);

    QVERIFY(!settings.save());

    QVERIFY(!settings.lastError().isEmpty());
    QVERIFY(!QFile::exists(kFile.path()));
}

void TestProviderSettings::test_editingClearsTheSaveError() {
    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    ProviderSettings settings({}, {}, ProviderConfigFile(kDirectory.filePath(QStringLiteral("provider.env"))));
    const QSignalSpy kErrorSpy(&settings, &ProviderSettings::lastErrorChanged);

    QVERIFY(!settings.save());
    QVERIFY(!settings.lastError().isEmpty());
    const qsizetype kErrorsAfterFailure = kErrorSpy.count();

    settings.setBaseUrl(QStringLiteral("https://example.test/v1"));

    QVERIFY(settings.lastError().isEmpty());
    QCOMPARE(kErrorSpy.count(), kErrorsAfterFailure + 1);
}

void TestProviderSettings::test_newChatUsesSavedSettings() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket& socket, const QByteArray&) { streamText(socket, "hello"); });

    const QTemporaryDir kDirectory;
    QVERIFY(kDirectory.isValid());
    const ProviderConfigFile kFile(kDirectory.filePath(QStringLiteral("provider.env")));
    ProviderSettings settings({}, {}, kFile);
    settings.setBaseUrl(server.baseUrl());
    settings.setModel(QStringLiteral("entered-model"));
    settings.setApiKey(QStringLiteral("entered-key"));
    QVERIFY(settings.save());

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
    settings.setBaseUrl(server.baseUrl());
    settings.setModel(QStringLiteral("model-one"));
    settings.setApiKey(QStringLiteral("key"));
    QVERIFY(settings.save());

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
    // model - the recipe reads the settings at fresh() time, not at startup.
    settings.setModel(QStringLiteral("model-two"));
    QVERIFY(settings.save());
    Chat* second = newChat.open(QStringLiteral("openai-compatible"));
    QVERIFY(second != nullptr);
    QSignalSpy secondReplySpy(second, &Chat::replyReceived);
    second->send(QStringLiteral("two"));
    QVERIFY(secondReplySpy.wait(kReplyTimeout));
    QCOMPARE(QJsonDocument::fromJson(server.lastRequestBody()).object().value(QStringLiteral("model")).toString(),
             QStringLiteral("model-two"));
}

QTEST_GUILESS_MAIN(TestProviderSettings)

#include "tst_provider_settings.moc"
