#pragma once

#include <QString>
#include <cstdint>

// Who authored a Message. Kept as a plain value enum: the domain never needs to
// dispatch on the provider or transport that produced the text.
enum class MessageRole : std::uint8_t {
    kUser,
    kAssistant,
    kSystem,
};

// A single chat message. It is an immutable value: it is valid at construction
// and never mutated afterwards, so it can be copied and compared freely.
class Message {
public:
    Message(MessageRole role, QString text);

    [[nodiscard]] MessageRole role() const noexcept;
    [[nodiscard]] const QString& text() const noexcept;

    [[nodiscard]] bool operator==(const Message& other) const;

private:
    MessageRole role_;
    QString text_;
};
