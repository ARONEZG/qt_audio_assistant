#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <functional>
#include <map>

class QTcpSocket;

// A minimal, deterministic OpenAI-compatible HTTP endpoint for tests. It binds
// to 127.0.0.1 on an ephemeral port and answers each parsed request with
// whatever the installed handler writes, so a test can script a streamed SSE
// reply, an HTTP error, a malformed frame, a stalled connection or an abrupt
// close without touching the real network.
//
// It is a test double for the *wire protocol*, not for a C++ class: it never
// inherits a project or LLMQore type, so it cannot leak provider internals into
// the test and it exercises the adapter through the same bytes a real endpoint
// would send.
class OpenAiMockServer : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(OpenAiMockServer)

public:
    // Writes one complete HTTP response for a parsed request. `body` is the
    // request body (the JSON payload). The connection stays open until the
    // handler closes it, so a handler can stream and then keep it alive to let
    // the client cancel or time out.
    using Handler = std::function<void(QTcpSocket& socket, const QByteArray& body)>;

    explicit OpenAiMockServer(QObject* parent = nullptr);
    ~OpenAiMockServer() override;

    // Binds to 127.0.0.1:0. False when no port could be claimed.
    [[nodiscard]] bool listen();
    // Base URL the client is configured with (…/v1); LLMQore appends
    // "/chat/completions" to it.
    [[nodiscard]] QString baseUrl() const;

    void setHandler(Handler handler);

    [[nodiscard]] int requestCount() const noexcept;
    [[nodiscard]] QByteArray lastRequestBody() const;
    // The request target of the last fully received request, e.g.
    // "/v1/audio/transcriptions" (the second token of the request line).
    [[nodiscard]] QByteArray lastRequestTarget() const;
    // The value of a header of the last fully received request, trimmed and
    // looked up case-insensitively; empty when the header is absent.
    [[nodiscard]] QByteArray lastRequestHeader(const QByteArray& name) const;

signals:
    // Emitted once a client closed a connection. A scenario that keeps the
    // socket open (cancellation) uses this to observe the client's abort.
    void clientDisconnected();

private:
    struct Connection {
        QByteArray buffer;
        QByteArray head;
        qsizetype bodyOffset = 0;
        qsizetype contentLength = 0;
        bool headersParsed = false;
        bool handled = false;
    };

    void onNewConnection();
    void onReadyRead(QTcpSocket* socket);
    void onDisconnected(QTcpSocket* socket);

    QTcpServer server_;
    Handler handler_;
    std::map<QTcpSocket*, Connection> connections_;
    int requestCount_ = 0;
    QByteArray lastRequestBody_;
    QByteArray lastRequestHead_;
};
