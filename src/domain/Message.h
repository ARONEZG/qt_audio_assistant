#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
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
//
// It is exposed to QML as an anonymous type: a view can observe a message (for
// example as the argument of Chat::replyReceived) and read its properties, but
// it can never construct one - the domain is the only source of valid messages.
class Message {
    Q_GADGET
    QML_ANONYMOUS
    Q_PROPERTY(QString text READ text CONSTANT)
    Q_PROPERTY(bool isUser READ isUser CONSTANT)

public:
    Message(MessageRole role, QString text);

    [[nodiscard]] MessageRole role() const noexcept;
    [[nodiscard]] const QString& text() const noexcept;
    // Derived query used by the presentation layer to pick a message's styling;
    // the raw role stays available for anything that needs the full distinction.
    [[nodiscard]] bool isUser() const noexcept;

    [[nodiscard]] bool operator==(const Message& other) const;

private:
    MessageRole role_;
    QString text_;
};
