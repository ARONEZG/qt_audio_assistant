#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <cstdint>
#include <memory>
#include <vector>

#include "domain/Provider.h"
#include "domain/ProviderOffer.h"

// The collection of providers offered for selection: an ordered, immutable list
// of identities plus the recipe to mint one instance per chat. It is a data
// object: it never creates, owns, deletes or orchestrates chats, and it has no
// static state. Its two jobs are "which providers exist" and "give me a fresh
// one of these" - the latter is what lets Chats::add() take exclusive ownership
// of a provider it did not have to know how to build.
class Providers : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Providers)

public:
    // Roles exposed to a view; a delegate reads model.<roleName>. Qt roles are
    // plain ints by design, so the enum stays unscoped; the smallest type that
    // fits the Qt::UserRole range keeps the enum from being padded to int.
    enum Role : std::uint16_t {
        kIdRole = static_cast<std::uint16_t>(Qt::UserRole) + 1,
        kDisplayNameRole,
    };

    // Ids must be unique: fresh() resolves an offer by id.
    explicit Providers(std::vector<ProviderOffer> offers, QObject* parent = nullptr);
    ~Providers() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const noexcept;
    [[nodiscard]] bool contains(const QString& id) const;
    // Precondition: index is in range (checked in debug builds), like any
    // container accessor.
    [[nodiscard]] QString idAt(int index) const;
    [[nodiscard]] QString displayNameAt(int index) const;

    // Mints a fresh instance of the offered provider and passes its ownership to
    // the caller (normally straight into Chats::add). A null result means the id
    // is not offered at all - not an "empty provider".
    [[nodiscard]] std::unique_ptr<Provider> fresh(const QString& id) const;

private:
    [[nodiscard]] bool isValidRow(int index) const noexcept;
    // Position of the offered id, or -1 when it is not offered.
    [[nodiscard]] int indexOf(const QString& id) const;

    std::vector<ProviderOffer> offers_;
};
