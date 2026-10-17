#pragma once

#include <QObject>
#include <QString>
#include <cstdint>

#include "domain/Message.h"
#include "domain/ProviderError.h"

// One streaming answer. It is the single place where mutation is justified: it
// models an asynchronous operation whose text and terminal state change while
// the owning provider feeds it. Consumers only observe it (queries + signals);
// commands feed it, and cancel() lets a consumer abandon it.
//
// Invariant: once a terminal command (complete/fail/cancel) has been accepted,
// the Answer is finished and every further command is ignored.
class Answer : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Answer)

public:
    explicit Answer(QObject* parent = nullptr);
    ~Answer() override;

    // Commands. The first terminal command wins; later ones are no-ops.
    void appendChunk(const QString& chunk);
    // Freezes the finished reply and emits completed(). The provider supplies
    // the frozen message; its role is Assistant and its text is expected to
    // equal the accumulated text() (the Answer does not re-derive it).
    void complete(const Message& message);
    void fail(ProviderError error, const QString& detail);
    // Abandons the answer and emits cancelled(). Observable so a consumer can
    // tell "cancelled" apart from "still streaming".
    void cancel();

    // Queries.
    [[nodiscard]] QString text() const;
    [[nodiscard]] bool isCompleted() const noexcept;
    [[nodiscard]] bool isFailed() const noexcept;
    [[nodiscard]] bool isCancelled() const noexcept;
    [[nodiscard]] ProviderError error() const noexcept;
    [[nodiscard]] QString errorText() const;

signals:
    void chunkReceived(const QString& chunk);
    void completed(const Message& message);
    void failed(ProviderError error, const QString& detail);
    void cancelled();

private:
    enum class State : std::uint8_t {
        kStreaming,
        kCompleted,
        kFailed,
        kCancelled,
    };

    [[nodiscard]] bool isStreaming() const noexcept;

    State state_ = State::kStreaming;
    QString text_;
    ProviderError error_ = ProviderError::kUnknown;
    QString errorText_;
};
