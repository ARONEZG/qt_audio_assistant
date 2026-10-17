#include "domain/Chat.h"

#include <utility>

Chat::Chat(std::unique_ptr<Provider> provider, QObject* parent) : QObject(parent), provider_(std::move(provider)) {
}

Chat::~Chat() = default;

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
    Answer* answer = currentAnswer_;
    stopStreaming();
    if (answer != nullptr) {
        // Detached already: its signals no longer reach this Chat.
        answer->cancel();
    }
}

void Chat::startStreaming() {
    Answer* answer = provider_->respond(messages_);
    currentAnswer_ = answer;
    connect(answer, &Answer::chunkReceived, this, &Chat::onChunkReceived);
    connect(answer, &Answer::completed, this, &Chat::onAnswerCompleted);
    connect(answer, &Answer::failed, this, &Chat::onAnswerFailed);
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
    if (error != ProviderError::kCancelled) {
        emit failed(detail);
    }
}
