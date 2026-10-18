#include "app/OpenAiCompatibleTranscriber.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <cstdint>
#include <utility>
#include <vector>

#include "audio/AudioFormat.h"
#include "audio/WavEncoder.h"

namespace {

// The canonical capture stream: 16 kHz, mono, f32. Every request re-encodes the
// samples as 16-bit PCM WAV, the most compatible container the endpoint accepts
// (30 s of canonical audio is ~0.94 MB, ~1.28 MB base64 - well inside the ~15 MB
// body limit). A function, not a global constant: AudioFormat is not trivially
// constructible at compile time, and clazy rejects a namespace-scope non-POD
// static.
audio::AudioFormat canonicalFormat() {
    return {16000U, 1U, audio::SampleFormat::kFloat32};
}

// Polza documents `file` both as raw base64 (the cURL example) and as a data-URL
// (`data:audio/…;base64,…`, the schema description and example). The data-URL
// form is used here: it is the documented canonical shape and it carries the
// container MIME type, so the endpoint does not have to sniff a WAV out of an
// opaque blob. tst_transcriber pins this exact prefix, so a change is visible.
constexpr auto kWavDataUrlPrefix = "data:audio/wav;base64,";

// Maps an HTTP status onto the typed domain error, alongside the chat provider's
// classification so both roles fail the same way.
ProviderError classifyHttpStatus(int status) {
    switch (status) {
        case 401:
        case 403:
            return ProviderError::kAuthentication;
        case 429:
            return ProviderError::kRateLimit;
        case 400:
        case 413:
        case 422:
            return ProviderError::kInvalidResponse;
        default:
            return ProviderError::kNetwork;
    }
}

QString httpErrorMessage(int status, const QByteArray& body) {
    return QStringLiteral("HTTP %1: %2").arg(status).arg(QString::fromUtf8(body).left(200));
}

}  // namespace

OpenAiCompatibleTranscriber::OpenAiCompatibleTranscriber(
        QString baseUrl, QString apiKey, QString model, int transferTimeoutMs, QObject* parent)
    : TranscriptionProvider(parent),
      baseUrl_(std::move(baseUrl)),
      apiKey_(std::move(apiKey)),
      model_(std::move(model)),
      network_(new QNetworkAccessManager(this)) {
    if (transferTimeoutMs > 0) {
        network_->setTransferTimeout(transferTimeoutMs);
    }
}

OpenAiCompatibleTranscriber::~OpenAiCompatibleTranscriber() {
    // Detach from the reply before aborting: the class is going away, so a
    // finished() callback into onReplyFinished() must not run during
    // destruction. The reply itself is owned by network_ and is destroyed with
    // the manager.
    if (reply_ != nullptr) {
        QObject::disconnect(reply_, nullptr, this, nullptr);
        reply_->abort();
        reply_ = nullptr;
    }
}

void OpenAiCompatibleTranscriber::transcribe(std::vector<float> samples) {
    if (baseUrl_.isEmpty() || model_.isEmpty()) {
        scheduleFailure(ProviderError::kUnknown, QStringLiteral("Transcriber is not configured: set POLZA_BASE_URL"));
        return;
    }
    if (apiKey_.isEmpty()) {
        scheduleFailure(ProviderError::kAuthentication, QStringLiteral("API key is not configured: set POLZA_API_KEY"));
        return;
    }

    // A single request is in flight at a time; a newer call supersedes the
    // previous one instead of racing it. The superseded caller is told from the
    // event loop (scheduleFailure), so transcribe() still never emits a terminal
    // signal synchronously - even when a failed() slot re-enters transcribe().
    if (abortActive()) {
        scheduleFailure(ProviderError::kCancelled, QStringLiteral("Transcription superseded by a newer request"));
    }

    const std::vector<std::uint8_t> kWav = audio::encodeWavPcm16(canonicalFormat(), samples);
    const QByteArray kFile =
            QByteArray(kWavDataUrlPrefix) +
            QByteArray(reinterpret_cast<const char*>(kWav.data()), static_cast<qsizetype>(kWav.size())).toBase64();

    const QJsonObject kPayload{
            {QStringLiteral("model"), model_},
            {QStringLiteral("file"), QString::fromLatin1(kFile)},
            {QStringLiteral("response_format"), QStringLiteral("json")},
    };

    QNetworkRequest request{QUrl(transcribeUrl())};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + apiKey_.toUtf8());
    reply_ = network_->post(request, QJsonDocument(kPayload).toJson(QJsonDocument::Compact));
    connect(reply_, &QNetworkReply::finished, this, &OpenAiCompatibleTranscriber::onReplyFinished);
}

void OpenAiCompatibleTranscriber::cancel() {
    // Synchronous, like Answer::cancel(): the caller asked for the abort now.
    if (abortActive()) {
        emit failed(ProviderError::kCancelled, QStringLiteral("Transcription cancelled"));
    }
}

void OpenAiCompatibleTranscriber::scheduleFailure(ProviderError error, const QString& message) {
    QTimer::singleShot(0, this, [this, error, message] { emit failed(error, message); });
}

bool OpenAiCompatibleTranscriber::abortActive() {
    QNetworkReply* reply = reply_;
    if (reply == nullptr) {
        return false;
    }
    reply_ = nullptr;
    QObject::disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
    return true;
}

void OpenAiCompatibleTranscriber::onReplyFinished() {
    QNetworkReply* reply = reply_;
    if (reply == nullptr) {
        return;
    }
    reply_ = nullptr;
    reply->deleteLater();

    const int kStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray kBody = reply->readAll();

    // A transport failure leaves the HTTP status unset; the reply's own error
    // string is the most precise detail available.
    if (kStatus == 0) {
        emit failed(ProviderError::kNetwork, reply->errorString());
        return;
    }
    if (kStatus < 200 || kStatus >= 300) {
        emit failed(classifyHttpStatus(kStatus), httpErrorMessage(kStatus, kBody));
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument kDocument = QJsonDocument::fromJson(kBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !kDocument.isObject()) {
        emit failed(ProviderError::kInvalidResponse,
                    QStringLiteral("Malformed transcription response: ") + parseError.errorString());
        return;
    }
    const QJsonValue kText = kDocument.object().value(QStringLiteral("text"));
    if (!kText.isString()) {
        emit failed(ProviderError::kInvalidResponse,
                    QStringLiteral("Transcription response is missing the 'text' field"));
        return;
    }
    emit transcribed(kText.toString());
}

QString OpenAiCompatibleTranscriber::transcribeUrl() const {
    QString base = baseUrl_;
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    return base + QStringLiteral("/audio/transcriptions");
}
