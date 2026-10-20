#pragma once

#include <QImage>
#include <optional>

#include "domain/ImageAttachment.h"

// Role interface for "turn a decoded image into a message attachment".
//
// It is deliberately separate from ClipboardImageSource: reading the clipboard
// is a GUI-thread concern, while scaling and PNG/JPEG encoding are CPU-bound and
// run on a worker thread. Implementations must be safe to call from that worker
// and must not touch UI state.
class ImageEncoder {
public:
    ImageEncoder() = default;
    ImageEncoder(const ImageEncoder&) = delete;
    ImageEncoder& operator=(const ImageEncoder&) = delete;
    ImageEncoder(ImageEncoder&&) = delete;
    ImageEncoder& operator=(ImageEncoder&&) = delete;
    virtual ~ImageEncoder();

    [[nodiscard]] virtual std::optional<ImageAttachment> encode(const QImage& image) const = 0;
};
