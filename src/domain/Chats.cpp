#include "domain/Chats.h"

#include <QQmlEngine>
#include <cstddef>
#include <utility>

Chats::Chats(QObject* parent) : QAbstractListModel(parent) {
}

Chats::~Chats() = default;

int Chats::rowCount(const QModelIndex& parent) const {
    // A list model has no children, so rows exist only under the root index.
    if (parent.isValid()) {
        return 0;
    }
    return count();
}

QVariant Chats::data(const QModelIndex& index, int role) const {
    // A list model is flat: every index it hands out is parentless and in the
    // single column, so the row check is the whole contract. An invalid index
    // has row() == -1, and an index built by another model can name a row past
    // the end; both yield nothing.
    if (!isValidRow(index.row())) {
        return {};
    }
    const std::unique_ptr<Chat>& chat = chats_[static_cast<std::size_t>(index.row())];
    if (role == kChatRole) {
        return QVariant::fromValue(chat.get());
    }
    return {};
}

QHash<int, QByteArray> Chats::roleNames() const {
    // roleNames() is a Qt model API whose return type is mandated as QHash. This
    // is the unavoidable-container boundary from src/AGENTS.md: the QHash is
    // built here, never stored in a field and never returned by another API.
    QHash<int, QByteArray> roles;
    roles.insert(kChatRole, QByteArrayLiteral("chat"));
    return roles;
}

int Chats::count() const noexcept {
    return static_cast<int>(chats_.size());
}

Chat* Chats::at(int index) const {
    if (!isValidRow(index)) {
        return nullptr;
    }
    return chats_[static_cast<std::size_t>(index)].get();
}

Chat* Chats::add(std::unique_ptr<Provider> provider) {
    if (provider == nullptr) {
        return nullptr;
    }
    const int kRow = count();
    beginInsertRows(QModelIndex(), kRow, kRow);
    chats_.push_back(std::make_unique<Chat>(std::move(provider)));
    endInsertRows();
    Chat* chat = chats_.back().get();
    // The collection owns its chats through unique_ptr, so a chat handed to QML
    // (via the model role or NewChat::open) must stay C++-owned; otherwise the
    // QML engine would take JavaScript ownership of a parentless QObject and
    // delete it, leaving the collection's unique_ptr dangling.
    QQmlEngine::setObjectOwnership(chat, QQmlEngine::CppOwnership);
    emit countChanged();
    return chat;
}

bool Chats::remove(int index) {
    if (!isValidRow(index)) {
        return false;
    }
    beginRemoveRows(QModelIndex(), index, index);
    chats_.erase(chats_.begin() + index);
    endRemoveRows();
    emit countChanged();
    return true;
}

void Chats::clear() {
    if (chats_.empty()) {
        return;
    }
    // A wholesale replacement of the model's contents: a reset is the correct
    // notification (no per-row bookkeeping) and a bound view rebuilds from the
    // empty state.
    //
    // The chats themselves are moved aside and destroyed only after
    // endResetModel(): a QML view holds each chat as a delegate property, and
    // destroying it before the view has been told to drop its delegates would
    // leave those delegates reading a dangling object. The swap keeps the chats
    // alive through the reset and releases them once the view has rebuilt.
    std::vector<std::unique_ptr<Chat>> doomed;
    beginResetModel();
    doomed.swap(chats_);
    endResetModel();
    emit countChanged();
}

bool Chats::isValidRow(int index) const noexcept {
    return index >= 0 && index < count();
}
