// Tests for the OpenAI-compatible transcription adapter.
//
// The adapter is exercised end to end - real WAV encoding, real base64, real
// JSON, the real Qt Network client, the real local mock HTTP server - with no
// external network and no sleep: every wait is a QSignalSpy or a bounded
// QTest::qWaitFor on an observable condition.
//
// The mock duplicates exactly the wire shape the adapter produces: a JSON POST
// whose `file` is the documented data-URL form (`data:audio/wav;base64,…`), so
// test_sendsCanonicalRequestAndEmitsText fails if the adapter and the mock ever
// drift apart.
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QString>
#include <QTcpSocket>
#include <QtTest>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "OpenAiMockServer.h"
#include "app/OpenAiCompatibleTranscriber.h"
#include "audio/AudioFormat.h"
#include "audio/WavEncoder.h"
#include "domain/ProviderError.h"
#include "domain/TranscriptionProvider.h"

namespace {

constexpr auto kReplyTimeout = std::chrono::seconds{5};
constexpr auto kWavDataUrlPrefix = QLatin1String("data:audio/wav;base64,");

// Writes one complete JSON HTTP response and closes the connection.
void writeJsonResponse(QTcpSocket& socket, int status, const QByteArray& body) {
    socket.write("HTTP/1.1 " + QByteArray::number(status) +
                 " OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) +
                 "\r\nConnection: close\r\n\r\n" + body);
    socket.disconnectFromHost();
}

audio::AudioFormat canonicalFormat() {
    return {16000U, 1U, audio::SampleFormat::kFloat32};
}

}  // namespace

class TestTranscriber : public QObject {
    Q_OBJECT

public:
    explicit TestTranscriber(QObject* parent = nullptr);

private slots:
    void test_sendsCanonicalRequestAndEmitsText();
    void test_honoursExplicitModel();
    void test_normalisesTrailingSlashInBaseUrl();
    void test_httpStatusIsClassified_data();
    void test_httpStatusIsClassified();
    void test_transportFailureFails();
    void test_transferTimeoutFails();
    void test_malformedJsonFails();
    void test_missingTextFieldFails();
    void test_nonObjectJsonFails();
    void test_cancelsInFlightRequest();
    void test_cancelWithoutRequestIsNoOp();
    void test_destructionAbortsInFlightRequest();
    void test_newerRequestSupersedesTheActiveOne();
    void test_missingApiKeyFailsWithoutNetwork();
    void test_missingEndpointFailsWithoutNetwork();
};

TestTranscriber::TestTranscriber(QObject* parent) : QObject(parent) {
}

void TestTranscriber::test_sendsCanonicalRequestAndEmitsText() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket& socket, const QByteArray&) {
        writeJsonResponse(socket, 200, QByteArrayLiteral(R"({"text":"привет мир"})"));
    });
    OpenAiCompatibleTranscriber transcriber(server.baseUrl(), QStringLiteral("test-key"));

    QSignalSpy kTranscribedSpy(&transcriber, &TranscriptionProvider::transcribed);
    const QSignalSpy kFailedSpy(&transcriber, &TranscriptionProvider::failed);
    const std::vector<float> kSamples{0.0F, 0.5F, -0.5F};
    transcriber.transcribe(kSamples);

    QVERIFY(kTranscribedSpy.wait(kReplyTimeout));
    QCOMPARE(kTranscribedSpy.count(), 1);
    QCOMPARE(kTranscribedSpy.at(0).at(0).toString(), QStringLiteral("привет мир"));
    QCOMPARE(kFailedSpy.count(), 0);
    QCOMPARE(server.requestCount(), 1);

    QCOMPARE(server.lastRequestTarget(), QByteArrayLiteral("/v1/audio/transcriptions"));
    QCOMPARE(server.lastRequestHeader("Authorization"), QByteArrayLiteral("Bearer test-key"));
    QVERIFY(server.lastRequestHeader("Content-Type").startsWith(QByteArrayLiteral("application/json")));

    const QJsonObject kPayload = QJsonDocument::fromJson(server.lastRequestBody()).object();
    QCOMPARE(kPayload.value(QStringLiteral("model")).toString(), QStringLiteral("openai/whisper-1"));
    QCOMPARE(kPayload.value(QStringLiteral("response_format")).toString(), QStringLiteral("json"));

    // The wire `file` must be the exact base64 data-URL of the canonical WAV.
    const QString kFile = kPayload.value(QStringLiteral("file")).toString();
    QVERIFY(kFile.startsWith(kWavDataUrlPrefix));
    const QByteArray kDecoded = QByteArray::fromBase64(kFile.mid(kWavDataUrlPrefix.size()).toLatin1());
    const std::vector<std::uint8_t> kExpected = audio::encodeWavPcm16(canonicalFormat(), kSamples);
    QCOMPARE(kDecoded,
             QByteArray(reinterpret_cast<const char*>(kExpected.data()), static_cast<qsizetype>(kExpected.size())));
}

