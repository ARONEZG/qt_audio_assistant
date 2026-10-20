#include "app/ClipboardImageEncoder.h"

#include <QBuffer>
#include <QByteArray>
#include <QIODevice>
#include <QImage>
#include <QPainter>
#include <QtGlobal>
#include <optional>

namespace {

// Encodes one image into the requested format. nullopt means Qt could not write
// that format (for example an image plugin is unavailable).
[[nodiscard]] std::optional<QByteArray> encodeAs(const QImage& image, const char* format, int quality = -1) {
    QBuffer buffer;
    if (!buffer.open(QIODevice::WriteOnly)) {
        return std::nullopt;
    }
    if (!image.save(&buffer, format, quality)) {
        return std::nullopt;
    }
    if (buffer.data().isEmpty()) {
        return std::nullopt;
    }
    return buffer.data();
}

// Caps the long side while preserving the aspect ratio. A smaller image is
// returned unchanged (implicit sharing).
[[nodiscard]] QImage shrinkToLimit(const QImage& image) {
    if (image.width() <= clipboard_image::kMaxDimension && image.height() <= clipboard_image::kMaxDimension) {
        return image;
    }
    return image.scaled(clipboard_image::kMaxDimension,
                        clipboard_image::kMaxDimension,
                        Qt::KeepAspectRatio,
                        Qt::SmoothTransformation);
}

// JPEG has no alpha channel: paint the image onto white so transparent areas do
// not turn black.
[[nodiscard]] QImage flattenOnWhite(const QImage& image) {
    QImage opaque(image.size(), QImage::Format_RGB32);
    opaque.fill(Qt::white);
    QPainter painter(&opaque);
    painter.drawImage(0, 0, image);
    painter.end();
    return opaque;
}

}  // namespace

namespace clipboard_image {

std::optional<ImageAttachment> encode(const QImage& image, qsizetype maxPngBytes) {
    if (image.isNull()) {
        return std::nullopt;
    }
    const QImage kScaled = shrinkToLimit(image);

    const std::optional<QByteArray> kPng = encodeAs(kScaled, "PNG");
    if (kPng.has_value() && kPng->size() <= maxPngBytes) {
        return ImageAttachment(*kPng, QStringLiteral("image/png"));
    }

    // The lossless payload is too large (or PNG writing failed): send a JPEG
    // instead. Flattening is only needed when the source actually has alpha.
    const QImage kOpaque = kScaled.hasAlphaChannel() ? flattenOnWhite(kScaled) : kScaled;
    const std::optional<QByteArray> kJpeg = encodeAs(kOpaque, "JPEG", 90);
    if (!kJpeg.has_value()) {
        return std::nullopt;
    }
    return ImageAttachment(*kJpeg, QStringLiteral("image/jpeg"));
}

}  // namespace clipboard_image

ClipboardImageEncoder::~ClipboardImageEncoder() = default;
