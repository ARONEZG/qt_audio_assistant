#pragma once

#include <QtQml/qqmlregistration.h>

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
//
// The list is composed in main() from configuration and injected into the view,
// so the QML type is visible but uncreatable.
class Providers : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Providers)
    QML_ELEMENT
    QML_UNCREATABLE("Built in main() from configuration")

    // The list is immutable after construction, so the count is constant too.
    Q_PROPERTY(int count READ count CONSTANT)

public:
    // Roles exposed to a view; a delegate reads model.<roleName>. Qt roles are
    // plain ints by design, so the enum stays unscoped; the smallest type that
    // fits the Qt::UserRole range keeps the enum from being padded to int.
    enum Role : std::uint16_t {
        kIdRole = static_cast<std::uint16_t>(Qt::UserRole) + 1,
        kDisplayNameRole,
    };

    // Ids must be unique: fresh() resolves an offer by id. Duplicates are a
    // composition-root mistake: debug builds assert, release builds resolve to
    // the first matching offer.
    explicit Providers(std::vector<ProviderOffer> offers, QObject* parent = nullptr);
    ~Providers() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const noexcept;
    [[nodiscard]] bool contains(const QString& id) const;
    // Queries by row are total: an out-of-range index yields "nothing" (a null
    // QString, QString::isNull()) rather than undefined behaviour, so a stale row
    // from a view can never crash the model.
    [[nodiscard]] QString idAt(int index) const;
    [[nodiscard]] QString displayNameAt(int index) const;

    // Mints a fresh instance of the offered provider and passes its ownership to
    // the caller (normally straight into Chats::add). Returns nullptr when the id
    // is not offered at all, or when the offer's recipe could not build a
    // provider - in both cases there is simply no provider, never an "empty" one.
    [[nodiscard]] std::unique_ptr<Provider> fresh(const QString& id) const;

private:
    [[nodiscard]] bool isValidRow(int index) const noexcept;
    // Position of the offered id, or -1 when it is not offered.
    [[nodiscard]] int indexOf(const QString& id) const;

    std::vector<ProviderOffer> offers_;
};
