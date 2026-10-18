#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>

class Chats;
class Providers;
class Chat;

// The single composition seam between the provider list and the chats.
//
// The view expresses one intent - "open a chat with the selected provider" - and
// neither half can be done from QML on its own: Chats::add() takes a
// std::unique_ptr<Provider> (QML has no unique_ptr) and Providers::fresh()
// returns one (QML cannot receive it). This thin invokable exists to carry that
// one composition:
//
//     chats.add(providers.fresh(providerId))
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

    // Appends a chat for the given provider id and returns it (the collection
    // keeps owning it), or nullptr when the id is not offered - the same "no
    // provider" outcome Providers::fresh() and Chats::add() already document.
    Q_INVOKABLE Chat* open(const QString& providerId);

private:
    Chats& chats_;
    const Providers& providers_;
};
