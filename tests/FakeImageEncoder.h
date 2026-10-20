#pragma once

#include <QImage>
#include <QThread>
#include <functional>
#include <optional>

#include "domain/ImageAttachment.h"
#include "domain/ImageEncoder.h"

// Test double for the image encoder. It returns a preset attachment without
// doing real encoding, so paste tests stay fast and deterministic; it also
// records the thread that performed the work, which proves the command really
// moved encoding off the caller's (GUI) thread.
class FakeImageEncoder : public ImageEncoder {
public:
    FakeImageEncoder() = default;
    FakeImageEncoder(const FakeImageEncoder&) = delete;
    FakeImageEncoder& operator=(const FakeImageEncoder&) = delete;
    FakeImageEncoder(FakeImageEncoder&&) = delete;
    FakeImageEncoder& operator=(FakeImageEncoder&&) = delete;
    ~FakeImageEncoder() override = default;

    void setResult(std::optional<ImageAttachment> result);
    // Called on the worker thread before returning, so a test can observe it.
    void setOnEncode(std::function<void()> callback);

    [[nodiscard]] int encodeCount() const noexcept;
    [[nodiscard]] QThread* encodeThread() const noexcept;

    [[nodiscard]] std::optional<ImageAttachment> encode(const QImage& image) const override;

private:
    std::optional<ImageAttachment> result_;
    std::function<void()> onEncode_;
    mutable int encodeCount_ = 0;
    mutable QThread* encodeThread_ = nullptr;
};
