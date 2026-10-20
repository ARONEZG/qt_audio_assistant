#include "FakeClipboardImageSource.h"

#include <utility>

void FakeClipboardImageSource::setImage(std::optional<QImage> image) {
    image_ = std::move(image);
}

void FakeClipboardImageSource::clear() {
    image_.reset();
}

int FakeClipboardImageSource::readCount() const noexcept {
    return readCount_;
}

std::optional<QImage> FakeClipboardImageSource::image() const {
    ++readCount_;
    return image_;
}
