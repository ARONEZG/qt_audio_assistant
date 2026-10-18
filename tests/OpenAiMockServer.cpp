#include "OpenAiMockServer.h"

#include <QHostAddress>
#include <QTcpSocket>
#include <utility>

namespace {

// Case-insensitive header value lookup over a raw request head. Returns the
// trimmed value, or an empty array when the header is absent.
QByteArray headerText(const QByteArray& head, const QByteArray& name) {
    const QByteArray kLower = head.toLower();
    const QByteArray kKey = name.toLower() + ":";
    const qsizetype kIndex = kLower.indexOf(kKey);
    if (kIndex < 0) {
        return {};
    }
    const qsizetype kStart = kIndex + kKey.size();
    const qsizetype kEnd = head.indexOf("\r\n", kStart);
    const qsizetype kStop = kEnd < 0 ? head.size() : kEnd;
    return head.mid(kStart, kStop - kStart).trimmed();
}

// Case-insensitive header lookup over a raw request head. Returns the numeric
// value of the header, or -1 when it is absent.
qsizetype headerValue(const QByteArray& head, const QByteArray& name) {
    const QByteArray kValue = headerText(head, name);
    return kValue.isEmpty() ? -1 : static_cast<qsizetype>(kValue.toLongLong());
}

// The request target (second token of the request line), e.g.
// "/v1/audio/transcriptions".
QByteArray requestTarget(const QByteArray& head) {
    const qsizetype kLineEnd = head.indexOf("\r\n");
    const QByteArray kRequestLine = kLineEnd < 0 ? head : head.left(kLineEnd);
    const qsizetype kFirstSpace = kRequestLine.indexOf(' ');
    if (kFirstSpace < 0) {
        return {};
    }
    const qsizetype kSecondSpace = kRequestLine.indexOf(' ', kFirstSpace + 1);
    const qsizetype kEnd = kSecondSpace < 0 ? kRequestLine.size() : kSecondSpace;
    return kRequestLine.mid(kFirstSpace + 1, kEnd - kFirstSpace - 1);
}

}  // namespace

OpenAiMockServer::OpenAiMockServer(QObject* parent) : QObject(parent) {
    connect(&server_, &QTcpServer::newConnection, this, &OpenAiMockServer::onNewConnection);
}

OpenAiMockServer::~OpenAiMockServer() {
    // Detach from every socket before the members are destroyed. The QTcpServer
    // deletes its pending connections during its own destruction, and a socket
    // may emit disconnected() from its destructor; without this the callback
    // would re-enter onDisconnected() after connections_ is already gone.
    for (const auto& entry : connections_) {
        QObject::disconnect(entry.first, nullptr, this, nullptr);
    }
    connections_.clear();
}

bool OpenAiMockServer::listen() {
    return server_.listen(QHostAddress::LocalHost, 0);
}

QString OpenAiMockServer::baseUrl() const {
    return QStringLiteral("http://127.0.0.1:%1/v1").arg(server_.serverPort());
}

void OpenAiMockServer::setHandler(Handler handler) {
    handler_ = std::move(handler);
}

int OpenAiMockServer::requestCount() const noexcept {
    return requestCount_;
}

QByteArray OpenAiMockServer::lastRequestBody() const {
    return lastRequestBody_;
}

QByteArray OpenAiMockServer::lastRequestTarget() const {
    return requestTarget(lastRequestHead_);
}

QByteArray OpenAiMockServer::lastRequestHeader(const QByteArray& name) const {
    return headerText(lastRequestHead_, name);
}

void OpenAiMockServer::onNewConnection() {
    while (QTcpSocket* socket = server_.nextPendingConnection()) {
        connections_.emplace(socket, Connection{});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { onDisconnected(socket); });
    }
}

void OpenAiMockServer::onReadyRead(QTcpSocket* socket) {
    const auto kIt = connections_.find(socket);
    if (kIt == connections_.end()) {
        return;
    }
    Connection& connection = kIt->second;
    connection.buffer.append(socket->readAll());

    if (!connection.headersParsed) {
        const qsizetype kHeaderEnd = connection.buffer.indexOf("\r\n\r\n");
        if (kHeaderEnd < 0) {
            return;
        }
        connection.headersParsed = true;
        connection.bodyOffset = kHeaderEnd + 4;
        connection.head = connection.buffer.left(kHeaderEnd);
        const qsizetype kLength = headerValue(connection.head, "Content-Length");
        connection.contentLength = kLength < 0 ? 0 : kLength;
    }

    if (connection.handled || connection.buffer.size() - connection.bodyOffset < connection.contentLength) {
        return;
    }
    connection.handled = true;
    ++requestCount_;
    lastRequestHead_ = connection.head;
    lastRequestBody_ = connection.buffer.mid(connection.bodyOffset, connection.contentLength);
    if (handler_) {
        handler_(*socket, lastRequestBody_);
    }
}

void OpenAiMockServer::onDisconnected(QTcpSocket* socket) {
    connections_.erase(socket);
    emit clientDisconnected();
    socket->deleteLater();
}
