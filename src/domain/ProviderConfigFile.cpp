#include "domain/ProviderConfigFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringView>
#include <cstdint>
#include <utility>

namespace {

// The two keys this build owns. The API key is intentionally absent.
constexpr auto kBaseUrlKey = QLatin1StringView("POLZA_BASE_URL");
constexpr auto kModelKey = QLatin1StringView("POLZA_MODEL");
constexpr auto kFileHeader = QLatin1StringView(
        "# Audio Assistant provider configuration (KEY=VALUE).\n"
        "# Endpoint and model only. The API key is a session secret and is\n"
        "# never stored here; provide it in the environment or in the app.\n");

[[nodiscard]] QString readError(const QString& path, const QString& reason) {
    return QStringLiteral("Cannot read provider configuration '%1': %2").arg(path, reason);
}

[[nodiscard]] QString writeError(const QString& path, const QString& reason) {
    return QStringLiteral("Cannot write provider configuration '%1': %2").arg(path, reason);
}

// Parses one non-comment line. kMalformed means the line has no KEY=VALUE
// separator; the caller turns it into a line-numbered error.
enum class LineResult : std::uint8_t { kIgnored, kApplied, kMalformed };

[[nodiscard]] LineResult parseConfigLine(const QString& line, ProviderConfig& config) {
    const QString kTrimmed = line.trimmed();
    if (kTrimmed.isEmpty() || kTrimmed.startsWith(QLatin1Char('#'))) {
        return LineResult::kIgnored;
    }
    const qsizetype kEquals = kTrimmed.indexOf(QLatin1Char('='));
    if (kEquals <= 0) {
        return LineResult::kMalformed;
    }
    const QString kKey = kTrimmed.left(kEquals).trimmed();
    const QString kValue = kTrimmed.mid(kEquals + 1).trimmed();
    if (kKey == kBaseUrlKey) {
        config.baseUrl = kValue;
    } else if (kKey == kModelKey) {
        config.model = kValue;
    }
    return LineResult::kApplied;
}

// Splits the KEY=VALUE body and applies each line to config. Returns an empty
// string on success and a user-facing error otherwise. Unknown keys are ignored
// so a regular .env the user already has stays usable.
[[nodiscard]] QString parseConfigText(const QString& text, const QString& path, ProviderConfig& config) {
    qsizetype lineStart = 0;
    int lineNumber = 0;
    while (lineStart <= text.size()) {
        const qsizetype kNewline = text.indexOf(QLatin1Char('\n'), lineStart);
        const qsizetype kLineEnd = kNewline < 0 ? text.size() : kNewline;
        QString line = text.mid(lineStart, kLineEnd - lineStart);
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        ++lineNumber;

        if (parseConfigLine(line, config) == LineResult::kMalformed) {
            return QStringLiteral("Malformed provider configuration line %1 in '%2': expected KEY=VALUE.")
                    .arg(lineNumber)
                    .arg(path);
        }
        if (kNewline < 0) {
            break;
        }
        lineStart = kNewline + 1;
    }
    return {};
}

}  // namespace

ProviderConfigFile::ProviderConfigFile(QString path) : path_(std::move(path)) {
}

ProviderConfigFile::LoadResult ProviderConfigFile::load() const {
    if (path_.isEmpty()) {
        return {{}, QStringLiteral("Provider configuration path is empty.")};
    }
    QFile file(path_);
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {{}, readError(path_, file.errorString())};
    }

    ProviderConfig config;
    const QString kError = parseConfigText(QString::fromUtf8(file.readAll()), path_, config);
    if (!kError.isEmpty()) {
        return {{}, kError};
    }
    return {config, {}};
}

ProviderConfigFile::SaveResult ProviderConfigFile::save(const ProviderConfig& config) const {
    const QString kValidation = config.validationError();
    if (!kValidation.isEmpty()) {
        return {kValidation};
    }
    if (path_.isEmpty()) {
        return {QStringLiteral("Provider configuration path is empty.")};
    }

    const QDir kDirectory = QFileInfo(path_).absoluteDir();
    if (!kDirectory.exists() && !kDirectory.mkpath(QStringLiteral("."))) {
        return {writeError(path_, QStringLiteral("cannot create the configuration directory"))};
    }

    const QString kContent = QString::fromLatin1(kFileHeader) + QString::fromLatin1(kBaseUrlKey) + QLatin1Char('=') +
                             config.baseUrl.trimmed() + QLatin1Char('\n') + QString::fromLatin1(kModelKey) +
                             QLatin1Char('=') + config.model.trimmed() + QLatin1Char('\n');

    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return {writeError(path_, file.errorString())};
    }
    const QByteArray kBytes = kContent.toUtf8();
    if (file.write(kBytes) != kBytes.size()) {
        return {writeError(path_, file.errorString())};
    }
    if (!file.commit()) {
        return {writeError(path_, file.errorString())};
    }
    return {};
}

const QString& ProviderConfigFile::path() const noexcept {
    return path_;
}
