#pragma once

#include <QObject>
#include <QString>
#include <vector>

#include "domain/ProviderError.h"

// Role interface of a speech-to-text backend: it turns one block of captured
// PCM into text, mirroring Provider (the LLM chat role) on the capture side.
//
// Contract: transcribe() returns immediately and must NOT emit a terminal
// signal synchronously - it starts the work and reports the result later from
// the event loop (a network reply, a worker, or a test), exactly like
// Provider::respond and AudioSink::write. Exactly one terminal signal
// (transcribed or failed) is emitted per accepted call.
//
// The parameter is plain canonical PCM - f32, mono, 16 kHz, in [-1, 1], passed
// by value so the caller hands over ownership. It is deliberately not an
// AudioFormat: the format is a capture-layer detail, and keeping it out of the
// signature stops domain code from depending on the audio library. The one
// canonical stream is instead documented in place (mirrors AudioSink::write).
//
// Failures reuse the typed ProviderError so a consumer branches on the kind of
// failure, never on prose.
class TranscriptionProvider : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(TranscriptionProvider)

public:
    using QObject::QObject;
    ~TranscriptionProvider() override;

    virtual void transcribe(std::vector<float> samples) = 0;

signals:
    void transcribed(const QString& text);
    void failed(ProviderError error, const QString& message);
};
