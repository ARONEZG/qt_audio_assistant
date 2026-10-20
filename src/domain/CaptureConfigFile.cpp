#include "domain/CaptureConfigFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringView>
#include <cstdint>
#include <utility>

namespace {

// The six keys this build owns. The endpoint and the API key deliberately stay
// in provider.env; these settings are about capturing and transcribing audio.
constexpr auto kModelKey = QLatin1StringView("POLZA_STT_MODEL");
constexpr auto kLanguageKey = QLatin1StringView("POLZA_STT_LANGUAGE");
constexpr auto kPromptKey = QLatin1StringView("POLZA_STT_PROMPT");
constexpr auto kCaptureSecondsKey = QLatin1StringView("AA_CAPTURE_SECONDS");
constexpr auto kVadEnabledKey = QLatin1StringView("AA_VAD_ENABLED");
constexpr auto kMinSpeechSecondsKey = QLatin1StringView("AA_MIN_SPEECH_SECONDS");
constexpr auto kFileHeader = QLatin1StringView(
        "# Audio Assistant capture and transcription configuration (KEY=VALUE).\n"
        "# Speech-to-text model/language/context and the capture window.\n"
        "# Numbers use '.' as the decimal separator; unknown keys are ignored.\n");

[[nodiscard]] QString readError(const QString& path, const QString& reason) {
    return QStringLiteral("Cannot read capture configuration '%1': %2").arg(path, reason);
}

[[nodiscard]] QString writeError(const QString& path, const QString& reason) {
    return QStringLiteral("Cannot write capture configuration '%1': %2").arg(path, reason);
}

// Parses one non-comment line. kMalformed means the line has no KEY=VALUE
// separator; kInvalidValue means a known numeric/boolean key carries a value
// that cannot be parsed. The caller turns both into line-numbered errors.
enum class LineResult : std::uint8_t { kIgnored, kApplied, kMalformed, kInvalidValue };

// Applies the text-valued keys. Unknown keys are ignored so a regular .env the
// user already has stays usable.
[[nodiscard]] LineResult applyTextKeys(const QString& key, const QString& value, CaptureConfig& config) {
    if (key == kModelKey) {
        config.sttModel = value;
        return LineResult::kApplied;
    }
    if (key == kLanguageKey) {
        config.sttLanguage = value;
        return LineResult::kApplied;
    }
    if (key == kPromptKey) {
        config.sttPrompt = value;
        return LineResult::kApplied;
    }
    return LineResult::kIgnored;
}

// Applies the numeric/boolean keys. A known key with an unparseable value is
// reported as kInvalidValue, never silently ignored.
[[nodiscard]] LineResult applyCaptureKeys(const QString& key, const QString& value, CaptureConfig& config) {
    if (key == kCaptureSecondsKey) {
        bool ok = false;
        const int kSeconds = value.toInt(&ok);
        if (!ok) {
            return LineResult::kInvalidValue;
        }
        config.captureDurationSeconds = kSeconds;
        return LineResult::kApplied;
    }
    if (key == kVadEnabledKey) {
        if (value == QLatin1StringView("true")) {
            config.vadEnabled = true;
            return LineResult::kApplied;
        }
        if (value == QLatin1StringView("false")) {
            config.vadEnabled = false;
            return LineResult::kApplied;
        }
        return LineResult::kInvalidValue;
    }
    if (key == kMinSpeechSecondsKey) {
        bool ok = false;
        const double kSeconds = value.toDouble(&ok);
        if (!ok) {
            return LineResult::kInvalidValue;
        }
        config.minSpeechSeconds = kSeconds;
        return LineResult::kApplied;
    }
    return LineResult::kIgnored;
}

[[nodiscard]] LineResult parseConfigLine(const QString& line, CaptureConfig& config) {
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
    const LineResult kTextResult = applyTextKeys(kKey, kValue, config);
    if (kTextResult != LineResult::kIgnored) {
        return kTextResult;
    }
    return applyCaptureKeys(kKey, kValue, config);
}

// Splits the KEY=VALUE body and applies each line to config. Returns an empty
// string on success and a user-facing error otherwise. Unknown keys are ignored
// so a regular .env the user already has stays usable.
[[nodiscard]] QString parseConfigText(const QString& text, const QString& path, CaptureConfig& config) {
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

        const LineResult kResult = parseConfigLine(line, config);
        if (kResult == LineResult::kMalformed) {
            return QStringLiteral("Malformed capture configuration line %1 in '%2': expected KEY=VALUE.")
                    .arg(lineNumber)
                    .arg(path);
        }
        if (kResult == LineResult::kInvalidValue) {
            return QStringLiteral("Invalid capture configuration value on line %1 in '%2'.").arg(lineNumber).arg(path);
        }
        if (kNewline < 0) {
            break;
        }
        lineStart = kNewline + 1;
    }
    return {};
}

}  // namespace

CaptureConfigFile::CaptureConfigFile(QString path) : path_(std::move(path)) {
}

CaptureConfigFile::LoadResult CaptureConfigFile::load() const {
    if (path_.isEmpty()) {
        return {{}, QStringLiteral("Capture configuration path is empty.")};
    }
    QFile file(path_);
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {{}, readError(path_, file.errorString())};
    }

    CaptureConfig config;
    const QString kError = parseConfigText(QString::fromUtf8(file.readAll()), path_, config);
    if (!kError.isEmpty()) {
        return {{}, kError};
    }
    return {config, {}};
}

CaptureConfigFile::SaveResult CaptureConfigFile::save(const CaptureConfig& config) const {
    const QString kValidation = config.validationError();
    if (!kValidation.isEmpty()) {
        return {kValidation};
    }
    if (path_.isEmpty()) {
        return {QStringLiteral("Capture configuration path is empty.")};
    }

    const QDir kDirectory = QFileInfo(path_).absoluteDir();
    if (!kDirectory.exists() && !kDirectory.mkpath(QStringLiteral("."))) {
        return {writeError(path_, QStringLiteral("cannot create the configuration directory"))};
    }

    // QString::number(double) is locale-independent (always '.'), so the file
    // reads back the same on every platform and locale.
    const QString kContent = QString::fromLatin1(kFileHeader) + QString::fromLatin1(kModelKey) + QLatin1Char('=') +
                             config.sttModel.trimmed() + QLatin1Char('\n') + QString::fromLatin1(kLanguageKey) +
                             QLatin1Char('=') + config.sttLanguage.trimmed() + QLatin1Char('\n') +
                             QString::fromLatin1(kPromptKey) + QLatin1Char('=') + config.sttPrompt.trimmed() +
                             QLatin1Char('\n') + QString::fromLatin1(kCaptureSecondsKey) + QLatin1Char('=') +
                             QString::number(config.captureDurationSeconds) + QLatin1Char('\n') +
                             QString::fromLatin1(kVadEnabledKey) + QLatin1Char('=') +
                             (config.vadEnabled ? QStringLiteral("true") : QStringLiteral("false")) +
                             QLatin1Char('\n') + QString::fromLatin1(kMinSpeechSecondsKey) + QLatin1Char('=') +
                             QString::number(config.minSpeechSeconds) + QLatin1Char('\n');

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

const QString& CaptureConfigFile::path() const noexcept {
    return path_;
}
