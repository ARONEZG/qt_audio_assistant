#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QString>
#include <QTcpSocket>
#include <QtTest>
#include <chrono>
#include <memory>
#include <vector>

#include "OpenAiMockServer.h"
#include "app/OpenAiCompatibleProvider.h"
#include "domain/Answer.h"
#include "domain/Message.h"
#include "domain/ProviderError.h"

namespace {

constexpr auto kReplyTimeout = std::chrono::seconds{5};

// One OpenAI streaming chunk carrying a text delta.
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

// The final streaming chunk (no delta, a finish reason).
QByteArray finishChunk() {
    const QJsonObject kChoice{{QStringLiteral("index"), 0},
                              {QStringLiteral("delta"), QJsonObject{}},
                              {QStringLiteral("finish_reason"), QStringLiteral("stop")}};
    const QJsonObject kChunk{{QStringLiteral("id"), QStringLiteral("chatcmpl-test")},
                             {QStringLiteral("object"), QStringLiteral("chat.completion.chunk")},
                             {QStringLiteral("choices"), QJsonArray{kChoice}}};
    return QJsonDocument(kChunk).toJson(QJsonDocument::Compact);
}

void writeChunkedHeaders(QTcpSocket& socket, int status = 200, const QByteArray& reason = "OK") {
    socket.write("HTTP/1.1 " + QByteArray::number(status) + " " + reason +
                 "\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\n\r\n");
}

void writeSseEvent(QTcpSocket& socket, const QByteArray& data) {
    const QByteArray kPayload = "data: " + data + "\r\n\r\n";
    socket.write(QByteArray::number(kPayload.size(), 16).toUpper() + "\r\n" + kPayload + "\r\n");
}

void endChunked(QTcpSocket& socket) {
    socket.write("0\r\n\r\n");
    socket.disconnectFromHost();
}

// A 200 response that promises more bytes than it sends and then closes. With an
// unsatisfied Content-Length the truncation is a protocol error, so the client
// deterministically reports a transport failure - unlike a chunked body cut
// short, whose handling depends on timing.
void writeTruncatedSseResponse(QTcpSocket& socket, const QByteArray& data) {
    const QByteArray kEvent = "data: " + data + "\r\n\r\n";
    socket.write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: " +
                 QByteArray::number(kEvent.size() + 64) + "\r\n\r\n" + kEvent);
    socket.disconnectFromHost();
}

void writeHttpError(QTcpSocket& socket, int status, const QByteArray& reason, const QByteArray& message) {
    const QJsonObject kError{{QStringLiteral("message"), QString::fromUtf8(message)},
                             {QStringLiteral("type"), QStringLiteral("invalid_request_error")},
                             {QStringLiteral("code"), QStringLiteral("invalid_api_key")}};
    const QByteArray kBody =
            QJsonDocument(QJsonObject{{QStringLiteral("error"), kError}}).toJson(QJsonDocument::Compact);
    socket.write("HTTP/1.1 " + QByteArray::number(status) + " " + reason +
                 "\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(kBody.size()) +
                 "\r\nConnection: close\r\n\r\n" + kBody);
    socket.disconnectFromHost();
}

std::unique_ptr<OpenAiCompatibleProvider> makeProvider(const OpenAiMockServer& server) {
    return std::make_unique<OpenAiCompatibleProvider>(
            server.baseUrl(), QStringLiteral("test-key"), QStringLiteral("test-model"));
}

std::vector<Message> userHistory() {
    return {Message(MessageRole::kUser, QStringLiteral("hi"))};
}

}  // namespace

