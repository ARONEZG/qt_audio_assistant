#pragma once

#include <QImage>
#include <optional>

// Role interface for "the image currently held by the system clipboard".
//
// It returns the decoded clipboard image as a cheap-to-copy QImage handle; the
// caller owns no platform code. The expensive part - scaling and PNG/JPEG
// encoding - happens behind ImageEncoder on a worker thread, so the GUI thread
// only performs the clipboard read the platform requires.
//
// A missing image is an ordinary result (std::nullopt), not an error: the
// clipboard may hold only text.
class ClipboardImageSource {
public:
    ClipboardImageSource() = default;
    ClipboardImageSource(const ClipboardImageSource&) = delete;
    ClipboardImageSource& operator=(const ClipboardImageSource&) = delete;
    ClipboardImageSource(ClipboardImageSource&&) = delete;
    ClipboardImageSource& operator=(ClipboardImageSource&&) = delete;
    virtual ~ClipboardImageSource();

    [[nodiscard]] virtual std::optional<QImage> image() const = 0;
};
