#include "domain/Message.h"

#include <utility>

Message::Message(MessageRole role, QString text) : role_(role), text_(std::move(text)) {
}

MessageRole Message::role() const noexcept {
    return role_;
}

const QString& Message::text() const noexcept {
    return text_;
}

bool Message::operator==(const Message& other) const {
    return role_ == other.role_ && text_ == other.text_;
}
