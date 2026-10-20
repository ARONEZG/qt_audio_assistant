#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>

#include "domain/ProviderError.h"

class AudioCapture;
class Chat;

// The voice-input command: "recognize the last spoken utterance and post it to
// the chat". It is the one seam between the capture facade and the conversation,
// so neither side has to know about the other:
//
//     transcribeAndSend() -> AudioCapture::transcribeLast()
//     AudioCapture::transcribed -> Chat::send()
//
// The capture facade deliberately stays a pure pipeline relay, and the chat
// keeps owning its send invariants. This object only sequences the two and owns
// the transient request state (busy, one in-flight request, a transcript that
// reached a still-streaming chat).
//
// It is a view model for the shortcut and the Transcribe button, so both go
// through exactly the same path. The injected chat may be null when the
// composition root has no open chat: the command is then disabled, because
// there is nowhere to post the recognized text. If the chat is destroyed while
// a transcription is in flight, the command clears its observing pointer and
// discards the late result instead of dereferencing it.
//
// "Background capture is on" is a command precondition, not only a view rule:
// with capture off there is no fresh utterance to recognize, so a direct call
// is ignored as well.
class VoiceInput : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(VoiceInput)
    QML_ELEMENT
    QML_UNCREATABLE("Injected by the composition root")

    // True from an accepted transcribeAndSend() until the transcriber reports
    // its terminal result. The view uses it to disable the shortcut and the
    // button, so a repeated press cannot start a second request.
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)

public:
    // The capture facade and the chat are borrowed and must outlive this object.
    VoiceInput(AudioCapture& capture, Chat* chat, QObject* parent = nullptr);
    ~VoiceInput() override;

    [[nodiscard]] bool isBusy() const noexcept;

    // Starts recognition of the last capture window. Ignored while a request is
    // already in flight, while background capture is off, while a reply is
    // streaming (the chat would reject the message) or when there is no open
    // chat. The terminal result arrives later from the transcriber; an empty
    // transcript and a failed request are not sent to the chat.
    Q_INVOKABLE void transcribeAndSend();

signals:
    void busyChanged();

private:
    void onTranscribed(const QString& text);
    void onFailed(ProviderError error, const QString& message);
    void onChatStateChanged();
    void setBusy(bool busy);
    // Posts the transcript now, or holds it until the in-flight reply finishes.
    // An empty transcript is dropped.
    void deliver(const QString& text);

    AudioCapture& capture_;
    Chat* chat_;
    // A transcript that arrived while a reply was streaming. It is posted as
    // soon as the chat is idle again, so a race between the user's typed send
    // and the recognition result never loses the spoken message.
    QString pendingText_;
    bool busy_ = false;
};
