#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>

#include "domain/ProviderConfig.h"
#include "domain/ProviderConfigFile.h"

// The provider configuration as the UI sees it.
//
// This is the one intentionally mutable object in the chat domain. Every other
// domain value is immutable; settings are not, because they are the user's
// in-progress edits plus a persistence command, and the recipe that mints a
// provider must observe the latest values rather than the ones captured at
// startup.
//
// Mutation is a single explicit command, not a set of property setters:
//
//     save(baseUrl, model, apiKey)
//
// The command normalises and validates the whole draft, writes the persisted
// values (baseUrl + model + apiKey), then replaces the stored value atomically
// (config_ = normalized). A rejected save leaves both the previous state and
// the previous file untouched. The view keeps its fields as a local draft; the
// properties here are read-only.
//
// The API key is persisted by save() alongside the endpoint/model and is
// deliberately not a readable QML property. The command accepts it write-only
// (an empty value means "keep the current key"), and QML may read only
// isKeyConfigured, never the value.
class ProviderSettings : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ProviderSettings)
    QML_ELEMENT
    QML_UNCREATABLE("Injected by the composition root")

    Q_PROPERTY(QString baseUrl READ baseUrl NOTIFY baseUrlChanged)
    Q_PROPERTY(QString model READ model NOTIFY modelChanged)
    // Whether a key is present. Exposed instead of the key itself so the
    // view can hint the user without ever reading the secret back.
    Q_PROPERTY(bool isKeyConfigured READ isKeyConfigured NOTIFY isKeyConfiguredChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    // Takes the initial values (already resolved from file + environment by the
    // composition root) and the file the persisted values are written to. The
    // file is owned here, so the settings object always has a valid store.
    ProviderSettings(ProviderConfig config, QString apiKey, ProviderConfigFile file, QObject* parent = nullptr);
    ~ProviderSettings() override;

    [[nodiscard]] QString baseUrl() const;
    // Read by the composition root's provider recipe only; not exposed to QML.
    [[nodiscard]] QString apiKey() const;
    [[nodiscard]] bool isKeyConfigured() const;
    [[nodiscard]] QString model() const;
    [[nodiscard]] QString lastError() const;

    // The single mutating command. Persists baseUrl + model + apiKey; a non-empty
    // apiKey replaces the stored key, while an empty one keeps the current key
    // (important when it came from the environment or a previous save and was
    // never shown in the form). Returns false and stores a user-facing reason in
    // lastError when baseUrl/model (or the key) are invalid; the previous
    // configuration file and the stored settings are left untouched in that case.
    Q_INVOKABLE bool save(const QString& baseUrl, const QString& model, const QString& apiKey);

    // Clears a previous save error (for example when the user starts editing
    // again). Kept separate so the view can call it on edit without mutating the
    // stored configuration.
    Q_INVOKABLE void clearError();

signals:
    void baseUrlChanged();
    void modelChanged();
    void isKeyConfiguredChanged();
    void lastErrorChanged();
    void saved();

private:
    // Applies a config that has already been validated and persisted, replacing
    // the stored value as a whole and emitting only the changes that happened.
    void applySavedConfig(const ProviderConfig& config, const QString& apiKey);

    void setLastError(const QString& error);

    ProviderConfig config_;
    QString apiKey_;
    ProviderConfigFile file_;
    QString lastError_;
};
