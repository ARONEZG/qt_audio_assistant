#include "domain/NewChat.h"

#include <memory>
#include <utility>

#include "domain/Chat.h"
#include "domain/Chats.h"
#include "domain/Provider.h"
#include "domain/Providers.h"

NewChat::NewChat(Chats& chats, const Providers& providers, QObject* parent)
    : QObject(parent), chats_(chats), providers_(providers) {
}

NewChat::~NewChat() = default;

Chat* NewChat::open(const QString& providerId) {
    // Resolve the provider before touching the open chat: an unknown id (or a
    // recipe that could not build a provider) yields null, so a failed lookup
    // leaves any already-open chat untouched instead of closing the conversation.
    std::unique_ptr<Provider> provider = providers_.fresh(providerId);
    if (provider == nullptr) {
        return nullptr;
    }
    // The application holds a single chat: re-opening swaps the provider of the
    // open chat (keeping its conversation) rather than appending another one.
    if (Chat* existing = chats_.at(0); existing != nullptr) {
        existing->setProvider(std::move(provider));
        return existing;
    }
    // No chat is open yet: the collection takes exclusive ownership and mints the
    // first one.
    return chats_.add(std::move(provider));
}
