#pragma once

#include <QImage>
#include <optional>

#include "domain/ClipboardImageSource.h"

// Test double for the clipboard role interface. It returns whatever image the
// test put there, so the paste command can be exercised without a platform
// clipboard or a GUI session.
class FakeClipboardImageSource : public ClipboardImageSource {
public:
    FakeClipboardImageSource() = default;
    FakeClipboardImageSource(const FakeClipboardImageSource&) = delete;
    FakeClipboardImageSource& operator=(const FakeClipboardImageSource&) = delete;
    FakeClipboardImageSource(FakeClipboardImageSource&&) = delete;
    FakeClipboardImageSource& operator=(FakeClipboardImageSource&&) = delete;
    ~FakeClipboardImageSource() override = default;

    void setImage(std::optional<QImage> image);
    void clear();
    // Number of reads, so a test can prove the command looks at the clipboard
    // once per paste gesture rather than caching a stale image.
    [[nodiscard]] int readCount() const noexcept;

    [[nodiscard]] std::optional<QImage> image() const override;

private:
    std::optional<QImage> image_;
    mutable int readCount_ = 0;
};
