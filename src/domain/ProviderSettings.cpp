#include "domain/ProviderSettings.h"

#include <utility>

ProviderSettings::ProviderSettings(ProviderConfig config, QString apiKey, ProviderConfigFile file, QObject* parent)
    : QObject(parent), config_(std::move(config)), apiKey_(std::move(apiKey)), file_(std::move(file)) {
}

ProviderSettings::~ProviderSettings() = default;

QString ProviderSettings::baseUrl() const {
    return config_.baseUrl;
}

QString ProviderSettings::apiKey() const {
    return apiKey_;
}

QString ProviderSettings::model() const {
    return config_.model;
}

QString ProviderSettings::lastError() const {
    return lastError_;
}

void ProviderSettings::setBaseUrl(QString baseUrl) {
    if (config_.baseUrl == baseUrl) {
        return;
    }
    config_.baseUrl = std::move(baseUrl);
    clearError();
    emit baseUrlChanged();
}

void ProviderSettings::setApiKey(QString apiKey) {
    if (apiKey_ == apiKey) {
        return;
    }
    apiKey_ = std::move(apiKey);
    clearError();
    emit apiKeyChanged();
}

void ProviderSettings::setModel(QString model) {
    if (config_.model == model) {
        return;
    }
    config_.model = std::move(model);
    clearError();
    emit modelChanged();
}

bool ProviderSettings::save() {
    // An API key is required for a usable session, so an empty one is reported
    // rather than silently producing a provider that always fails
    // authentication. The key itself is never persisted.
    const QString kKey = apiKey_.trimmed();
    if (kKey.isEmpty()) {
        setLastError(QStringLiteral("API key is required for the chat session."));
        return false;
    }

    const ProviderConfig kNormalized{config_.baseUrl.trimmed(), config_.model.trimmed()};
    const QString kValidation = kNormalized.validationError();
    if (!kValidation.isEmpty()) {
        setLastError(kValidation);
        return false;
    }

    const ProviderConfigFile::SaveResult kSaved = file_.save(kNormalized);
    if (!kSaved.ok()) {
        setLastError(kSaved.error);
        return false;
    }

    // Reflect normalisation in the live settings as well, so the provider the
    // next chat mints uses exactly what was written.
    setApiKey(kKey);
    setBaseUrl(kNormalized.baseUrl);
    setModel(kNormalized.model);
    setLastError(QString());
    emit saved();
    return true;
}

void ProviderSettings::clearError() {
    setLastError(QString());
}

void ProviderSettings::setLastError(const QString& error) {
    if (lastError_ == error) {
        return;
    }
    lastError_ = error;
    emit lastErrorChanged();
}