// The network provider is exercised end to end against a local OpenAI-compatible
// mock server: the real LLMQore client, the real adapter and the real Answer.
// No external network, no sleep - every wait is a QSignalSpy on a signal.
class TestOpenAiProvider : public QObject {
    Q_OBJECT

public:
    explicit TestOpenAiProvider(QObject* parent = nullptr);

private slots:
    void test_exposesIdentity();
    void test_streamsChunksAndCompletes();
    void test_mapsHistoryAndModelIntoTheRequest();
    void test_httpStatusIsClassified_data();
    void test_httpStatusIsClassified();
    void test_malformedFrameIsIgnoredAndStreamContinues();
    void test_interruptedStreamFails();
    void test_cancelAbortsTheNetworkRequest();
    void test_cancelThenRespondStillWorks();
    void test_timeoutFailsTheAnswer();
    void test_missingApiKeyFailsWithoutNetwork();
    void test_missingEndpointFailsWithoutNetwork();
};

TestOpenAiProvider::TestOpenAiProvider(QObject* parent) : QObject(parent) {
}

void TestOpenAiProvider::test_exposesIdentity() {
    const OpenAiCompatibleProvider kProvider(
            QStringLiteral("http://127.0.0.1:1/v1"), QStringLiteral("key"), QStringLiteral("model"));

    QCOMPARE(kProvider.id(), QStringLiteral("openai-compatible"));
    QCOMPARE(kProvider.displayName(), QStringLiteral("OpenAI-compatible"));
}

void TestOpenAiProvider::test_streamsChunksAndCompletes() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket& socket, const QByteArray&) {
        writeChunkedHeaders(socket);
        writeSseEvent(socket, contentChunk("Hel"));
        writeSseEvent(socket, contentChunk("lo"));
        writeSseEvent(socket, finishChunk());
        writeSseEvent(socket, "[DONE]");
        endChunked(socket);
    });
    const auto kProvider = makeProvider(server);

    Answer* answer = kProvider->respond(userHistory());
    QVERIFY(answer != nullptr);
    // respond() returns before any byte is exchanged.
    QCOMPARE(answer->isCompleted(), false);
    QCOMPARE(answer->isFailed(), false);

    const QSignalSpy kChunkSpy(answer, &Answer::chunkReceived);
    QSignalSpy kCompletedSpy(answer, &Answer::completed);
    QVERIFY(kCompletedSpy.wait(kReplyTimeout));

    QCOMPARE(answer->isCompleted(), true);
    QCOMPARE(answer->isFailed(), false);
    QCOMPARE(answer->text(), QStringLiteral("Hello"));
    QCOMPARE(kChunkSpy.count(), 2);
    QCOMPARE(server.requestCount(), 1);
}

