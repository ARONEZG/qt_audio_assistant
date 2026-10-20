#include "domain/VoiceInput.h"

#include <QDebug>
#include <QString>
#include <utility>

#include "domain/AudioCapture.h"
#include "domain/Chat.h"

VoiceInput::VoiceInput(AudioCapture& capture, Chat* chat, QObject* parent)
    : QObject(parent), capture_(capture), chat_(chat) {
    // The command owns the result only while one of its requests is in flight;
    // see onTranscribed().
    connect(&capture_, &AudioCapture::transcribed, this, &VoiceInput::onTranscribed);
    connect(&capture_, &AudioCapture::failed, this, &VoiceInput::onFailed);
    if (chat_ != nullptr) {
        connect(chat_, &Chat::stateChanged, this, &VoiceInput::onChatStateChanged);
    }
}

VoiceInput::~VoiceInput() = default;

bool VoiceInput::isBusy() const noexcept {
    return busy_;
}

void VoiceInput::transcribeAndSend() {
    if (busy_) {
        qWarning() << "Voice input ignored: a transcription is already in flight";
        return;
    }
    if (chat_ == nullptr) {
        qWarning() << "Voice input ignored: no chat is open";
        return;
    }
    // Chat::send() ignores a message while a reply streams. Refusing the request
    // up front is honest: no recognition is spent, and the user still sees the
    // in-flight reply instead of a silently dropped message.
    if (chat_->isStreaming()) {
        qWarning() << "Voice input ignored: wait for the current reply to finish";
        return;
    }
    setBusy(true);
    qInfo() << "Voice input: transcribing the last captured utterance";
    capture_.transcribeLast();
}

void VoiceInput::onTranscribed(const QString& text) {
    // A transcription requested directly on the facade (not through this
    // command) is not ours to send.
    if (!busy_) {
        return;
    }
    setBusy(false);
    deliver(text);
}

void VoiceInput::onFailed(ProviderError error, const QString& message) {
    if (!busy_) {
        return;
    }
    setBusy(false);
    // The reason is already exposed by AudioCapture (errorText), which the view
    // shows next to the composer.
    qWarning() << "Voice input: transcription failed (error" << static_cast<int>(error) << "):" << message;
}

void VoiceInput::onChatStateChanged() {
    if (chat_->isStreaming() || pendingText_.isEmpty()) {
        return;
    }
    deliver(std::exchange(pendingText_, QString()));
}

void VoiceInput::deliver(const QString& text) {
    const QString kText = text.trimmed();
    if (kText.isEmpty()) {
        qWarning() << "Voice input: the transcription was empty; nothing sent";
        return;
    }
    if (chat_->isStreaming()) {
        // A typed message started after the request but before its result. Keep
        // the transcript and post it when that reply finishes (onChatStateChanged).
        pendingText_ = kText;
        qInfo() << "Voice input: chat is busy; the transcription is queued until the reply finishes";
        return;
    }
    qInfo() << "Voice input: sending the transcription to the chat (" << kText.size() << "characters)";
    chat_->send(kText);
}

void VoiceInput::setBusy(bool busy) {
    // Every caller changes the value (a request starts while idle, or a terminal
    // result ends the one in flight), so no same-value guard is needed.
    busy_ = busy;
    emit busyChanged();
}
