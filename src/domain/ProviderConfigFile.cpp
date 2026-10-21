#include "domain/ProviderConfigFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringView>
#include <cstdint>
#include <utility>

namespace {

// The three keys this build owns. The API key is persisted as plain text, so the
// file is written with owner-only permissions.
constexpr auto kBaseUrlKey = QLatin1StringView("POLZA_BASE_URL");
constexpr auto kModelKey = QLatin1StringView("POLZA_MODEL");
constexpr auto kApiKeyKey = QLatin1StringView("POLZA_API_KEY");
constexpr auto kFileHeader = QLatin1StringView(
        "# Audio Assistant provider configuration (KEY=VALUE).\n"
        "# Endpoint, model and API key. The API key is stored in plain text in\n"
        "# this file, which is created with owner-only permissions (0600).\n");

// The API key is a secret, so neither the file nor a directory created for it is
// readable by other local users. 0600 for the file, 0700 for the directory.
constexpr auto kOwnerFilePermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
constexpr auto kOwnerDirectoryPermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;

[[nodiscard]] QString readError(const QString& path, const QString& reason) {
    return QStringLiteral("Cannot read provider configuration '%1': %2").arg(path, reason);
}

[[nodiscard]] QString writeError(const QString& path, const QString& reason) {
    return QStringLiteral("Cannot write provider configuration '%1': %2").arg(path, reason);
}

// Creates the parent directory when needed and makes it owner-only. An existing
// directory is left untouched: its owner may reasonably manage its permissions.
[[nodiscard]] QString prepareConfigDirectory(const QString& path) {
    const QDir kDirectory = QFileInfo(path).absoluteDir();
    if (kDirectory.exists()) {
        return {};
    }
    if (!kDirectory.mkpath(QStringLiteral("."))) {
        return writeError(path, QStringLiteral("cannot create the configuration directory"));
    }
    if (!QFile::setPermissions(kDirectory.absolutePath(), kOwnerDirectoryPermissions)) {
        return writeError(path, QStringLiteral("cannot restrict the configuration directory permissions"));
    }
    return {};
}

// Writes the content through QSaveFile and restricts the committed file to its
// owner. The temporary file receives the permissions before the rename, so the
// destination is never world-readable, even for a moment.
[[nodiscard]] QString writeConfigFile(const QString& path, const QString& content) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return writeError(path, file.errorString());
    }
    if (!file.setPermissions(kOwnerFilePermissions)) {
        return writeError(path, QStringLiteral("cannot restrict the configuration file permissions"));
    }
    const QByteArray kBytes = content.toUtf8();
    if (file.write(kBytes) != kBytes.size()) {
        return writeError(path, file.errorString());
    }
    if (!file.commit()) {
        return writeError(path, file.errorString());
    }
    return {};
}

// Parses one non-comment line. kMalformed means the line has no KEY=VALUE
// separator; the caller turns it into a line-numbered error.
enum class LineResult : std::uint8_t { kIgnored, kApplied, kMalformed };

[[nodiscard]] LineResult parseConfigLine(const QString& line, ProviderConfig& config, QString& apiKey) {
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
    } else if (kKey == kApiKeyKey) {
        apiKey = kValue;
    }
    return LineResult::kApplied;
}

// Splits the KEY=VALUE body and applies each line to config and apiKey. Returns
// an empty string on success and a user-facing error otherwise. Unknown keys are
// ignored so a regular .env the user already has stays usable.
[[nodiscard]] QString parseConfigText(const QString& text,
                                      const QString& path,
                                      ProviderConfig& config,
                                      QString& apiKey) {
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

        if (parseConfigLine(line, config, apiKey) == LineResult::kMalformed) {
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
        return {{}, {}, QStringLiteral("Provider configuration path is empty.")};
    }
    QFile file(path_);
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {{}, {}, readError(path_, file.errorString())};
    }

    ProviderConfig config;
    QString apiKey;
    const QString kError = parseConfigText(QString::fromUtf8(file.readAll()), path_, config, apiKey);
    if (!kError.isEmpty()) {
        return {{}, {}, kError};
    }
    return {config, apiKey, {}};
}

ProviderConfigFile::SaveResult ProviderConfigFile::save(const ProviderConfig& config, const QString& apiKey) const {
    const QString kValidation = config.validationError();
    if (!kValidation.isEmpty()) {
        return {kValidation};
    }
    // Surrounding whitespace is not part of a key in the line-based env format;
    // it is normalised here and on read. Internal line breaks are rejected.
    const QString kKey = apiKey.trimmed();
    if (kKey.contains(QLatin1Char('\n')) || kKey.contains(QLatin1Char('\r'))) {
        return {QStringLiteral("API key must be a single line.")};
    }
    if (path_.isEmpty()) {
        return {QStringLiteral("Provider configuration path is empty.")};
    }

    const QString kDirectoryError = prepareConfigDirectory(path_);
    if (!kDirectoryError.isEmpty()) {
        return {kDirectoryError};
    }

    const QString kContent = QString::fromLatin1(kFileHeader) + QString::fromLatin1(kBaseUrlKey) + QLatin1Char('=') +
                             config.baseUrl.trimmed() + QLatin1Char('\n') + QString::fromLatin1(kModelKey) +
                             QLatin1Char('=') + config.model.trimmed() + QLatin1Char('\n') +
                             QString::fromLatin1(kApiKeyKey) + QLatin1Char('=') + kKey + QLatin1Char('\n');

    return {writeConfigFile(path_, kContent)};
}

const QString& ProviderConfigFile::path() const noexcept {
    return path_;
}
