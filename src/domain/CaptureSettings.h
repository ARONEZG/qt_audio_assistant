#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>

#include "domain/CaptureConfig.h"
#include "domain/CaptureConfigFile.h"
#include "domain/CapturePresets.h"

// The capture and transcription configuration as the UI sees it.
//
// Like ProviderSettings, this is intentionally mutable: it is the user's
// committed state plus the persistence command. Mutation is a single explicit
// command, not a set of property setters:
//
//     save(captureDurationSeconds, sttModel, sttLanguage, sttPrompt,
//          vadEnabled, minSpeechSeconds)
//
// The command normalises (trims the text fields) and validates the whole draft,
// writes the configuration file, then replaces the stored value atomically. A
// rejected save leaves both the previous state and the previous file untouched.
// The view keeps its fields as a local draft; the properties here are
// read-only.
//
// The presets model is borrowed, not owned: the composition root keeps it alive
// for the whole event loop and it is exposed read-only so the form can fill its
// draft from a scheme without saving.
class CaptureSettings : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(CaptureSettings)
    QML_ELEMENT
    QML_UNCREATABLE("Injected by the composition root")

    Q_PROPERTY(int captureDurationSeconds READ captureDurationSeconds NOTIFY captureDurationSecondsChanged)
    Q_PROPERTY(QString sttModel READ sttModel NOTIFY sttModelChanged)
    Q_PROPERTY(QString sttLanguage READ sttLanguage NOTIFY sttLanguageChanged)
    Q_PROPERTY(QString sttPrompt READ sttPrompt NOTIFY sttPromptChanged)
    Q_PROPERTY(bool vadEnabled READ vadEnabled NOTIFY vadEnabledChanged)
    Q_PROPERTY(double minSpeechSeconds READ minSpeechSeconds NOTIFY minSpeechSecondsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(CapturePresets* presets READ presets CONSTANT)

public:
    // Takes the initial values (already resolved from file + environment by the
    // composition root), the file the configuration is written to, and the
    // borrowed presets model. The file is owned here, so the settings object
    // always has a valid store.
    CaptureSettings(CaptureConfig config, CaptureConfigFile file, CapturePresets* presets, QObject* parent = nullptr);
    ~CaptureSettings() override;

    [[nodiscard]] int captureDurationSeconds() const noexcept;
    [[nodiscard]] QString sttModel() const;
    [[nodiscard]] QString sttLanguage() const;
    [[nodiscard]] QString sttPrompt() const;
    [[nodiscard]] bool vadEnabled() const noexcept;
    [[nodiscard]] double minSpeechSeconds() const noexcept;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] CapturePresets* presets() const noexcept;

    // The single mutating command. Normalises the text fields, validates the
    // whole capture configuration and persists it. Returns false and stores a
    // user-facing reason in lastError when the draft is invalid or cannot be
    // written; the previous configuration file and the stored settings are left
    // untouched in that case. On success only the fields that actually changed
    // emit their *Changed signal; saved() always follows.
    Q_INVOKABLE bool save(int captureDurationSeconds,
                          const QString& sttModel,
                          const QString& sttLanguage,
                          const QString& sttPrompt,
                          bool vadEnabled,
                          double minSpeechSeconds);

    // Clears a previous save error (for example when the user starts editing
    // again). Kept separate so the view can call it on edit without mutating the
    // stored configuration.
    Q_INVOKABLE void clearError();

signals:
    void captureDurationSecondsChanged();
    void sttModelChanged();
    void sttLanguageChanged();
    void sttPromptChanged();
    void vadEnabledChanged();
    void minSpeechSecondsChanged();
    void lastErrorChanged();
    void saved();

private:
    // Applies a configuration that has already been validated and persisted,
    // replacing the stored value as a whole and emitting only the changes that
    // happened.
    void applySavedConfig(const CaptureConfig& config);

    void setLastError(const QString& error);

    CaptureConfig config_;
    CaptureConfigFile file_;
    CapturePresets* presets_;
    QString lastError_;
};
