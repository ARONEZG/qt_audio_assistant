#pragma once

#include <QString>

#include "domain/ProviderConfig.h"

// Env-compatible (KEY=VALUE) persistence for the provider endpoint, model and
// API key.
//
// The path is injected so the composition root decides where user configuration
// lives (QStandardPaths::AppConfigLocation in production, a temporary directory
// in tests) and the class itself never reaches for a hardcoded location.
//
// The API key is persisted as POLZA_API_KEY alongside POLZA_BASE_URL and
// POLZA_MODEL. It is stored in plain text in this user-readable file, so the
// file's permissions should be kept restricted. Unknown keys are ignored on
// read, which keeps the file compatible with a regular .env the user may also
// use with the environment variables.
class ProviderConfigFile {
public:
    // Plain result aggregates: their public members are the whole contract.
    // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
    struct LoadResult {
        ProviderConfig config;
        // Empty when no key was persisted yet. A missing file is not an error:
        // it is simply an unconfigured provider, which the provider reports
        // honestly on use.
        QString apiKey;
        // Empty on success. A missing file is not an error either.
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

    explicit ProviderConfigFile(QString path);

    // Reads the persisted configuration. A missing file yields an empty config
    // and no error; a file that cannot be read or parsed yields an error.
    [[nodiscard]] LoadResult load() const;

    // Writes the configuration, creating the parent directory when needed. The
    // values are validated (see ProviderConfig::validationError and the API key
    // single-line check) before the file is touched, so a rejected save leaves
    // the previous file intact.
    [[nodiscard]] SaveResult save(const ProviderConfig& config, const QString& apiKey) const;

    [[nodiscard]] const QString& path() const noexcept;

private:
    QString path_;
};
