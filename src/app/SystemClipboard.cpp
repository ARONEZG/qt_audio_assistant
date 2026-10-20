#include "app/SystemClipboard.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <optional>

std::optional<QImage> SystemClipboard::image() const {
    const QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
        return std::nullopt;
    }
    QImage image = clipboard->image();
    if (image.isNull()) {
        return std::nullopt;
    }
    return image;
}
