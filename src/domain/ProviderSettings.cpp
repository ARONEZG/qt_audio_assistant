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

bool ProviderSettings::isKeyConfigured() const {
    return !apiKey_.trimmed().isEmpty();
}

QString ProviderSettings::model() const {
    return config_.model;
}

QString ProviderSettings::lastError() const {
    return lastError_;
}

bool ProviderSettings::save(const QString& baseUrl, const QString& model, const QString& apiKey) {
    const ProviderConfig kNormalized{baseUrl.trimmed(), model.trimmed()};
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

    applySavedConfig(kNormalized, apiKey);
    return true;
}

void ProviderSettings::applySavedConfig(const ProviderConfig& config, const QString& apiKey) {
    // Replace the stored value as a whole (copy-and-modify rather than
    // field-by-field mutation). The session key is replaced only when the view
    // supplied one; an empty field means the existing key (for example from the
    // environment) stays.
    const QString kNormalizedKey = apiKey.trimmed();
    const bool kWasKeyConfigured = isKeyConfigured();
    const bool kBaseUrlChanged = config_.baseUrl != config.baseUrl;
    const bool kModelChanged = config_.model != config.model;
    config_ = config;
    if (!kNormalizedKey.isEmpty()) {
        apiKey_ = kNormalizedKey;
    }
    setLastError(QString());

    if (kBaseUrlChanged) {
        emit baseUrlChanged();
    }
    if (kModelChanged) {
        emit modelChanged();
    }
    if (kWasKeyConfigured != isKeyConfigured()) {
        emit isKeyConfiguredChanged();
    }
    emit saved();
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
