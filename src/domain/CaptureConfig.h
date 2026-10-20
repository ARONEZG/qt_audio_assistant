#pragma once

#include <QString>

// The persisted capture and transcription configuration: how much audio a
// transcription request covers, which speech-to-text model/language/context is
// used, and how aggressive the local voice-activity filtering is.
//
// It is a plain aggregate value type, like ProviderConfig: the members are the
// whole public contract and carry no invariants of their own. Empty values are
// legitimate editing states (language empty means endpoint autodetection,
// prompt empty means no context is sent), so validation is an explicit query
// the save path calls before persisting anything.
// NOLINTBEGIN(misc-non-private-member-variables-in-classes)
struct CaptureConfig {
    // The trailing part of the ring buffer sent to the transcriber.
    int captureDurationSeconds = 20;
    QString sttModel = QStringLiteral("openai/whisper-1");
    // Empty means "let the endpoint autodetect the language".
    QString sttLanguage = QStringLiteral("ru");
    // Optional vocabulary/spelling hint; empty means the field is omitted.
    QString sttPrompt;
    bool vadEnabled = true;
    // Speech shorter than this is rejected locally instead of being sent.
    double minSpeechSeconds = 0.75;

    // Empty when the configuration can be persisted and used; otherwise a
    // user-facing reason (out-of-range numbers, an empty model, or line breaks
    // that would corrupt the KEY=VALUE file format).
    [[nodiscard]] QString validationError() const;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)
