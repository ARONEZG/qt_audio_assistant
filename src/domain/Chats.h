#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <cstdint>
#include <memory>
#include <vector>

#include "domain/Chat.h"
#include "domain/Provider.h"

// The collection of open chats. It is a data object: it owns the chats it was
// given and exposes them row by row, but it never talks to a provider's network,
// never decides when a chat sends, and holds no static state. A view binds to it
// and calls add()/remove()/clear().
//
// The application opens a single chat; the collection type still models several
// so the single-chat UI can be widened later without a new model.
//
// Ownership: the collection owns the chats through std::unique_ptr; at() and
// add() return observing pointers. A chat is created with the provider it was
// handed, so the caller mints one instance per chat (Providers::fresh(id)) and
// the chats never share a provider.
class Chats : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Chats)
    // The collection is composed in main() and injected into the view; a view
    // must never mint its own chats, so the type is visible to QML but
    // uncreatable.
    QML_ELEMENT
    QML_UNCREATABLE("The composition root owns the chats")

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    // The Chat object of a row is exposed as model.chat. Qt roles are plain ints
    // by design, so the enum stays unscoped; the smallest type that fits the
    // Qt::UserRole range keeps the enum from being padded to int.
    enum Role : std::uint16_t {
        kChatRole = static_cast<std::uint16_t>(Qt::UserRole) + 1,
    };

    explicit Chats(QObject* parent = nullptr);
    ~Chats() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const noexcept;
    // The chat at index, or nullptr when the index is out of range (a stale row
    // yields nothing instead of undefined behaviour); the returned chat stays
    // owned by the grid.
    [[nodiscard]] Chat* at(int index) const;

    // Takes ownership of the provider, builds the chat that owns it and appends
    // it. Returns the created chat (the grid keeps owning it) for callers that
    // want to select it right away, or nullptr when provider is null - which is
    // exactly what Providers::fresh() returns for an id that is not offered, so
    // a failed lookup cannot silently become a chat with no provider.
    [[nodiscard]] Chat* add(std::unique_ptr<Provider> provider);
    // Removes and destroys the chat at index. Returns false when the index is
    // out of range - removing nothing is a no-op, not an error.
    bool remove(int index);
    // Removes and destroys every chat. Used when the provider of the single open
    // chat is switched: the previous chat (and its history) is dropped as a
    // whole rather than left behind invisibly. Clearing an already empty
    // collection is a no-op that emits nothing.
    Q_INVOKABLE void clear();

signals:
    void countChanged();

private:
    [[nodiscard]] bool isValidRow(int index) const noexcept;

    std::vector<std::unique_ptr<Chat>> chats_;
};
