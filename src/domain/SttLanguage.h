#pragma once

#include <QProcessEnvironment>
#include <QString>

// Resolves the speech-to-text language from the environment, mirroring the
// STT model (POLZA_STT_MODEL) resolution: a fixed, startup-only value separate
// from the persisted chat-provider settings.
//
//   POLZA_STT_LANGUAGE absent      -> "ru" (the primary language of the app)
//   POLZA_STT_LANGUAGE="" (empty)  -> "" (endpoint autodetects)
//   POLZA_STT_LANGUAGE="en"        -> "en"
//
// The environment is injected (not read from the process here) so the mapping is
// unit-testable without mutating the real environment, exactly like
// ProviderConfigSource takes its environment by reference.
[[nodiscard]] QString sttLanguageFromEnvironment(const QProcessEnvironment& environment);
