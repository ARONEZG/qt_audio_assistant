#pragma once

#include <QProcessEnvironment>
#include <QString>

#include "domain/ProviderConfig.h"
#include "domain/ProviderConfigFile.h"

// Resolves the provider configuration the application starts with.
//
// Two sources are combined, with the real process environment taking priority
// per value over the persisted file: an environment variable that is set and
// non-empty wins; a variable that is absent or blank falls back to the file.
// This is what lets an operator run the binary with POLZA_BASE_URL=… without
// touching the user's saved configuration.
//
// The API key is resolved like the other values: the file provides the default,
// and a non-empty POLZA_API_KEY environment variable overrides it for the run.
class ProviderConfigSource {
public:
    // Plain result aggregate: its public members are the whole contract.
    // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
    struct Result {
        ProviderConfig config;
        QString apiKey;
        // Empty on success. A missing configuration file is not an error; a
        // file that cannot be read or parsed is, and the caller decides whether
        // to warn (the application still starts with whatever it has).
        QString error;

        [[nodiscard]] bool ok() const noexcept {
            return error.isEmpty();
        }
    };
    // NOLINTEND(misc-non-private-member-variables-in-classes)

    // QProcessEnvironment is copyable but not movable, so the environment is
    // taken by const reference and copied into the source.
    ProviderConfigSource(ProviderConfigFile file, const QProcessEnvironment& environment);

    [[nodiscard]] Result resolve() const;

private:
    ProviderConfigFile file_;
    QProcessEnvironment environment_;
};
