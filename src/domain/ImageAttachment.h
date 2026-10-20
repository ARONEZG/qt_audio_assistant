#pragma once

#include <QByteArray>
#include <QString>

// An image that travels with a chat message. It is an immutable value: the
// encoded bytes and their MIME type are fixed at construction, so it can be
// copied and compared freely (QByteArray is implicitly shared).
//
// The domain keeps only the encoded representation. Decoding for display
// (a QImage) or for a request (a data URL) is a presentation/adapter concern,
// so this type has no Qt GUI dependency.
class ImageAttachment {
public:
    // A non-empty byte payload and its MIME type (e.g. "image/png"). The caller
    // is responsible for passing a real encoded image; Chat rejects an
    // attachment with an empty payload.
    ImageAttachment(QByteArray data, QString mimeType);

    [[nodiscard]] const QByteArray& data() const noexcept;
    [[nodiscard]] const QString& mimeType() const noexcept;

    [[nodiscard]] bool operator==(const ImageAttachment& other) const;

private:
    QByteArray data_;
    QString mimeType_;
};
