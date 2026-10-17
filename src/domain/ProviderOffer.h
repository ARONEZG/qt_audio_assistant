#pragma once

#include <QString>
#include <functional>
#include <memory>

#include "domain/Provider.h"

// One provider the application can offer for selection: a stable identity, a
// human-readable name, and the recipe that mints a provider instance.
//
// Why a recipe and not a ready instance: a Chat owns its provider exclusively
// (std::unique_ptr), so no two chats may share one, while a Provider is a
// QObject and is therefore neither copyable nor movable. Handing out "the"
// provider would force shared ownership of something the contract says is
// owned; the recipe lets every caller mint an independent instance instead.
// This keeps instantiation out of the domain as a value ("here is how to make
// one"), so no factory class, no service locator and no global state is needed.
// A real offer captures its configuration (PolzaProvider will capture
// url/apiKey/model); a test offer captures a FakeProvider.
class ProviderOffer {
public:
    // Mints one fresh provider instance. Must not return nullptr.
    using Recipe = std::function<std::unique_ptr<Provider>()>;

    // Valid at creation: the id is the key a chat is created by and must not be
    // empty, and the recipe must be callable.
    ProviderOffer(QString id, QString displayName, Recipe recipe);

    [[nodiscard]] const QString& id() const noexcept;
    [[nodiscard]] const QString& displayName() const noexcept;

    // Returns a new provider instance owned by the caller; every call returns an
    // independent object.
    [[nodiscard]] std::unique_ptr<Provider> fresh() const;

private:
    QString id_;
    QString displayName_;
    Recipe recipe_;
};
