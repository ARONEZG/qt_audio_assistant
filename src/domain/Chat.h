#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <vector>

#include "domain/Message.h"
#include "domain/Provider.h"

// One chat cell of the grid. It owns its provider (dependency injected through
// the constructor) and keeps an append-only history of finished messages. The
// in-flight answer never touches the history: it lives in streamingText() until
// the provider completes it, at which point it is frozen into a Message.
//
// Ownership of the in-flight Answer: the provider owns it (it is parented to the
// provider); Chat only observes it. currentAnswer_ is therefore a bare observing
// pointer, and Chat clears it on the Answer's destroyed() signal, so a provider
// that destroys its Answer (even before a terminal signal) can never leave Chat
// with a dangling pointer - Chat resets its streaming state instead.
class Chat : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Chat)

public:
    // Takes ownership of the provider. The provider must not be null.
    explicit Chat(std::unique_ptr<Provider> provider, QObject* parent = nullptr);
    ~Chat() override;

    [[nodiscard]] const std::vector<Message>& messages() const noexcept;
    [[nodiscard]] QString streamingText() const;
    [[nodiscard]] bool isStreaming() const noexcept;
    [[nodiscard]] QString providerId() const;
    [[nodiscard]] QString providerName() const;

    // Appends a user message and asks the provider to answer. Ignored while an
    // answer is already streaming (one in-flight answer per chat). Invokable so
    // the QML layer can call it without extra glue.
    Q_INVOKABLE void send(const QString& text);
    // Abandons the in-flight answer, if any, without appending a reply.
    Q_INVOKABLE void cancel();

signals:
    void streamingTextChanged();
    void stateChanged();
    void replyReceived(const Message& message);
    // Typed failure so the presentation layer can branch (retry on Network,
    // prompt on Authentication, ...).
    void failed(ProviderError error, const QString& detail);
    // Emitted when the in-flight answer is abandoned, so "cancelled" is
    // observable and distinct from "still streaming".
    void cancelled();

private:
    void startStreaming();
    void stopStreaming();
    void setStreamingText(const QString& text);
    void setStreaming(bool streaming);

    void onChunkReceived(const QString& chunk);
    void onAnswerCompleted(const Message& message);
    void onAnswerFailed(ProviderError error, const QString& detail);
    void onAnswerCancelled();
    void onAnswerDestroyed();

    std::unique_ptr<Provider> provider_;
    std::vector<Message> messages_;
    QString streamingText_;
    bool isStreaming_ = false;
    Answer* currentAnswer_ = nullptr;
};
