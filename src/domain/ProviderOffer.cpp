#include "domain/ProviderOffer.h"

#include <utility>

ProviderOffer::ProviderOffer(QString id, QString displayName, Recipe recipe)
    : id_(std::move(id)), displayName_(std::move(displayName)), recipe_(std::move(recipe)) {
    Q_ASSERT(!id_.isEmpty());
    Q_ASSERT(recipe_ != nullptr);
}

const QString& ProviderOffer::id() const noexcept {
    return id_;
}

const QString& ProviderOffer::displayName() const noexcept {
    return displayName_;
}

std::unique_ptr<Provider> ProviderOffer::fresh() const {
    std::unique_ptr<Provider> provider = recipe_();
    Q_ASSERT(provider != nullptr);
    return provider;
}
