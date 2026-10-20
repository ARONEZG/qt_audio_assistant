#pragma once

#include <QString>

#include "domain/CaptureConfig.h"

// Env-compatible (KEY=VALUE) persistence for the capture and transcription
// configuration, mirroring ProviderConfigFile.
//
// The path is injected so the composition root decides where user
// configuration lives (QStandardPaths::AppConfigLocation in production, a
// temporary directory in tests) and the class itself never reaches for a
// hardcoded location.
//
// The file carries no API key and no endpoint: only the capture window and the
// speech-to-text options are written and read back. Unknown keys are ignored on
// read, so the file stays compatible with a regular .env the user may already
// use with the environment variables.
class CaptureConfigFile {
public:
    // Plain result aggregates: their public members are the whole contract.
    // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
    struct LoadResult {
        CaptureConfig config;
        // Empty on success. A missing file is not an error: it is simply the
        // default configuration, which the application can use as-is.
        QString error;

        [[nodiscard]] bool ok() const noexcept {
            return error.isEmpty();
        }
    };

    struct SaveResult {
        QString error;

        [[nodiscard]] bool ok() const noexcept {
            return error.isEmpty();
        }
    };
    // NOLINTEND(misc-non-private-member-variables-in-classes)

    explicit CaptureConfigFile(QString path);

    // Reads the persisted configuration. A missing file yields the defaults and
    // no error; a file that cannot be read or parsed yields an error.
    [[nodiscard]] LoadResult load() const;

    // Writes the configuration, creating the parent directory when needed. The
    // values are validated (see CaptureConfig::validationError) before the file
    // is touched, so a rejected save leaves the previous file intact.
    [[nodiscard]] SaveResult save(const CaptureConfig& config) const;

    [[nodiscard]] const QString& path() const noexcept;

private:
    QString path_;
};
