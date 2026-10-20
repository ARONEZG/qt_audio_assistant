#include "domain/ImageAttachment.h"

#include <utility>

ImageAttachment::ImageAttachment(QByteArray data, QString mimeType)
    : data_(std::move(data)), mimeType_(std::move(mimeType)) {
}

const QByteArray& ImageAttachment::data() const noexcept {
    return data_;
}

const QString& ImageAttachment::mimeType() const noexcept {
    return mimeType_;
}

bool ImageAttachment::operator==(const ImageAttachment& other) const {
    return data_ == other.data_ && mimeType_ == other.mimeType_;
}
