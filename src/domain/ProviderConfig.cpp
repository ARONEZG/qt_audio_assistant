#include "domain/ProviderConfig.h"

#include <QUrl>

namespace {

// A line break would break the KEY=VALUE record it is written into, so it is
// rejected here rather than silently producing a corrupt configuration file.
[[nodiscard]] bool isSingleLine(const QString& value) {
    return !value.contains(QLatin1Char('\n')) && !value.contains(QLatin1Char('\r'));
}

[[nodiscard]] bool isHttpUrl(const QString& value) {
    const QUrl kUrl(value, QUrl::StrictMode);
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
