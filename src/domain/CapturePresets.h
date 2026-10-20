#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <cstdint>
#include <vector>

#include "domain/CaptureConfig.h"

// The capture presets ("schemes") offered for selection: an ordered, immutable
// list of named CaptureConfig values. A preset is a starting point for the
// settings form, not a live configuration: selecting one fills the draft, and
// the user still saves explicitly.
//
// The list is composed by the composition root and injected into the view, so
// the QML type is visible but uncreatable. Like Providers, the model is a data
// object: it never persists or mutates anything.
class CapturePresets : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(CapturePresets)
    QML_ELEMENT
    QML_UNCREATABLE("Built in the composition root")

    // The list is immutable after construction, so the count is constant too.
    Q_PROPERTY(int count READ count CONSTANT)

public:
    // Roles exposed to a view; a delegate reads model.<roleName>. Qt roles are
    // plain ints by design, so the enum stays unscoped.
    enum Role : std::uint16_t {
        kIdRole = static_cast<std::uint16_t>(Qt::UserRole) + 1,
        kDisplayNameRole,
    };

    // Ids must be unique: a view resolves a preset by id. Duplicates are a
    // composition-root mistake: debug builds assert, release builds use the
    // first matching preset.
    explicit CapturePresets(QObject* parent = nullptr);
    ~CapturePresets() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const noexcept;

    // Queries by row are total: an out-of-range index yields "nothing" (a null
    // QString / zero / false / a default CaptureConfig) rather than undefined
    // behaviour, so a stale row from a view can never crash the model.
    [[nodiscard]] QString idAt(int index) const;
    [[nodiscard]] QString displayNameAt(int index) const;
    Q_INVOKABLE [[nodiscard]] int captureDurationSecondsAt(int index) const;
    Q_INVOKABLE [[nodiscard]] QString sttModelAt(int index) const;
    Q_INVOKABLE [[nodiscard]] QString sttLanguageAt(int index) const;
    Q_INVOKABLE [[nodiscard]] QString sttPromptAt(int index) const;
    Q_INVOKABLE [[nodiscard]] bool vadEnabledAt(int index) const;
    Q_INVOKABLE [[nodiscard]] double minSpeechSecondsAt(int index) const;
    [[nodiscard]] CaptureConfig configAt(int index) const;

private:
    struct Preset {
        QString id;
        QString displayName;
        CaptureConfig config;
    };

    [[nodiscard]] bool isValidRow(int index) const noexcept;
    [[nodiscard]] const Preset* presetAt(int index) const;

    std::vector<Preset> presets_;
};
