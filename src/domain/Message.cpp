#include "domain/Message.h"

#include <utility>

Message::Message(MessageRole role, QString text) : role_(role), text_(std::move(text)) {
}

Message::Message(MessageRole role, QString text, ImageAttachment image)
    : role_(role), text_(std::move(text)), image_(std::move(image)) {
}

MessageRole Message::role() const noexcept {
    return role_;
}

const QString& Message::text() const noexcept {
    return text_;
}

bool Message::isUser() const noexcept {
    return role_ == MessageRole::kUser;
}

bool Message::hasImage() const noexcept {
    return image_.has_value();
}

const std::optional<ImageAttachment>& Message::image() const noexcept {
    return image_;
}

bool Message::operator==(const Message& other) const {
    return role_ == other.role_ && text_ == other.text_ && image_ == other.image_;
}
