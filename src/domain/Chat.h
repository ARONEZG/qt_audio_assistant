#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "domain/Message.h"
#include "domain/Provider.h"

// One chat cell of the grid. It owns its provider (dependency injected through
// the constructor) and keeps an append-only history of finished messages. The
// in-flight answer never touches the history: it lives in streamingText() until
// the provider completes it, at which point it is frozen into a Message.
//
// The finished history is also the chat's QAbstractListModel body, so a QML
// ListView binds to the Chat itself and rows appear as messages are appended.
// The append-only contract is the model's: rows are only ever inserted, never
// removed or mutated, so an index handed to a delegate keeps naming the same
// message.
//
// Ownership of the in-flight Answer: the provider owns it (it is parented to the
// provider); Chat only observes it. currentAnswer_ is therefore a bare observing
// pointer, and Chat clears it on the Answer's destroyed() signal, so a provider
// that destroys its Answer (even before a terminal signal) can never leave Chat
// with a dangling pointer - Chat resets its streaming state instead.
class Chat : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Chat)
    // QML only presents chats; the grid builds them from a provider, so a chat
    // can never be constructed from a view without one.
    QML_ELEMENT
    QML_UNCREATABLE("Chats creates chats from a provider")

    Q_PROPERTY(QString providerId READ providerId CONSTANT)
    Q_PROPERTY(QString providerName READ providerName CONSTANT)
    Q_PROPERTY(QString streamingText READ streamingText NOTIFY streamingTextChanged)
    // The live reply as sanitised Markdown, so the streaming tail renders it
    // through the same safety boundary as the finished message (SafeMarkdown).
    Q_PROPERTY(QString streamingMarkup READ streamingMarkup NOTIFY streamingTextChanged)
    Q_PROPERTY(bool streaming READ isStreaming NOTIFY stateChanged)
    // Last failed request, presented to the user. Transient like streamingText:
    // an accepted send() starts a new request and clears it. Cancellation is not
    // a failure, so a cancelled answer never sets it.
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY errorTextChanged)

public:
    // The message roles a delegate binds to. Qt roles are plain ints by design,
    // so the enum stays unscoped; the smallest type that fits the Qt::UserRole
    // range keeps the enum from being padded to int.
    enum Role : std::uint16_t {
        kTextRole = static_cast<std::uint16_t>(Qt::UserRole) + 1,
        kIsUserRole,
        // Display-ready text: the assistant's Markdown is neutralised in C++
        // (SafeMarkdown), while the user's own text is passed through verbatim
        // because a view shows it as plain text. The raw `text` role stays for
        // accessibility, so a screen reader never reads Markdown escapes.
        kMarkupRole,
    };

    // Takes ownership of the provider. The provider must not be null.
    explicit Chat(std::unique_ptr<Provider> provider, QObject* parent = nullptr);
    ~Chat() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] const std::vector<Message>& messages() const noexcept;
    [[nodiscard]] QString streamingText() const;
    [[nodiscard]] QString streamingMarkup() const;
    [[nodiscard]] bool isStreaming() const noexcept;
    [[nodiscard]] QString errorText() const;
    [[nodiscard]] bool hasError() const noexcept;
    [[nodiscard]] QString providerId() const;
    [[nodiscard]] QString providerName() const;

    // Appends a user message and asks the provider to answer. Ignored while an
    // answer is already streaming (one in-flight answer per chat) and for empty
    // text (a blank message is meaningless); the invariant lives here, so the
    // view does not have to filter it. Invokable so the QML layer can call it
    // without extra glue.
    Q_INVOKABLE void send(const QString& text);
    // Abandons the in-flight answer, if any, without appending a reply.
    Q_INVOKABLE void cancel();

signals:
    void streamingTextChanged();
    void stateChanged();
    void errorTextChanged();
    void replyReceived(const Message& message);
    // Typed failure so the presentation layer can branch (retry on Network,
    // prompt on Authentication, ...).
    void failed(ProviderError error, const QString& detail);
    // Emitted when the in-flight answer is abandoned, so "cancelled" is
    // observable and distinct from "still streaming".
    void cancelled();

private:
    [[nodiscard]] bool isValidRow(int index) const noexcept;
    // Display text of one row, sanitised on first request and memoised.
    [[nodiscard]] QString markupAt(std::size_t row) const;

    void appendMessage(const Message& message);
    void startStreaming();
    void stopStreaming();
    void setStreamingText(const QString& text);
    void setStreaming(bool streaming);
    // The failure state is explicit rather than derived from errorText_, so an
    // error with an empty presentation text still counts as an error.
    void setErrorText(const QString& text);
    void clearError();

    void onChunkReceived(const QString& chunk);
    void onAnswerCompleted(const Message& message);
    void onAnswerFailed(ProviderError error, const QString& detail);
    void onAnswerCancelled();
    void onAnswerDestroyed();

    std::unique_ptr<Provider> provider_;
    std::vector<Message> messages_;
    // Display markup memoised per row and aligned index-for-index with messages_.
    // data(kMarkupRole) is queried many times per row while a view scrolls or
    // lays out; without the cache every query would re-parse the message. Filled
    // lazily so a headless consumer that never asks for markup never pays for it.
    mutable std::vector<std::optional<QString>> markupCache_;
    QString streamingText_;
    // Memoised streaming markup: recomputed only when the live text changes, so a
    // view that reads the property several times per chunk sanitises once.
    mutable QString streamingMarkupCache_;
    mutable bool streamingMarkupDirty_ = false;
    QString errorText_;
    bool hasError_ = false;
    bool isStreaming_ = false;
    Answer* currentAnswer_ = nullptr;
};
