#include "domain/Chat.h"

#include <utility>

Chat::Chat(std::unique_ptr<Provider> provider, QObject* parent) : QObject(parent), provider_(std::move(provider)) {
    Q_ASSERT(provider_ != nullptr);
}

Chat::~Chat() {
    // Disconnect before the provider (and thus its Answer) is destroyed below:
    // a callback into a half-destroyed Chat from onAnswerDestroyed is unsafe.
    if (currentAnswer_ != nullptr) {
        disconnect(currentAnswer_, nullptr, this, nullptr);
    }
}

const std::vector<Message>& Chat::messages() const noexcept {
    return messages_;
}

QString Chat::streamingText() const {
    return streamingText_;
}

bool Chat::isStreaming() const noexcept {
    return isStreaming_;
}

QString Chat::providerId() const {
    return provider_->id();
}

QString Chat::providerName() const {
    return provider_->displayName();
}

void Chat::send(const QString& text) {
    if (isStreaming_) {
        return;
    }
    messages_.emplace_back(MessageRole::kUser, text);
    startStreaming();
}

void Chat::cancel() {
    if (!isStreaming_) {
        return;
    }
    if (currentAnswer_ != nullptr) {
        // Answer::cancel() emits cancelled(); onAnswerCancelled() resets state.
        currentAnswer_->cancel();
    } else {
        stopStreaming();
    }
}

void Chat::startStreaming() {
    Answer* answer = provider_->respond(messages_);
    Q_ASSERT(answer != nullptr);
    currentAnswer_ = answer;
    connect(answer, &Answer::chunkReceived, this, &Chat::onChunkReceived);
    connect(answer, &Answer::completed, this, &Chat::onAnswerCompleted);
    connect(answer, &Answer::failed, this, &Chat::onAnswerFailed);
    connect(answer, &Answer::cancelled, this, &Chat::onAnswerCancelled);
    connect(answer, &QObject::destroyed, this, &Chat::onAnswerDestroyed);
    setStreaming(true);
}

void Chat::stopStreaming() {
    if (currentAnswer_ != nullptr) {
        disconnect(currentAnswer_, nullptr, this, nullptr);
        currentAnswer_ = nullptr;
    }
    setStreamingText(QString());
    setStreaming(false);
}

void Chat::setStreamingText(const QString& text) {
    if (streamingText_ == text) {
        return;
    }
    streamingText_ = text;
    emit streamingTextChanged();
}

void Chat::setStreaming(bool streaming) {
    if (isStreaming_ == streaming) {
        return;
    }
    isStreaming_ = streaming;
    emit stateChanged();
}

void Chat::onChunkReceived(const QString& chunk) {
    setStreamingText(streamingText_ + chunk);
}

void Chat::onAnswerCompleted(const Message& message) {
    messages_.push_back(message);
    emit replyReceived(message);
    stopStreaming();
}

void Chat::onAnswerFailed(ProviderError error, const QString& detail) {
    stopStreaming();
    emit failed(error, detail);
}

void Chat::onAnswerCancelled() {
    stopStreaming();
    emit cancelled();
}

void Chat::onAnswerDestroyed() {
    // The provider destroyed its Answer without a terminal signal. The bare
    // observing pointer is cleared here, synchronously with destruction, so it
    // can never dangle.
    currentAnswer_ = nullptr;
    setStreamingText(QString());
    setStreaming(false);
}