void TestTranscriber::test_honoursExplicitModel() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket& socket, const QByteArray&) {
        writeJsonResponse(socket, 200, QByteArrayLiteral(R"({"text":"ok"})"));
    });
    OpenAiCompatibleTranscriber transcriber(
            server.baseUrl(), QStringLiteral("k"), QStringLiteral("openai/whisper-large-v3"));

    QSignalSpy kTranscribedSpy(&transcriber, &TranscriptionProvider::transcribed);
    transcriber.transcribe({0.0F});
    QVERIFY(kTranscribedSpy.wait(kReplyTimeout));

    const QJsonObject kPayload = QJsonDocument::fromJson(server.lastRequestBody()).object();
    QCOMPARE(kPayload.value(QStringLiteral("model")).toString(), QStringLiteral("openai/whisper-large-v3"));
}

void TestTranscriber::test_normalisesTrailingSlashInBaseUrl() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket& socket, const QByteArray&) {
        writeJsonResponse(socket, 200, QByteArrayLiteral(R"({"text":"ok"})"));
    });
    OpenAiCompatibleTranscriber transcriber(server.baseUrl() + QStringLiteral("/"), QStringLiteral("k"));

    QSignalSpy kTranscribedSpy(&transcriber, &TranscriptionProvider::transcribed);
    transcriber.transcribe({0.0F});
    QVERIFY(kTranscribedSpy.wait(kReplyTimeout));

    // A trailing slash must not produce a double slash before the path.
    QCOMPARE(server.lastRequestTarget(), QByteArrayLiteral("/v1/audio/transcriptions"));
}

void TestTranscriber::test_httpStatusIsClassified_data() {
    QTest::addColumn<int>("status");
    QTest::addColumn<ProviderError>("expected");

    QTest::newRow("401 unauthorized") << 401 << ProviderError::kAuthentication;
    QTest::newRow("429 rate limit") << 429 << ProviderError::kRateLimit;
    QTest::newRow("400 bad request") << 400 << ProviderError::kInvalidResponse;
    QTest::newRow("403 forbidden") << 403 << ProviderError::kAuthentication;
    QTest::newRow("413 too large") << 413 << ProviderError::kInvalidResponse;
    QTest::newRow("422 unprocessable") << 422 << ProviderError::kInvalidResponse;
    QTest::newRow("500 server error") << 500 << ProviderError::kNetwork;
}

void TestTranscriber::test_httpStatusIsClassified() {
    QFETCH(int, status);              // NOLINT(misc-const-correctness) - QFETCH cannot be const
    QFETCH(ProviderError, expected);  // NOLINT(misc-const-correctness) - QFETCH cannot be const

    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([status](QTcpSocket& socket, const QByteArray&) {
        writeJsonResponse(socket, status, QByteArrayLiteral(R"({"error":{"message":"nope"}})"));
    });
    OpenAiCompatibleTranscriber transcriber(server.baseUrl(), QStringLiteral("k"));

    QSignalSpy kFailedSpy(&transcriber, &TranscriptionProvider::failed);
    transcriber.transcribe({0.0F});
    QVERIFY(kFailedSpy.wait(kReplyTimeout));

    QCOMPARE(kFailedSpy.at(0).at(0).value<ProviderError>(), expected);
    QVERIFY(kFailedSpy.at(0).at(1).toString().contains(QString::number(status)));
}

