#include "domain/CaptureConfig.h"

#include <cmath>

namespace {

constexpr int kMinCaptureSeconds = 5;
constexpr int kMaxCaptureSeconds = 120;
constexpr double kMinSpeechSeconds = 0.25;
constexpr double kMaxSpeechSeconds = 3.0;

// A line break would break the KEY=VALUE record it is written into, so it is
// rejected here rather than silently producing a corrupt configuration file.
[[nodiscard]] bool isSingleLine(const QString& value) {
    return !value.contains(QLatin1Char('\n')) && !value.contains(QLatin1Char('\r'));
}

}  // namespace

QString CaptureConfig::validationError() const {
    if (captureDurationSeconds < kMinCaptureSeconds || captureDurationSeconds > kMaxCaptureSeconds) {
        return QStringLiteral("Capture duration must be between 5 and 120 seconds.");
    }

    const QString kModel = sttModel.trimmed();
    if (kModel.isEmpty()) {
        return QStringLiteral("Speech-to-text model is required.");
    }
    if (!isSingleLine(kModel)) {
        return QStringLiteral("Speech-to-text model must be a single line.");
    }
    if (!isSingleLine(sttLanguage)) {
        return QStringLiteral("Speech-to-text language must be a single line.");
    }
    if (!isSingleLine(sttPrompt)) {
        return QStringLiteral("Context must be a single line.");
    }
    // An explicit finite check keeps a NaN from slipping through the range
    // comparison (it compares false against both bounds).
    if (!std::isfinite(minSpeechSeconds) || minSpeechSeconds < kMinSpeechSeconds ||
        minSpeechSeconds > kMaxSpeechSeconds) {
        return QStringLiteral("Minimum speech duration must be between 0.25 and 3.0 seconds.");
    }
    return {};
}
