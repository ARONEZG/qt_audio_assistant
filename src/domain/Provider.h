#pragma once

#include <QObject>
#include <QString>
#include <vector>

#include "domain/Answer.h"
#include "domain/Message.h"

// Role interface of an LLM backend. An implementation turns a chat history into
// a streaming Answer.
//
// Contract: respond() returns immediately and must NOT drive the Answer
// synchronously. It creates an Answer, returns it, and feeds it later (from a
// network thread, the event loop, or a test).
//
// Ownership: the provider owns the returned Answer (it is parented to the
// provider). It must emit one terminal signal (completed/failed/cancelled)
// before destroying the Answer - deliver the terminal state first, then drop
// the Answer (deleteLater() is the recommended way). Destroying an Answer
// without a terminal signal is tolerated (consumers hold it weakly) but leaves
// the consumer without a result.
class Provider : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Provider)

public:
    explicit Provider(QObject* parent = nullptr);
    ~Provider() override;

    [[nodiscard]] virtual QString id() const = 0;
    [[nodiscard]] virtual QString displayName() const = 0;
    virtual Answer* respond(const std::vector<Message>& history) = 0;
};
