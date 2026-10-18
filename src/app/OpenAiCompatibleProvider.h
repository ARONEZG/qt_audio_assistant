#pragma once

#include <QObject>
#include <QString>
#include <map>
#include <vector>

#include "domain/Answer.h"
#include "domain/Message.h"
#include "domain/Provider.h"

namespace LLMQore {
class OpenAIClient;
}  // namespace LLMQore

// Concrete network provider for an OpenAI-compatible chat-completions endpoint
// (the MVP's "Polza AI" is one such endpoint; the URL, model and key are
// configuration, so the class is named for the protocol, not a vendor).
//
// It is an adapter, and nothing else: it wraps the third-party
// LLMQore::OpenAIClient and is the single seam where provider-specific types are
// translated into the domain contract -
//
//   std::vector<Message>  -> LLMQore::Conversation   (respond)
//   LLMQore chunkReceived -> Answer::appendChunk
//   LLMQore requestFinalized -> Answer::complete
//   LLMQore requestFailed -> Answer::fail(ProviderError, detail)
//   Answer::cancelled     -> LLMQore cancelRequest   (real network abort)
//
// No LLMQore type appears in this header or in any other project-owned
// interface; the domain and the UI see only Provider/Answer/Message/ProviderError.
//
// Configuration is injected through the constructor (composition root reads it
// from the environment). A provider with missing configuration is still valid -
// it simply fails each request with a typed error instead of hitting the
// network, so the app can report the misconfiguration instead of crashing.
class OpenAiCompatibleProvider : public Provider {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(OpenAiCompatibleProvider)

public:
    OpenAiCompatibleProvider(QString url, QString apiKey, QString model, QObject* parent = nullptr);
    ~OpenAiCompatibleProvider() override;

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    Answer* respond(const std::vector<Message>& history) override;

    // Overrides the underlying client's transfer timeout. Production keeps the
    // client default; a test uses it to make a stalled request fail promptly.
    void setTransferTimeout(int milliseconds);

private:
    // The Answer currently fed by the given in-flight request, or nullptr when
    // the request already reached a terminal state.
    [[nodiscard]] Answer* answerFor(const QString& requestId) const;
    // Forgets a finished request and disposes of its Answer. It is called only
    // after the Answer has already emitted its terminal signal (per the Provider
    // contract: deliver the terminal state first, destroy afterwards).
    void drop(const QString& requestId);

    QString url_;
    QString apiKey_;
    QString model_;
    LLMQore::OpenAIClient* client_ = nullptr;
    std::map<QString, Answer*> answers_;
};
