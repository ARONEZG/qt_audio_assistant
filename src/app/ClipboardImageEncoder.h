#pragma once

#include <QImage>
#include <optional>

#include "domain/ImageAttachment.h"
#include "domain/ImageEncoder.h"

// Encoding of a clipboard image for a vision request.
//
// PNG is the default: clipboard images are dominated by screenshots and text,
// where lossless pixels matter and transparency must survive. A plain photo can
// still produce a PNG larger than many endpoints accept, so an oversized PNG is
// re-encoded as JPEG (quality 90, transparency flattened to white) instead of
// being rejected or silently truncated. The long side is capped so a 4K
// screenshot does not turn into a multi-megapixel payload.
namespace clipboard_image {

// Longest side kept when an image is larger than the model needs.
inline constexpr int kMaxDimension = 2048;
// Above this PNG size the encoder falls back to JPEG.
inline constexpr qsizetype kMaxPngBytes = static_cast<qsizetype>(4 * 1024 * 1024);

// Returns the encoded attachment, or nullopt for a null image. `maxPngBytes`
// is injectable so the fallback is testable without generating a multi-megabyte
// image.
[[nodiscard]] std::optional<ImageAttachment> encode(const QImage& image, qsizetype maxPngBytes = kMaxPngBytes);

}  // namespace clipboard_image

// The ImageEncoder role implementation used by the application: the policy
// above, with no state, so it is safe to call from the paste worker thread.
class ClipboardImageEncoder final : public ImageEncoder {
public:
    ClipboardImageEncoder() = default;
    ClipboardImageEncoder(const ClipboardImageEncoder&) = delete;
    ClipboardImageEncoder& operator=(const ClipboardImageEncoder&) = delete;
    ClipboardImageEncoder(ClipboardImageEncoder&&) = delete;
    ClipboardImageEncoder& operator=(ClipboardImageEncoder&&) = delete;
    ~ClipboardImageEncoder() override;

    [[nodiscard]] std::optional<ImageAttachment> encode(const QImage& image) const override {
        return clipboard_image::encode(image);
    }
};
