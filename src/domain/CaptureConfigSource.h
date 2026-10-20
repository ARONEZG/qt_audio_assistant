#pragma once

#include <QProcessEnvironment>
#include <QString>

#include "domain/CaptureConfig.h"
#include "domain/CaptureConfigFile.h"

// Resolves the capture and transcription configuration the application starts
// with, mirroring ProviderConfigSource.
//
// Two sources are combined, with the real process environment taking priority
// for the speech-to-text fields over the persisted file: an environment
// variable that is set and non-empty wins for the model, while language and
// prompt keep their historical POLZA_STT_* semantics (an absent language means
// "ru", an explicitly empty language means endpoint autodetection, and an
// absent/empty prompt means no context field). Capture-window fields come only
// from the file (with defaults); there are no environment variables for them.
class CaptureConfigSource {
public:
    // Plain result aggregate: its public members are the whole contract.
    // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
    struct Result {
        CaptureConfig config;
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
    CaptureConfigSource(CaptureConfigFile file, const QProcessEnvironment& environment);

    [[nodiscard]] Result resolve() const;

private:
    CaptureConfigFile file_;
    QProcessEnvironment environment_;
};