void TestTranscriber::test_transportFailureFails() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket& socket, const QByteArray&) {
        // Close before sending any response: an unset status, a transport error.
        socket.disconnectFromHost();
    });
    OpenAiCompatibleTranscriber transcriber(server.baseUrl(), QStringLiteral("k"));

    QSignalSpy kFailedSpy(&transcriber, &TranscriptionProvider::failed);
    transcriber.transcribe({0.0F});
    QVERIFY(kFailedSpy.wait(kReplyTimeout));

    QCOMPARE(kFailedSpy.at(0).at(0).value<ProviderError>(), ProviderError::kNetwork);
}

void TestTranscriber::test_transferTimeoutFails() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket&, const QByteArray&) {
        // Accept the request but never respond, forcing the transfer timeout.
    });
    OpenAiCompatibleTranscriber transcriber(
            server.baseUrl(), QStringLiteral("k"), QStringLiteral("openai/whisper-1"), 300);

    QSignalSpy kFailedSpy(&transcriber, &TranscriptionProvider::failed);
    transcriber.transcribe({0.0F});
    QVERIFY(kFailedSpy.wait(kReplyTimeout));

    QCOMPARE(kFailedSpy.at(0).at(0).value<ProviderError>(), ProviderError::kNetwork);
}

void TestTranscriber::test_malformedJsonFails() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket& socket, const QByteArray&) {
        writeJsonResponse(socket, 200, QByteArrayLiteral("{ this is not json"));
    });
    OpenAiCompatibleTranscriber transcriber(server.baseUrl(), QStringLiteral("k"));

    QSignalSpy kFailedSpy(&transcriber, &TranscriptionProvider::failed);
    transcriber.transcribe({0.0F});
    QVERIFY(kFailedSpy.wait(kReplyTimeout));

    QCOMPARE(kFailedSpy.at(0).at(0).value<ProviderError>(), ProviderError::kInvalidResponse);
}

void TestTranscriber::test_missingTextFieldFails() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket& socket, const QByteArray&) {
        writeJsonResponse(socket, 200, QByteArrayLiteral(R"({"language":"ru"})"));
    });
    OpenAiCompatibleTranscriber transcriber(server.baseUrl(), QStringLiteral("k"));

    QSignalSpy kFailedSpy(&transcriber, &TranscriptionProvider::failed);
    transcriber.transcribe({0.0F});
    QVERIFY(kFailedSpy.wait(kReplyTimeout));

    QCOMPARE(kFailedSpy.at(0).at(0).value<ProviderError>(), ProviderError::kInvalidResponse);
}

void TestTranscriber::test_nonObjectJsonFails() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket& socket, const QByteArray&) {
        // Valid JSON, but not an object: the response contract is an object.
        writeJsonResponse(socket, 200, QByteArrayLiteral("[]"));
    });
    OpenAiCompatibleTranscriber transcriber(server.baseUrl(), QStringLiteral("k"));

    QSignalSpy kFailedSpy(&transcriber, &TranscriptionProvider::failed);
    transcriber.transcribe({0.0F});
    QVERIFY(kFailedSpy.wait(kReplyTimeout));

    QCOMPARE(kFailedSpy.at(0).at(0).value<ProviderError>(), ProviderError::kInvalidResponse);
}

void TestTranscriber::test_cancelsInFlightRequest() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket&, const QByteArray&) {
        // Accept the request but never answer: only a client abort can end it.
    });
    OpenAiCompatibleTranscriber transcriber(server.baseUrl(), QStringLiteral("k"));

    const QSignalSpy kFailedSpy(&transcriber, &TranscriptionProvider::failed);
    const QSignalSpy kDisconnectedSpy(&server, &OpenAiMockServer::clientDisconnected);

    transcriber.transcribe({0.0F});
    QVERIFY(QTest::qWaitFor([&] { return server.requestCount() == 1; }, kReplyTimeout));

    transcriber.cancel();

    QCOMPARE(kFailedSpy.count(), 1);
    QCOMPARE(kFailedSpy.at(0).at(0).value<ProviderError>(), ProviderError::kCancelled);
    QVERIFY(QTest::qWaitFor([&] { return kDisconnectedSpy.count() > 0; }, kReplyTimeout));
}

