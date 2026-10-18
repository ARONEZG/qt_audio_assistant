#pragma once

#include <QString>
#include <optional>

#include "domain/ProviderError.h"

// Turns a typed provider failure into one user-facing line: a short summary of
// the failure kind plus the provider's detail (HTTP status, network text) when it
// carries information. Cancellation is not a failure - kCancelled yields no text
// - so a cancelled request can never surface as an error.
//
// Shared by the chat (a failed reply) and the capture facade (a failed
// transcription), so both report the same wording for the same error.
[[nodiscard]] std::optional<QString> providerFailureText(ProviderError error, const QString& detail);
