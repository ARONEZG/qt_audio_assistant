#pragma once

#include <QObject>
#include <QString>
#include <vector>

#include "domain/Answer.h"
#include "domain/Message.h"
#include "domain/Provider.h"

// Test double for the Provider role interface. It never drives the returned
// Answer on its own: the test acts as the "network" and feeds that Answer
// explicitly, which keeps the tests deterministic (no event loop, no sleep).
class FakeProvider : public Provider {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(FakeProvider)

public:
    explicit FakeProvider(QObject* parent = nullptr);
    ~FakeProvider() override;

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    Answer* respond(const std::vector<Message>& history) override;

    [[nodiscard]] Answer* lastAnswer() const noexcept;
    [[nodiscard]] const std::vector<Message>& lastHistory() const noexcept;
    [[nodiscard]] int respondCount() const noexcept;

    // Simulates a provider that owns and disposes of its Answer (e.g. after a
    // terminal signal). Used to prove Chat survives provider-side destruction.
    void destroyLastAnswer();

private:
    Answer* lastAnswer_ = nullptr;
    std::vector<Message> lastHistory_;
    int respondCount_ = 0;
};
