#include "domain/Providers.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <set>
#include <utility>

Providers::Providers(std::vector<ProviderOffer> offers, QObject* parent)
    : QAbstractListModel(parent), offers_(std::move(offers)) {
    // A duplicate id would make fresh() ambiguous, so the offered identities are
    // a set by construction. Built unconditionally: a handful of offers makes
    // the check cheaper than the branch around it.
    std::set<QString> offeredIds;
    for (const ProviderOffer& offer : offers_) {
        offeredIds.insert(offer.id());
    }
    Q_ASSERT(offeredIds.size() == offers_.size());
}

Providers::~Providers() = default;

int Providers::rowCount(const QModelIndex& parent) const {
    // A list model has no children, so rows exist only under the root index.
    if (parent.isValid()) {
        return 0;
    }
    return count();
}

QVariant Providers::data(const QModelIndex& index, int role) const {
    // An invalid index has row() == -1, and an index built by another model can
    // name a row past the end; both fail the row check, so a list model stays
    // robust without a second precondition.
    if (!isValidRow(index.row())) {
        return {};
    }
    const ProviderOffer& offer = offers_[static_cast<std::size_t>(index.row())];
    if (role == kIdRole) {
        return offer.id();
    }
    if (role == kDisplayNameRole) {
        return offer.displayName();
    }
    return {};
}

QHash<int, QByteArray> Providers::roleNames() const {
    // roleNames() is a Qt model API whose return type is mandated as QHash. This
    // is the unavoidable-container boundary from src/AGENTS.md: the QHash is
    // built here, never stored in a field and never returned by another API.
    QHash<int, QByteArray> roles;
    roles.insert(kIdRole, QByteArrayLiteral("id"));
    roles.insert(kDisplayNameRole, QByteArrayLiteral("displayName"));
    return roles;
}

int Providers::count() const noexcept {
    return static_cast<int>(offers_.size());
}

bool Providers::contains(const QString& id) const {
    return indexOf(id) >= 0;
}

QString Providers::idAt(int index) const {
    Q_ASSERT(isValidRow(index));
    return offers_[static_cast<std::size_t>(index)].id();
}

QString Providers::displayNameAt(int index) const {
    Q_ASSERT(isValidRow(index));
    return offers_[static_cast<std::size_t>(index)].displayName();
}

std::unique_ptr<Provider> Providers::fresh(const QString& id) const {
    const int kIndex = indexOf(id);
    if (kIndex < 0) {
        return nullptr;
    }
    return offers_[static_cast<std::size_t>(kIndex)].fresh();
}

bool Providers::isValidRow(int index) const noexcept {
    return index >= 0 && index < count();
}

int Providers::indexOf(const QString& id) const {
    const auto kFound = std::ranges::find_if(offers_, [&id](const ProviderOffer& offer) { return offer.id() == id; });
    if (kFound == offers_.end()) {
        return -1;
    }
    return static_cast<int>(std::ranges::distance(offers_.begin(), kFound));
}
