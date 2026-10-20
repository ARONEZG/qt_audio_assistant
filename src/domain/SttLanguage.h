#pragma once

#include <QProcessEnvironment>
#include <QString>

// Resolves the speech-to-text options from the environment, mirroring the STT
// model (POLZA_STT_MODEL) resolution: fixed, startup-only values separate from
// the persisted chat-provider settings.
//
//   POLZA_STT_LANGUAGE absent      -> "ru" (the primary language of the app)
//   POLZA_STT_LANGUAGE="" (empty)  -> "" (endpoint autodetects)
//   POLZA_STT_LANGUAGE="en"        -> "en"
//
//   POLZA_STT_PROMPT absent/empty  -> "" (no prompt field is sent)
//   POLZA_STT_PROMPT="..."         -> "..." verbatim (vocabulary/spelling hint)
//
// The environment is injected (not read from the process here) so the mapping is
// unit-testable without mutating the real environment, exactly like
// ProviderConfigSource takes its environment by reference.
[[nodiscard]] QString sttLanguageFromEnvironment(const QProcessEnvironment& environment);

// Resolves the optional vocabulary/spelling hint for the STT request. An absent
// or explicitly empty variable yields an empty string, which the transcriber
// omits from the payload.
[[nodiscard]] QString sttPromptFromEnvironment(const QProcessEnvironment& environment);