void TestOpenAiProvider::test_mapsHistoryAndModelIntoTheRequest() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket& socket, const QByteArray&) {
        writeChunkedHeaders(socket);
        writeSseEvent(socket, contentChunk("ok"));
        writeSseEvent(socket, finishChunk());
        endChunked(socket);
    });
    const auto kProvider = makeProvider(server);

    const std::vector<Message> kHistory{
            Message(MessageRole::kSystem, QStringLiteral("be nice")),
            Message(MessageRole::kUser, QStringLiteral("hello")),
            Message(MessageRole::kAssistant, QStringLiteral("hi")),
            Message(MessageRole::kUser, QStringLiteral("again")),
    };
    Answer* answer = kProvider->respond(kHistory);
    QSignalSpy kCompletedSpy(answer, &Answer::completed);
    QVERIFY(kCompletedSpy.wait(kReplyTimeout));

    const QJsonObject kPayload = QJsonDocument::fromJson(server.lastRequestBody()).object();
    QCOMPARE(kPayload.value(QStringLiteral("model")).toString(), QStringLiteral("test-model"));
    const QJsonArray kMessages = kPayload.value(QStringLiteral("messages")).toArray();
    QCOMPARE(kMessages.size(), 4);
    QCOMPARE(kMessages.at(0).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("system"));
    QCOMPARE(kMessages.at(0).toObject().value(QStringLiteral("content")).toString(), QStringLiteral("be nice"));
    QCOMPARE(kMessages.at(1).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    QCOMPARE(kMessages.at(3).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    QCOMPARE(kMessages.at(3).toObject().value(QStringLiteral("content")).toString(), QStringLiteral("again"));
}

void TestOpenAiProvider::test_httpStatusIsClassified_data() {
    QTest::addColumn<int>("status");
    QTest::addColumn<ProviderError>("expected");

    QTest::newRow("401 unauthorized") << 401 << ProviderError::kAuthentication;
    QTest::newRow("403 forbidden") << 403 << ProviderError::kAuthentication;
    QTest::newRow("429 rate limit") << 429 << ProviderError::kRateLimit;
    QTest::newRow("400 bad request") << 400 << ProviderError::kInvalidResponse;
    QTest::newRow("422 unprocessable") << 422 << ProviderError::kInvalidResponse;
    QTest::newRow("404 not found") << 404 << ProviderError::kNetwork;
    QTest::newRow("500 server error") << 500 << ProviderError::kNetwork;
}

void TestOpenAiProvider::test_httpStatusIsClassified() {
    // QFETCH cannot declare a const variable; clang-tidy's const-correctness
    // suggestion is therefore not applicable here.
    QFETCH(int, status);              // NOLINT(misc-const-correctness)
    QFETCH(ProviderError, expected);  // NOLINT(misc-const-correctness)

    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([status](QTcpSocket& socket, const QByteArray&) {
        writeHttpError(socket, status, "Error", "server rejected the request");
    });
    const auto kProvider = makeProvider(server);

    Answer* answer = kProvider->respond(userHistory());
    QSignalSpy kFailedSpy(answer, &Answer::failed);
    QVERIFY(kFailedSpy.wait(kReplyTimeout));

    QCOMPARE(answer->isFailed(), true);
    QCOMPARE(answer->error(), expected);
    QVERIFY(answer->errorText().contains(QString::number(status)));
}

void TestOpenAiProvider::test_malformedFrameIsIgnoredAndStreamContinues() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket& socket, const QByteArray&) {
        writeChunkedHeaders(socket);
        writeSseEvent(socket, "{ this is not json");
        writeSseEvent(socket, contentChunk("survived"));
        writeSseEvent(socket, finishChunk());
        endChunked(socket);
    });
    const auto kProvider = makeProvider(server);

    Answer* answer = kProvider->respond(userHistory());
    QSignalSpy kCompletedSpy(answer, &Answer::completed);
    QVERIFY(kCompletedSpy.wait(kReplyTimeout));

    QCOMPARE(answer->isCompleted(), true);
    QCOMPARE(answer->text(), QStringLiteral("survived"));
}

void TestOpenAiProvider::test_interruptedStreamFails() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket& socket, const QByteArray&) {
        // Stream one frame, then close before the promised Content-Length is
        // satisfied: the response is incomplete and must surface as a transport
        // failure.
        writeTruncatedSseResponse(socket, contentChunk("par"));
    });
    const auto kProvider = makeProvider(server);

    Answer* answer = kProvider->respond(userHistory());
    QSignalSpy kFailedSpy(answer, &Answer::failed);
    QVERIFY(kFailedSpy.wait(kReplyTimeout));

    QCOMPARE(answer->isFailed(), true);
    QCOMPARE(answer->error(), ProviderError::kNetwork);
}

