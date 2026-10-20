#include "domain/PasteImage.h"

#include <QFuture>
#include <QtConcurrent/QtConcurrentRun>
#include <optional>
#include <utility>

#include "domain/Chat.h"
#include "domain/ClipboardImageSource.h"
#include "domain/ImageAttachment.h"
#include "domain/ImageEncoder.h"

PasteImage::PasteImage(const ClipboardImageSource& clipboard, const ImageEncoder& encoder, QObject* parent)
    : QObject(parent), clipboard_(clipboard), encoder_(encoder) {
    connect(&watcher_, &QFutureWatcher<std::optional<ImageAttachment>>::finished, this, &PasteImage::onEncoded);
}

PasteImage::~PasteImage() {
    // The worker borrows encoder_, which the composition root destroys after
    // this command. Wait for an in-flight encode so it can never touch a
    // destroyed encoder during shutdown. Encoding is bounded by the 2048 px cap,
    // and this only runs when the application is closing.
    watcher_.waitForFinished();
}

bool PasteImage::isBusy() const noexcept {
    return busy_;
}

bool PasteImage::hasImage() {
    pendingImage_ = clipboard_.image();
    return pendingImage_.has_value();
}

bool PasteImage::paste(Chat* chat, const QString& caption) {
    if (!pendingImage_.has_value()) {
        pendingImage_ = clipboard_.image();
    }
    const std::optional<QImage> kImage = std::move(pendingImage_);
    pendingImage_.reset();
    if (!kImage.has_value()) {
        return false;
    }
    if (chat == nullptr || busy_ || chat->isStreaming()) {
        return false;
    }

    // Observe this chat until the encode completes. The connection is made once
    // per chat; destruction of either object drops it.
    observeChat(chat);
    pendingCaption_ = caption;
    setBusy(true);
    // Capture the encoder, not `this`: the worker must stay independent of this
    // QObject's lifetime. The encoder is borrowed and outlives the command.
    const ImageEncoder* encoder = &encoder_;
    watcher_.setFuture(QtConcurrent::run([encoder, image = *kImage] { return encoder->encode(image); }));
    return true;
}

void PasteImage::onEncoded() {
    const std::optional<ImageAttachment> kResult = watcher_.result();
    Chat* const kChat = chat_;
    const QString kCaption = std::exchange(pendingCaption_, QString());
    observeChat(nullptr);
    setBusy(false);

    if (!kResult.has_value() || kChat == nullptr) {
        emit pasteFailed();
        return;
    }
    if (!kChat->sendImage(*kResult, kCaption)) {
        // The chat started streaming while the image was being encoded. The
        // caption stays in the composer; the user can paste again.
        emit pasteFailed();
        return;
    }
    emit imagePasted();
}

void PasteImage::observeChat(Chat* chat) {
    if (chat_ == chat) {
        return;
    }
    if (chat_ != nullptr) {
        disconnect(chat_, &QObject::destroyed, this, nullptr);
    }
    chat_ = chat;
    if (chat_ == nullptr) {
        return;
    }
    connect(chat_, &QObject::destroyed, this, [this, chat] {
        if (chat_ == chat) {
            chat_ = nullptr;
        }
    });
}

void PasteImage::setBusy(bool busy) {
    if (busy_ == busy) {
        return;
    }
    busy_ = busy;
    emit busyChanged();
}
