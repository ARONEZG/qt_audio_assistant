#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>

class Chats;
class Providers;
class Chat;

// The single composition seam between the provider list and the chats.
//
// The view expresses one intent - "use the selected provider for the open chat"
// - and neither half can be done from QML on its own: Chats::add() and
// Chat::setProvider() take a std::unique_ptr<Provider> (QML has no unique_ptr)
// and Providers::fresh() returns one (QML cannot receive it). This thin
// invokable exists to carry that one composition:
//
//     chat ? chat->setProvider(providers.fresh(providerId))
//          : chats.add(providers.fresh(providerId))
//
// It is not a controller: it owns nothing, holds no state and has no decision to
// make beyond delegating to the two data objects. The composition root (main())
// decides *which* chats and *which* provider list it works on by constructing
// it; the object merely lets the QML intent reach that decision.
class NewChat : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NewChat)
    QML_ELEMENT
    QML_UNCREATABLE("Injected by the composition root")

public:
    NewChat(Chats& chats, const Providers& providers, QObject* parent = nullptr);
    ~NewChat() override;

    // Opens the chat for the given provider id. The application holds a single
    // chat, so when one is already open its provider is replaced and its
    // conversation is kept; when none is open a chat is created. Returns the open
    // chat (the collection keeps owning it), or nullptr when the id is not
    // offered - a failed lookup leaves any already-open chat untouched, so an
    // unknown provider can never silently close the conversation.
    Q_INVOKABLE Chat* open(const QString& providerId);

private:
    Chats& chats_;
    const Providers& providers_;
};