void TestOpenAiProvider::test_cancelAbortsTheNetworkRequest() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket& socket, const QByteArray&) {
        writeChunkedHeaders(socket);
        writeSseEvent(socket, contentChunk("par"));
        // Keep the connection open: only a client-side abort can end it.
    });
    const auto kProvider = makeProvider(server);

    Answer* answer = kProvider->respond(userHistory());
    QSignalSpy kChunkSpy(answer, &Answer::chunkReceived);
    QVERIFY(kChunkSpy.wait(kReplyTimeout));
    const QSignalSpy kCancelledSpy(answer, &Answer::cancelled);
    const QSignalSpy kFailedSpy(answer, &Answer::failed);
    const QSignalSpy kDisconnectedSpy(&server, &OpenAiMockServer::clientDisconnected);
    const QSignalSpy kDestroyedSpy(answer, &QObject::destroyed);

    answer->cancel();

    QCOMPARE(kCancelledSpy.count(), 1);
    QCOMPARE(answer->isCancelled(), true);
    // Wait for the whole cancellation to settle: the provider disposes of the
    // answer (and its request entry), and the abort reaches the wire (the server
    // observes the connection drop).
    QVERIFY(QTest::qWaitFor([&] { return kDestroyedSpy.count() > 0 && kDisconnectedSpy.count() > 0; }, kReplyTimeout));
    QCOMPARE(kFailedSpy.count(), 0);
}

void TestOpenAiProvider::test_cancelThenRespondStillWorks() {
    int requests = 0;
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([&requests](QTcpSocket& socket, const QByteArray&) {
        ++requests;
        writeChunkedHeaders(socket);
        if (requests == 1) {
            // Keep the first stream open so the test can cancel it mid-flight.
            writeSseEvent(socket, contentChunk("par"));
            return;
        }
        writeSseEvent(socket, contentChunk("ok"));
        writeSseEvent(socket, finishChunk());
        endChunked(socket);
    });
    const auto kProvider = makeProvider(server);

    Answer* first = kProvider->respond(userHistory());
    QSignalSpy kFirstChunkSpy(first, &Answer::chunkReceived);
    QVERIFY(kFirstChunkSpy.wait(kReplyTimeout));
    QSignalSpy kFirstDestroyedSpy(first, &QObject::destroyed);
    first->cancel();
    QVERIFY(kFirstDestroyedSpy.wait(kReplyTimeout));

    // The provider stays usable after a cancellation: a fresh request streams
    // and completes normally.
    Answer* second = kProvider->respond(userHistory());
    QSignalSpy kCompletedSpy(second, &Answer::completed);
    QVERIFY(kCompletedSpy.wait(kReplyTimeout));
    QCOMPARE(second->text(), QStringLiteral("ok"));
    QCOMPARE(server.requestCount(), 2);
}

void TestOpenAiProvider::test_timeoutFailsTheAnswer() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket&, const QByteArray&) {
        // Accept the request but never respond, forcing the transfer timeout.
    });
    OpenAiCompatibleProvider provider(server.baseUrl(), QStringLiteral("test-key"), QStringLiteral("test-model"), 300);

    Answer* answer = provider.respond(userHistory());
    QSignalSpy kFailedSpy(answer, &Answer::failed);
    QVERIFY(kFailedSpy.wait(kReplyTimeout));

    QCOMPARE(answer->isFailed(), true);
    QCOMPARE(answer->error(), ProviderError::kNetwork);
}

void TestOpenAiProvider::test_missingApiKeyFailsWithoutNetwork() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    OpenAiCompatibleProvider provider(server.baseUrl(), QString(), QStringLiteral("model"));

    Answer* answer = provider.respond(userHistory());
    QSignalSpy kFailedSpy(answer, &Answer::failed);
    QVERIFY(kFailedSpy.wait(kReplyTimeout));

    QCOMPARE(answer->error(), ProviderError::kAuthentication);
    QVERIFY(answer->errorText().contains(QStringLiteral("POLZA_API_KEY")));
    QCOMPARE(server.requestCount(), 0);
}

void TestOpenAiProvider::test_missingEndpointFailsWithoutNetwork() {
    OpenAiCompatibleProvider provider(QString(), QStringLiteral("key"), QString());

    Answer* answer = provider.respond(userHistory());
    QSignalSpy kFailedSpy(answer, &Answer::failed);
    QVERIFY(kFailedSpy.wait(kReplyTimeout));

    QCOMPARE(answer->error(), ProviderError::kUnknown);
}

QTEST_GUILESS_MAIN(TestOpenAiProvider)

#include "tst_openai_provider.moc"
