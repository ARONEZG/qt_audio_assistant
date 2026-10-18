#pragma once

#include <QString>

// The persisted part of an OpenAI-compatible provider configuration: the
// endpoint and the model. The API key is deliberately not a member - it is a
// session secret that is read from the environment or entered by the user and
// is never written to disk.
//
// An empty value is a valid "not configured yet" state for an object that is
// being edited, so validation is an explicit query rather than a constructor
// precondition. The save paths call validationError() and report a non-empty
// result to the user instead of persisting broken configuration.
//
// A plain aggregate value type: its two members are the entire public contract
// and carry no invariants of their own.
// NOLINTBEGIN(misc-non-private-member-variables-in-classes)
struct ProviderConfig {
    QString baseUrl;
    QString model;

    // Empty when the configuration can be persisted and used; otherwise a
    // user-facing reason (empty/blank values, a non-http(s) URL, line breaks
    // that would corrupt the KEY=VALUE file format).
    [[nodiscard]] QString validationError() const;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)
