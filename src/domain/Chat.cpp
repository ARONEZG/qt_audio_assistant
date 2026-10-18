#include "domain/Chat.h"

#include <cstddef>
#include <optional>
#include <utility>

namespace {

// One user-facing line for a typed failure. The summary names the kind of
// failure; the provider's detail (HTTP status, network text) is appended when it
// carries information. kCancelled is deliberately not a failure and yields no
// value, so a cancelled answer can never surface as an error.
std::optional<QString> failureText(ProviderError error, const QString& detail) {
    if (error == ProviderError::kCancelled) {
        return std::nullopt;
    }

    QString summary = QStringLiteral("Unexpected error");
    if (error == ProviderError::kNetwork) {
        summary = QStringLiteral("Network error");
    } else if (error == ProviderError::kAuthentication) {
        summary = QStringLiteral("Authentication failed");
    } else if (error == ProviderError::kRateLimit) {
        summary = QStringLiteral("Rate limit exceeded");
    } else if (error == ProviderError::kInvalidResponse) {
        summary = QStringLiteral("Invalid response from provider");
    }

    if (detail.isEmpty()) {
        return summary;
    }
    return QStringLiteral("%1: %2").arg(summary, detail);
}

}  // namespace

Chat::Chat(std::unique_ptr<Provider> provider, QObject* parent)
    : QAbstractListModel(parent), provider_(std::move(provider)) {
    Q_ASSERT(provider_ != nullptr);
}

Chat::~Chat() {
    // Disconnect before the provider (and thus its Answer) is destroyed below:
    // a callback into a half-destroyed Chat from onAnswerDestroyed is unsafe.
    if (currentAnswer_ != nullptr) {
        disconnect(currentAnswer_, nullptr, this, nullptr);
    }
}

int Chat::rowCount(const QModelIndex& parent) const {
    // A list model has no children, so rows exist only under the root index.
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(messages_.size());
}

QVariant Chat::data(const QModelIndex& index, int role) const {
    // A list model is flat: every index it hands out is parentless and in the
    // single column, so the row check is the whole contract. An invalid index
    // has row() == -1, and an index built by another model can name a row past
    // the end; both yield nothing.
    if (!isValidRow(index.row())) {
        return {};
    }
    const Message& message = messages_[static_cast<std::size_t>(index.row())];
    if (role == kTextRole) {
        return message.text();
    }
    if (role == kIsUserRole) {
        return message.isUser();
    }
    return {};
}

QHash<int, QByteArray> Chat::roleNames() const {
    // roleNames() is a Qt model API whose return type is mandated as QHash. This
    // is the unavoidable-container boundary from src/AGENTS.md: the QHash is
    // built here, never stored in a field and never returned by another API.
    QHash<int, QByteArray> roles;
    roles.insert(kTextRole, QByteArrayLiteral("text"));
    roles.insert(kIsUserRole, QByteArrayLiteral("isUser"));
    return roles;
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

QString Chat::errorText() const {
    return errorText_;
}

bool Chat::hasError() const noexcept {
    return hasError_;
}

QString Chat::providerId() const {
    return provider_->id();
}

QString Chat::providerName() const {
    return provider_->displayName();
}

void Chat::send(const QString& text) {
    if (isStreaming_ || text.isEmpty()) {
        return;
    }
    // A new request supersedes the failure of the previous one. An ignored send
    // (streaming or blank text) is not a new request and leaves it untouched.
    clearError();
    appendMessage(Message(MessageRole::kUser, text));
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

void Chat::appendMessage(const Message& message) {
    // Append-only: the row is inserted at the end and the history is never
    // mutated, so a model index stays valid and keeps naming its message.
    const int kRow = static_cast<int>(messages_.size());
    beginInsertRows(QModelIndex(), kRow, kRow);
    messages_.push_back(message);
    endInsertRows();
}

bool Chat::isValidRow(int index) const noexcept {
    return index >= 0 && index < static_cast<int>(messages_.size());
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

void Chat::setErrorText(const QString& text) {
    errorText_ = text;
    hasError_ = true;
    emit errorTextChanged();
}

void Chat::clearError() {
    if (!hasError_) {
        return;
    }
    errorText_.clear();
    hasError_ = false;
    emit errorTextChanged();
}

void Chat::onChunkReceived(const QString& chunk) {
    setStreamingText(streamingText_ + chunk);
}

void Chat::onAnswerCompleted(const Message& message) {
    appendMessage(message);
    emit replyReceived(message);
    stopStreaming();
}

void Chat::onAnswerFailed(ProviderError error, const QString& detail) {
    stopStreaming();
    if (const std::optional<QString> kText = failureText(error, detail); kText.has_value()) {
        setErrorText(*kText);
    }
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
