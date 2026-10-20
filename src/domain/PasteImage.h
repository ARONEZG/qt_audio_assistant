#pragma once

#include <QtQml/qqmlregistration.h>

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>
#include <optional>

#include "domain/ImageAttachment.h"

class Chat;
class ClipboardImageSource;
class ImageEncoder;

// The paste-image command: "take the image held by the clipboard and post it to
// the chat". It is the one seam between the platform clipboard and the
// conversation, so neither the chat nor the view has to know how an image is
// read or encoded.
//
//     hasImage()             -> ClipboardImageSource::image()
//     paste(chat, caption)   -> ImageEncoder::encode() on a worker thread
//                            -> Chat::sendImage()
//
// The clipboards on all supported desktops must be read on the GUI thread, but
// scaling and PNG/JPEG encoding are CPU-bound: they run through QtConcurrent so
// pasting a 4K screenshot does not freeze the UI. The result is posted back to
// the GUI thread before it touches the chat.
//
// The two-step API lets the view distinguish "no image, do a normal text paste"
// from "image paste accepted/rejected". `paste()` returns true only when the
// image was accepted for sending; the view then clears the caption when
// imagePasted() confirms the message was actually appended (a rejected send
// emits pasteFailed() and leaves the caption untouched).
class PasteImage : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PasteImage)
    QML_ELEMENT
    QML_UNCREATABLE("Injected by the composition root")

    // True while a clipboard image is being encoded. The view can use it to
    // ignore repeated gestures; paste() also rejects them defensively.
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)

public:
    // Both dependencies are borrowed and must outlive this command.
    PasteImage(const ClipboardImageSource& clipboard, const ImageEncoder& encoder, QObject* parent = nullptr);
    ~PasteImage() override;

    [[nodiscard]] bool isBusy() const noexcept;

    // Reads the clipboard and remembers the image for the following paste()
    // call. Returns false when the clipboard holds no image, so the view knows
    // to let the normal text paste happen. This is the only clipboard read.
    Q_INVOKABLE bool hasImage();

    // Starts encoding the remembered image and, when it is ready, appends it to
    // `chat` with the optional caption. Returns true when the encode started;
    // false when there is no image, no open chat, a reply is streaming, or an
    // earlier paste is still being encoded. The caller must not clear the
    // caption on false.
    Q_INVOKABLE bool paste(Chat* chat, const QString& caption = QString());

signals:
    // The image was appended to the chat: the view may clear the caption.
    void imagePasted();
    // Encoding failed, the chat disappeared, or it started streaming before the
    // result arrived. The caption is left intact for the user.
    void pasteFailed();
    void busyChanged();

private:
    void onEncoded();
    void setBusy(bool busy);
    // Starts observing `chat` for destruction and stops observing the previous
    // one. Called with nullptr when the encode finishes or the chat is gone.
    void observeChat(Chat* chat);

    const ClipboardImageSource& clipboard_;
    const ImageEncoder& encoder_;
    std::optional<QImage> pendingImage_;
    QFutureWatcher<std::optional<ImageAttachment>> watcher_;
    // The chat is observed, not owned. The destroyed() connection clears this
    // pointer if the conversation is closed while the encode is still running.
    Chat* chat_ = nullptr;
    QString pendingCaption_;
    bool busy_ = false;
};
