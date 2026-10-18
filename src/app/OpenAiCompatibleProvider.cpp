#include "app/OpenAiCompatibleProvider.h"

#include <LLMQore/OpenAIClient.hpp>
#include <QTimer>
#include <utility>

namespace {

constexpr auto kHttpPrefix = QLatin1StringView("HTTP ");

// Collapses every system turn into the single system prompt the
// chat-completions API expects (joined with a blank line). A free helper keeps
// toConversation() flat.
QString systemPrompt(const std::vector<Message>& history) {
    QString system;
    for (const Message& message : history) {
        if (message.role() != MessageRole::kSystem) {
            continue;
        }
        if (!system.isEmpty()) {
            system.append(QStringLiteral("\n\n"));
        }
        system.append(message.text());
    }
    return system;
}

// Builds the request payload from our history. System turns become the system
// prompt; user and assistant turns keep their order. This is the only place our
// Message becomes an LLMQore value, so the translation stays inside the adapter.
LLMQore::Conversation toConversation(const std::vector<Message>& history) {
    LLMQore::Conversation conversation;
    for (const Message& message : history) {
        if (message.role() == MessageRole::kUser) {
            conversation.addUser(message.text());
        } else if (message.role() == MessageRole::kAssistant) {
            conversation.addAssistant(message.text());
        }
    }
    const QString kSystem = systemPrompt(history);
    if (!kSystem.isEmpty()) {
        conversation.setSystem(kSystem);
    }
    return conversation;
}

// Classifies an LLMQore failure message as a typed ProviderError. LLMQore's
// public requestFailed(id, QString) carries only the message text - the typed
// HttpTransportError is converted to a string inside the library - so string
// classification is the only seam available. LLMQore prefixes HTTP failures with
// "HTTP <code>" (BaseClient::parseHttpError) and carries the QNetworkReply error
// string otherwise; every mapping below is pinned by tst_openai_provider.
ProviderError classifyFailure(const QString& detail) {
    if (detail.startsWith(kHttpPrefix)) {
        int code = 0;
        for (qsizetype i = kHttpPrefix.size(); i < detail.size() && detail.at(i).isDigit(); ++i) {
            code = code * 10 + detail.at(i).digitValue();
        }
        switch (code) {
            case 401:
            case 403:
                return ProviderError::kAuthentication;
            case 429:
                return ProviderError::kRateLimit;
            case 400:
            case 422:
                return ProviderError::kInvalidResponse;
            default:
                return ProviderError::kNetwork;
        }
    }

    if (detail.contains(QLatin1StringView("cancelled"), Qt::CaseInsensitive)) {
        return ProviderError::kCancelled;
    }
    if (detail.contains(QLatin1StringView("invalid json"), Qt::CaseInsensitive) ||
        detail.contains(QLatin1StringView("malformed"), Qt::CaseInsensitive) ||
        detail.contains(QLatin1StringView("empty choices"), Qt::CaseInsensitive)) {
        return ProviderError::kInvalidResponse;
    }
    if (detail.contains(QLatin1StringView("timed out"), Qt::CaseInsensitive) ||
        detail.contains(QLatin1StringView("timeout"), Qt::CaseInsensitive)
        // Qt reports a transfer timeout as OperationCanceledError with the text
        // "Operation canceled" (single l), so it must be classified before any
        // generic cancellation handling.
        || detail.contains(QLatin1StringView("operation canceled"), Qt::CaseInsensitive) ||
        detail.contains(QLatin1StringView("connection"), Qt::CaseInsensitive) ||
        detail.contains(QLatin1StringView("network"), Qt::CaseInsensitive) ||
        detail.contains(QLatin1StringView("host"), Qt::CaseInsensitive)) {
        return ProviderError::kNetwork;
    }
    return ProviderError::kUnknown;
}

// Fails an Answer from the event loop, so respond() never drives it
// synchronously (the Provider contract).
void failLater(Answer* answer, ProviderError error, const QString& detail) {
    QTimer::singleShot(0, answer, [answer, error, detail] { answer->fail(error, detail); });
}

}  // namespace

OpenAiCompatibleProvider::OpenAiCompatibleProvider(
        QString url, QString apiKey, QString model, int transferTimeoutMs, QObject* parent)
    : Provider(parent),
      url_(std::move(url)),
      apiKey_(std::move(apiKey)),
      model_(std::move(model)),
      client_(new LLMQore::OpenAIClient(url_, apiKey_, model_, this)) {
    if (transferTimeoutMs > 0) {
        client_->setTransferTimeout(transferTimeoutMs);
    }
    // The client is the "network" side; its signals are routed to the Answer of
    // the matching request. Connections live on the provider, so they are torn
    // down with it.
    connect(client_,
            &LLMQore::BaseClient::chunkReceived,
            this,
            [this](const LLMQore::RequestID& id, const QString& chunk) {
                if (Answer* answer = answerFor(id)) {
                    answer->appendChunk(chunk);
                }
            });
    connect(client_,
            &LLMQore::BaseClient::requestFinalized,
            this,
            [this](const LLMQore::RequestID& id, const LLMQore::CompletionInfo& info) {
                if (Answer* answer = answerFor(id)) {
                    answer->complete(Message(MessageRole::kAssistant, info.fullText));
                    drop(id);
                }
            });
    connect(client_,
            &LLMQore::BaseClient::requestFailed,
            this,
            [this](const LLMQore::RequestID& id, const QString& error) {
                if (Answer* answer = answerFor(id)) {
                    answer->fail(classifyFailure(error), error);
                    drop(id);
                }
            });
}

OpenAiCompatibleProvider::~OpenAiCompatibleProvider() = default;

QString OpenAiCompatibleProvider::id() const {
    return QStringLiteral("openai-compatible");
}

QString OpenAiCompatibleProvider::displayName() const {
    return QStringLiteral("OpenAI-compatible");
}

Answer* OpenAiCompatibleProvider::respond(const std::vector<Message>& history) {
    auto* answer = new Answer(this);

    if (url_.isEmpty() || model_.isEmpty()) {
        failLater(answer,
                  ProviderError::kUnknown,
                  QStringLiteral("Provider is not configured: set POLZA_BASE_URL and POLZA_MODEL"));
        return answer;
    }
    if (apiKey_.isEmpty()) {
        failLater(
                answer, ProviderError::kAuthentication, QStringLiteral("API key is not configured: set POLZA_API_KEY"));
        return answer;
    }

    const LLMQore::RequestID kId = client_->ask(toConversation(history));
    answers_.emplace(kId, answer);
    // The Answer is the cancellation handle: when a consumer cancels it, the
    // real network request is aborted. The provider only translates the intent.
    // The entry is dropped here rather than waiting for LLMQore's terminal
    // signal, so cancellation does not depend on the client emitting
    // requestFailed (which would otherwise leave a stale map entry).
    connect(answer, &Answer::cancelled, this, [this, kId] {
        client_->cancelRequest(kId);
        drop(kId);
    });
    return answer;
}

Answer* OpenAiCompatibleProvider::answerFor(const QString& requestId) const {
    const auto kIt = answers_.find(requestId);
    return kIt == answers_.end() ? nullptr : kIt->second;
}

void OpenAiCompatibleProvider::drop(const QString& requestId) {
    const auto kIt = answers_.find(requestId);
    if (kIt == answers_.end()) {
        return;
    }
    kIt->second->deleteLater();
    answers_.erase(kIt);
}
