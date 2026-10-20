#include "domain/CaptureSettings.h"

#include <utility>

CaptureSettings::CaptureSettings(CaptureConfig config, CaptureConfigFile file, CapturePresets* presets, QObject* parent)
    : QObject(parent), config_(std::move(config)), file_(std::move(file)), presets_(presets) {
}

CaptureSettings::~CaptureSettings() = default;

int CaptureSettings::captureDurationSeconds() const noexcept {
    return config_.captureDurationSeconds;
}

QString CaptureSettings::sttModel() const {
    return config_.sttModel;
}

QString CaptureSettings::sttLanguage() const {
    return config_.sttLanguage;
}

QString CaptureSettings::sttPrompt() const {
    return config_.sttPrompt;
}

bool CaptureSettings::vadEnabled() const noexcept {
    return config_.vadEnabled;
}

double CaptureSettings::minSpeechSeconds() const noexcept {
    return config_.minSpeechSeconds;
}

QString CaptureSettings::lastError() const {
    return lastError_;
}

CapturePresets* CaptureSettings::presets() const noexcept {
    return presets_;
}

// NOLINTNEXTLINE(readability-function-size) - the six-argument QML command signature is the contract.
bool CaptureSettings::save(int captureDurationSeconds,
                           const QString& sttModel,
                           const QString& sttLanguage,
                           const QString& sttPrompt,
                           bool vadEnabled,
                           double minSpeechSeconds) {
    const CaptureConfig kNormalized{captureDurationSeconds,
                                    sttModel.trimmed(),
                                    sttLanguage.trimmed(),
                                    sttPrompt.trimmed(),
                                    vadEnabled,
                                    minSpeechSeconds};
    const QString kValidation = kNormalized.validationError();
    if (!kValidation.isEmpty()) {
        setLastError(kValidation);
        return false;
    }

    const CaptureConfigFile::SaveResult kSaved = file_.save(kNormalized);
    if (!kSaved.ok()) {
        setLastError(kSaved.error);
        return false;
    }

    applySavedConfig(kNormalized);
    return true;
}

void CaptureSettings::applySavedConfig(const CaptureConfig& config) {
    // Replace the stored value as a whole (copy-and-modify rather than
    // field-by-field mutation).
    const bool kDurationChanged = config_.captureDurationSeconds != config.captureDurationSeconds;
    const bool kModelChanged = config_.sttModel != config.sttModel;
    const bool kLanguageChanged = config_.sttLanguage != config.sttLanguage;
    const bool kPromptChanged = config_.sttPrompt != config.sttPrompt;
    const bool kVadChanged = config_.vadEnabled != config.vadEnabled;
    const bool kMinSpeechChanged = !qFuzzyCompare(config_.minSpeechSeconds, config.minSpeechSeconds);
    config_ = config;
    setLastError(QString());

    if (kDurationChanged) {
        emit captureDurationSecondsChanged();
    }
    if (kModelChanged) {
        emit sttModelChanged();
    }
    if (kLanguageChanged) {
        emit sttLanguageChanged();
    }
    if (kPromptChanged) {
        emit sttPromptChanged();
    }
    if (kVadChanged) {
        emit vadEnabledChanged();
    }
    if (kMinSpeechChanged) {
        emit minSpeechSecondsChanged();
    }
    emit saved();
}

void CaptureSettings::clearError() {
    setLastError(QString());
}

void CaptureSettings::setLastError(const QString& error) {
    if (lastError_ == error) {
        return;
    }
    lastError_ = error;
    emit lastErrorChanged();
}
