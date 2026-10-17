#include "domain/NewChat.h"

#include "domain/Chat.h"
#include "domain/Chats.h"
#include "domain/Providers.h"

NewChat::NewChat(Chats& chats, const Providers& providers, QObject* parent)
    : QObject(parent), chats_(chats), providers_(providers) {
}

NewChat::~NewChat() = default;

Chat* NewChat::open(const QString& providerId) {
    // The composition: the provider list mints a fresh provider, the grid takes
    // exclusive ownership of it. A missing id (or a recipe that could not build
    // a provider) yields null, which Chats::add() rejects, so no chat is created.
    return chats_.add(providers_.fresh(providerId));
}
