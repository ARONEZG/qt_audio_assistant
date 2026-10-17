#pragma once

#include <QObject>
#include <QString>
#include <vector>

#include "domain/Answer.h"
#include "domain/Message.h"
#include "domain/Provider.h"

// A deliberately local, in-memory Provider used to make the MVP interactive
// without a network. It is NOT a product feature: the real provider (Polza AI,
// OpenAI-compatible, streamed over HTTP) is the next increment. It exists only
// so the composition root in main() has something to wire into the grid, and it
// is labelled as such in its display name.
//
// It answers by echoing the last user message back from the event loop - so it
// respects the Provider contract (respond() returns immediately, the Answer is
// fed later) instead of completing synchronously.
class PlaceholderProvider : public Provider {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PlaceholderProvider)

public:
    explicit PlaceholderProvider(QObject* parent = nullptr);
    ~PlaceholderProvider() override;

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    Answer* respond(const std::vector<Message>& history) override;
};
