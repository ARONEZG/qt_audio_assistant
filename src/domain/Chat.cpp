#include "domain/Chat.h"

#include <cstddef>
#include <optional>
#include <utility>

#include "domain/SafeMarkdown.h"

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
    // One publish per window: the timer is restarted only after it fired, never
    // on every chunk, so a continuous stream still updates at the fixed rate
    // instead of being debounced into silence.
    markupThrottle_.setSingleShot(true);
    markupThrottle_.setInterval(kStreamingMarkupThrottleMs);
    connect(&markupThrottle_, &QTimer::timeout, this, &Chat::publishStreamingMarkup);
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
    if (role == kMarkupRole) {
        return markupAt(static_cast<std::size_t>(index.row()));
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
    roles.insert(kMarkupRole, QByteArrayLiteral("markup"));
    return roles;
}

const std::vector<Message>& Chat::messages() const noexcept {
    return messages_;
}

QString Chat::streamingText() const {
    // The raw reply is exposed only while it is in flight; once the stream ends
    // the finished text lives on as a Message in the history, and the retained
    // copy here only feeds the markup source.
    return isStreaming_ ? streamingText_ : QString();
}

QString Chat::streamingMarkup() const {
    if (streamingMarkupDirty_) {
        // The live reply is assistant content too: sanitise it so the streaming
        // tail is rendered through the same boundary as the finished message.
        // Recomputation is lazy (and thus needs no GUI unless a consumer reads
        // markup) and happens at most once per throttled notification.
        streamingMarkupCache_ = SafeMarkdown(streamingText_).text();
        streamingMarkupDirty_ = false;
    }
    return streamingMarkupCache_;
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
    // Keep the memoisation aligned with the history; the value is filled in on
    // the first kMarkupRole query.
    markupCache_.emplace_back(std::nullopt);
    endInsertRows();
}

bool Chat::isValidRow(int index) const noexcept {
    return index >= 0 && index < static_cast<int>(messages_.size());
}

QString Chat::markupAt(std::size_t row) const {
    std::optional<QString>& cached = markupCache_[row];
    if (cached.has_value()) {
        return *cached;
    }
    // The user writes plain text and it is shown verbatim; the assistant's text
    // is untrusted, so it is neutralised before the view can render it.
    const Message& kMessage = messages_[row];
    const QString kMarkup = kMessage.isUser() ? kMessage.text() : SafeMarkdown(kMessage.text()).text();
    cached = kMarkup;
    return kMarkup;
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
    // A new reply starts from a clean slate: the previous reply's text and its
    // (retained) markup must not leak into the new streaming tail.
    streamingText_.clear();
    setStreamingMarkup(QString());
    setStreaming(true);
}

void Chat::stopStreaming() {
    if (currentAnswer_ != nullptr) {
        disconnect(currentAnswer_, nullptr, this, nullptr);
        currentAnswer_ = nullptr;
    }
    finishStreaming();
}

void Chat::finishStreaming() {
    // Terminal flush: an update still pending is published now rather than on
    // the next throttle tick, so no markup update is deferred past the end of the
    // stream. streamingText_ is kept as the sanitisation source: the lazy
    // getter then exposes exactly SafeMarkdown(full text), byte-identical to the
    // finished message that replaces the tail.
    markupThrottle_.stop();
    if (isStreaming_ && markupPublishPending_) {
        markupPublishPending_ = false;
        emit streamingMarkupChanged();
    }
    // streamingText() hides the retained raw text once the stream is over; signal
    // the transition only if the text was actually exposed.
    const bool kExposedText = isStreaming_ && !streamingText_.isEmpty();
    setStreaming(false);
    if (kExposedText) {
        emit streamingTextChanged();
    }
}

void Chat::setStreamingMarkup(const QString& markup) {
    streamingMarkupDirty_ = false;
    markupPublishPending_ = false;
    if (streamingMarkupCache_ == markup) {
        return;
    }
    streamingMarkupCache_ = markup;
    emit streamingMarkupChanged();
}

void Chat::scheduleMarkupPublish() {
    if (markupPublishPending_ && !streamingText_.isEmpty() && !markupThrottle_.isActive()) {
        markupThrottle_.start();
    }
}

void Chat::publishStreamingMarkup() {
    // Only the notification is throttled; the (potentially expensive) whole-text
    // sanitisation stays lazy in streamingMarkup(). Keeping it out of the timer
    // also means a headless Chat that never reads markup never needs a
    // QGuiApplication (SafeMarkdown uses QTextDocument).
    if (!markupPublishPending_) {
        return;
    }
    markupPublishPending_ = false;
    emit streamingMarkupChanged();
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
    if (chunk.isEmpty()) {
        return;
    }
    // O(1) amortised append: the accumulated reply is never rebuilt through
    // operator+ per chunk, so raw accumulation stays linear over the stream.
    streamingText_.append(chunk);
    // The raw text is always current; the sanitised view is recomputed lazily and
    // is only advertised to QML at the throttled cadence.
    emit streamingTextChanged();
    streamingMarkupDirty_ = true;
    markupPublishPending_ = true;
    scheduleMarkupPublish();
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
    // can never dangle; finishStreaming() then flushes and clears the tail
    // without re-running the disconnect guard.
    currentAnswer_ = nullptr;
    finishStreaming();
}
