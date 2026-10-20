#pragma once

#include <QImage>
#include <optional>

#include "domain/ClipboardImageSource.h"

// Desktop adapter over QGuiApplication::clipboard(). It reads the image the
// platform clipboard currently exposes and hands the decoded QImage handle to
// the paste command. Clipping, scaling and encoding are not done here: encoding
// runs on a worker thread behind ImageEncoder.
//
// It is deliberately thin: there is no clipboard logic beyond "read the image".
// The paste command depends on the ClipboardImageSource role interface, so this
// adapter can be replaced by a test double.
class SystemClipboard final : public ClipboardImageSource {
public:
    SystemClipboard() = default;
    SystemClipboard(const SystemClipboard&) = delete;
    SystemClipboard& operator=(const SystemClipboard&) = delete;
    SystemClipboard(SystemClipboard&&) = delete;
    SystemClipboard& operator=(SystemClipboard&&) = delete;
    ~SystemClipboard() override = default;

    [[nodiscard]] std::optional<QImage> image() const override;
};
