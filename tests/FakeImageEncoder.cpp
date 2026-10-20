#include "FakeImageEncoder.h"

#include <utility>

void FakeImageEncoder::setResult(std::optional<ImageAttachment> result) {
    result_ = std::move(result);
}

void FakeImageEncoder::setOnEncode(std::function<void()> callback) {
    onEncode_ = std::move(callback);
}

int FakeImageEncoder::encodeCount() const noexcept {
    return encodeCount_;
}

QThread* FakeImageEncoder::encodeThread() const noexcept {
    return encodeThread_;
}

std::optional<ImageAttachment> FakeImageEncoder::encode(const QImage& image) const {
    Q_UNUSED(image);
    ++encodeCount_;
    encodeThread_ = QThread::currentThread();
    if (onEncode_) {
        onEncode_();
    }
    return result_;
}
