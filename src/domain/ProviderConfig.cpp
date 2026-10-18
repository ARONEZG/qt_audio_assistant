#include "domain/ProviderConfig.h"

#include <QUrl>

namespace {

// A line break would break the KEY=VALUE record it is written into, so it is
// rejected here rather than silently producing a corrupt configuration file.
[[nodiscard]] bool isSingleLine(const QString& value) {
    return !value.contains(QLatin1Char('\n')) && !value.contains(QLatin1Char('\r'));
}

[[nodiscard]] bool isHttpUrl(const QString& value) {
    // Tolerant mode accepts hosts StrictMode rejects (for example underscores in
    // internal host names); the explicit scheme + host checks below still reject
    // relative URLs and non-http(s) endpoints.
    const QUrl kUrl(value, QUrl::TolerantMode);
    if (!kUrl.isValid() || kUrl.host().isEmpty()) {
        return false;
    }
    const QString kScheme = kUrl.scheme();
    return kScheme == QStringLiteral("http") || kScheme == QStringLiteral("https");
}

}  // namespace

QString ProviderConfig::validationError() const {
    const QString kUrl = baseUrl.trimmed();
    if (kUrl.isEmpty()) {
        return QStringLiteral("Base URL is required.");
    }
    if (!isSingleLine(kUrl)) {
        return QStringLiteral("Base URL must be a single line.");
    }
    if (!isHttpUrl(kUrl)) {
        return QStringLiteral("Base URL must be an absolute http(s) URL.");
    }

    const QString kModel = model.trimmed();
    if (kModel.isEmpty()) {
        return QStringLiteral("Model is required.");
    }
    if (!isSingleLine(kModel)) {
        return QStringLiteral("Model must be a single line.");
    }
    return {};
}
