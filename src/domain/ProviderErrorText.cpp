#include "domain/ProviderErrorText.h"

std::optional<QString> providerFailureText(ProviderError error, const QString& detail) {
    if (error == ProviderError::kCancelled) {
        return std::nullopt;
    }

    QString summary = QStringLiteral("Unexpected error");
    if (error == ProviderError::kNetwork) {
        summary = QStringLiteral("Network error");
    } else if (error == ProviderError::kAuthentication) {
        summary = QStringLiteral("Authentication failed");
    } else if (error == ProviderError::kRateLimit) {
        summary = QStringLiteral("Rate limit exceeded");
    } else if (error == ProviderError::kInvalidResponse) {
        summary = QStringLiteral("Invalid response from provider");
    }

    if (detail.isEmpty()) {
        return summary;
    }
    return QStringLiteral("%1: %2").arg(summary, detail);
}