void TestTranscriber::test_cancelWithoutRequestIsNoOp() {
    OpenAiCompatibleTranscriber transcriber(QStringLiteral("http://127.0.0.1:1/v1"), QStringLiteral("k"));
    const QSignalSpy kFailedSpy(&transcriber, &TranscriptionProvider::failed);

    transcriber.cancel();

    QCOMPARE(kFailedSpy.count(), 0);
}

void TestTranscriber::test_destructionAbortsInFlightRequest() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    server.setHandler([](QTcpSocket&, const QByteArray&) {
        // Keep the request open so destruction happens mid-flight.
    });
    const QSignalSpy kDisconnectedSpy(&server, &OpenAiMockServer::clientDisconnected);
    auto transcriber = std::make_unique<OpenAiCompatibleTranscriber>(server.baseUrl(), QStringLiteral("k"));
    transcriber->transcribe({0.0F});
    QVERIFY(QTest::qWaitFor([&] { return server.requestCount() == 1; }, kReplyTimeout));

    transcriber.reset();

    // The destructor aborts the request: the server observes the connection
    // closing, and no signal is delivered for the abandoned request.
    QVERIFY(QTest::qWaitFor([&] { return kDisconnectedSpy.count() > 0; }, kReplyTimeout));
}

void TestTranscriber::test_newerRequestSupersedesTheActiveOne() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    int requests = 0;
    server.setHandler([&requests](QTcpSocket& socket, const QByteArray&) {
        ++requests;
        if (requests == 1) {
            return;  // keep the first request open so it can be superseded
        }
        writeJsonResponse(socket, 200, QByteArrayLiteral(R"({"text":"second"})"));
    });
    OpenAiCompatibleTranscriber transcriber(server.baseUrl(), QStringLiteral("k"));

    QSignalSpy kFailedSpy(&transcriber, &TranscriptionProvider::failed);
    QSignalSpy kTranscribedSpy(&transcriber, &TranscriptionProvider::transcribed);

    transcriber.transcribe({0.0F});
    QVERIFY(QTest::qWaitFor([&] { return server.requestCount() == 1; }, kReplyTimeout));
    transcriber.transcribe({0.1F});

    // The superseded failure is delivered from the event loop, never inside the
    // new transcribe() call (the TranscriptionProvider contract).
    QCOMPARE(kFailedSpy.count(), 0);
    QVERIFY(kFailedSpy.wait(kReplyTimeout));
    QCOMPARE(kFailedSpy.count(), 1);
    QCOMPARE(kFailedSpy.at(0).at(0).value<ProviderError>(), ProviderError::kCancelled);
    QVERIFY(kTranscribedSpy.wait(kReplyTimeout));
    QCOMPARE(kTranscribedSpy.at(0).at(0).toString(), QStringLiteral("second"));
    QCOMPARE(server.requestCount(), 2);
}

void TestTranscriber::test_missingApiKeyFailsWithoutNetwork() {
    OpenAiMockServer server;
    QVERIFY(server.listen());
    OpenAiCompatibleTranscriber transcriber(server.baseUrl(), QString());

    QSignalSpy kFailedSpy(&transcriber, &TranscriptionProvider::failed);
    transcriber.transcribe({0.0F});
    QVERIFY(kFailedSpy.wait(kReplyTimeout));

    QCOMPARE(kFailedSpy.at(0).at(0).value<ProviderError>(), ProviderError::kAuthentication);
    QCOMPARE(server.requestCount(), 0);
}

void TestTranscriber::test_missingEndpointFailsWithoutNetwork() {
    OpenAiCompatibleTranscriber transcriber(QString(), QStringLiteral("k"));

    QSignalSpy kFailedSpy(&transcriber, &TranscriptionProvider::failed);
    transcriber.transcribe({0.0F});
    QVERIFY(kFailedSpy.wait(kReplyTimeout));

    QCOMPARE(kFailedSpy.at(0).at(0).value<ProviderError>(), ProviderError::kUnknown);
}

QTEST_GUILESS_MAIN(TestTranscriber)

#include "tst_transcriber.moc"
