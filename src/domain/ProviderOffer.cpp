#include "domain/ProviderOffer.h"

#include <utility>

ProviderOffer::ProviderOffer(QString id, QString displayName, Recipe recipe)
    : id_(std::move(id)), displayName_(std::move(displayName)), recipe_(std::move(recipe)) {
    // Debug-build guards: the composition root is expected to hand over a valid
    // offer, so release builds do not pay for the checks. An empty id simply
    // never matches a lookup; a null recipe is handled by fresh() below.
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
    // No assertion on the result: a recipe that cannot build a provider returns
    // null, and "no provider" is a documented outcome the callers handle (an
    // assertion here would be a release-build no-op anyway).
    return recipe_();
}
