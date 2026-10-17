#include "domain/Answer.h"

Answer::Answer(QObject* parent) : QObject(parent) {
}

Answer::~Answer() = default;

bool Answer::isStreaming() const noexcept {
    return state_ == State::kStreaming;
}

void Answer::appendChunk(const QString& chunk) {
    if (!isStreaming()) {
        return;
    }
    text_.append(chunk);
    emit chunkReceived(chunk);
}

void Answer::complete(const Message& message) {
    if (!isStreaming()) {
        return;
    }
    state_ = State::kCompleted;
    emit completed(message);
}

void Answer::fail(ProviderError error, const QString& detail) {
    if (!isStreaming()) {
        return;
    }
    state_ = State::kFailed;
    error_ = error;
    errorText_ = detail;
    emit failed(error, detail);
}

void Answer::cancel() {
    if (!isStreaming()) {
        return;
    }
    state_ = State::kCancelled;
    emit cancelled();
}

QString Answer::text() const {
    return text_;
}

bool Answer::isCompleted() const noexcept {
    return state_ == State::kCompleted;
}

bool Answer::isFailed() const noexcept {
    return state_ == State::kFailed;
}

bool Answer::isCancelled() const noexcept {
    return state_ == State::kCancelled;
}

ProviderError Answer::error() const noexcept {
    return error_;
}

QString Answer::errorText() const {
    return errorText_;
}
