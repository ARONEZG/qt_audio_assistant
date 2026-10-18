#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>

#include "domain/ProviderConfig.h"
#include "domain/ProviderConfigFile.h"

// The provider configuration as the UI edits it.
//
// This is the one intentionally mutable object in the chat domain. Every other
// domain value is immutable; settings are not, because they are the user's
// in-progress edits plus a persistence command, and the recipe that mints a
// provider must observe the latest values rather than the ones captured at
// startup.
//
// Responsibilities:
//   * hold the current baseUrl / model (persisted) and apiKey (session only);
//   * validate and persist baseUrl + model through the injected
//     ProviderConfigFile;
//   * report the reason a save was rejected through lastError (never a key).
//
// The API key stays in memory for the session and is never written by save().
// It is a separate field from the persisted ProviderConfig by design.
class ProviderSettings : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ProviderSettings)
    QML_ELEMENT
    QML_UNCREATABLE("Injected by the composition root")

    Q_PROPERTY(QString baseUrl READ baseUrl WRITE setBaseUrl NOTIFY baseUrlChanged)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY apiKeyChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    // Takes the initial values (already resolved from file + environment by the
    // composition root) and the file the persisted pair is written to. The file
    // is owned here, so the settings object always has a valid store.
    ProviderSettings(ProviderConfig config, QString apiKey, ProviderConfigFile file, QObject* parent = nullptr);
    ~ProviderSettings() override;

    [[nodiscard]] QString baseUrl() const;
    [[nodiscard]] QString apiKey() const;
    [[nodiscard]] QString model() const;
    [[nodiscard]] QString lastError() const;

    void setBaseUrl(QString baseUrl);
    void setApiKey(QString apiKey);
    void setModel(QString model);

    // Validates the three fields, persists baseUrl + model and keeps apiKey in
    // memory. Returns false and stores a user-facing reason in lastError when
    // the values are missing or invalid; the previous configuration file is
    // left untouched.
    Q_INVOKABLE bool save();

    // Clears a previous save error (for example when the user starts editing
    // again). Kept separate so the view can call it on open without touching
    // the values.
    Q_INVOKABLE void clearError();

signals:
    void baseUrlChanged();
    void apiKeyChanged();
    void modelChanged();
    void lastErrorChanged();
    void saved();

private:
    void setLastError(const QString& error);

    ProviderConfig config_;
    QString apiKey_;
    ProviderConfigFile file_;
    QString lastError_;
};
