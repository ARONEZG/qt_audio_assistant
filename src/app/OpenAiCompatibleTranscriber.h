#pragma once

#include <QObject>
#include <QString>
#include <vector>

#include "domain/ProviderError.h"
#include "domain/TranscriptionProvider.h"

class QNetworkAccessManager;
class QNetworkReply;
class QByteArray;

// Concrete transcription provider for an OpenAI-compatible speech-to-text
// endpoint (the MVP's "Polza AI" is one such endpoint; the URL, model and key
// are configuration, so the class is named for the protocol, not a vendor).
//
// Request shape: JSON with a base64 data-URL `file`. That is the schema Polza
// documents for /audio/transcriptions. A stock OpenAI endpoint expects
// multipart/form-data instead, so pointing this adapter at one would require a
// multipart mode in the request builder (the role interface would not change).
//
// It is an adapter and nothing else: it is the single seam where a captured PCM
// block becomes an HTTP request and an HTTP response becomes domain signals -
//
//   std::vector<float>  -> encodeWavPcm16 -> base64 -> JSON POST
//   200 + {"text": ...} -> transcribed(text)
//   4xx/5xx / transport -> failed(ProviderError, detail)
//   cancel()            -> abort in-flight reply + failed(kCancelled)
//
// No audio type (beyond the STL vector the interface already uses) and no
// network type appears in the domain contract; the domain sees only
// TranscriptionProvider/ProviderError.
//
// The speech-to-text model is a separate concern from the chat model
// (POLZA_MODEL): it is a constructor parameter with an explicit default of
// openai/whisper-1. Wiring an environment variable for it belongs to the
// composition increment, not to this adapter.
//
// Configuration is injected through the constructor. A transcriber with missing
// configuration is still valid - it fails each request with a typed error
// instead of hitting the network, so the app can report the misconfiguration
// instead of crashing (mirrors OpenAiCompatibleProvider).
class OpenAiCompatibleTranscriber : public TranscriptionProvider {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(OpenAiCompatibleTranscriber)

public:
    // transferTimeoutMs > 0 overrides QNetworkAccessManager's transfer timeout
    // for this transcriber (a stalled endpoint must still produce exactly one
    // terminal signal); <= 0 keeps the manager's default.
    explicit OpenAiCompatibleTranscriber(QString baseUrl,
                                         QString apiKey,
                                         QString model = QStringLiteral("openai/whisper-1"),
                                         int transferTimeoutMs = 0,
                                         QString language = QStringLiteral("ru"),
                                         QObject* parent = nullptr);
    ~OpenAiCompatibleTranscriber() override;

    void transcribe(std::vector<float> samples) override;

    // Updates the endpoint and key used by subsequent requests. Unlike the chat
    // provider (rebuilt from live settings on each mint), this adapter is built
    // once at startup; the composition root calls this when the user saves
    // provider settings, so a key entered in the settings panel - a session
    // secret that is never persisted - reaches transcription. The model stays
    // fixed: it is the speech-to-text model (POLZA_STT_MODEL), a separate
    // concern from the chat model held in the provider settings.
    void setConfiguration(QString baseUrl, QString apiKey);

    // Aborts the in-flight request (if any) and reports it as
    // failed(ProviderError::kCancelled). Synchronous, like Answer::cancel(); a
    // call with no request in flight is a no-op. It is optional: a consumer
    // that never cancels simply drops the transcriber and the request dies with
    // it.
    void cancel();

private:
    // Fails from the event loop so transcribe() never emits synchronously (the
    // TranscriptionProvider contract).
    void scheduleFailure(ProviderError error, const QString& message);
    // Aborts and disposes of the active reply (if any). Returns true when a
    // reply was in flight. It deliberately does not emit: cancel() reports the
    // abandonment synchronously, while a superseding transcribe() schedules the
    // failure so it still never emits inside its own call.
    [[nodiscard]] bool abortActive();
    void onReplyFinished();
    // Parses a 2xx response body and emits exactly one terminal signal
    // (transcribed, or failed for a malformed/missing-text body). Kept separate
    // from onReplyFinished so each method stays small and single-purpose.
    void emitResult(const QByteArray& body);

    [[nodiscard]] QString transcribeUrl() const;

    QString baseUrl_;
    QString apiKey_;
    QString model_;
    QString language_;
    QNetworkAccessManager* network_;
    QNetworkReply* reply_ = nullptr;
};
