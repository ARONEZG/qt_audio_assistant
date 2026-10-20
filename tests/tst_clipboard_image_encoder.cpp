#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QString>
#include <QtTest>
#include <optional>

#include "app/ClipboardImageEncoder.h"
#include "domain/ImageAttachment.h"

namespace {

// A deterministic, text-free image with two known pixels: enough to prove the
// PNG path is lossless without depending on a font or a system theme.
[[nodiscard]] QImage markedImage() {
    QImage image(3, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    image.setPixelColor(1, 1, Qt::blue);
    return image;
}

[[nodiscard]] bool startsWith(const QByteArray& data, const QByteArray& prefix) {
    return data.startsWith(prefix);
}

// The encoder under test must return an attachment for every non-null input.
// Returning a value (instead of dereferencing the optional in the test body)
// keeps the access checked even for the static analyzer.
[[nodiscard]] ImageAttachment requireAttachment(const std::optional<ImageAttachment>& attachment) {
    if (!attachment.has_value()) {
        QTest::qFail("expected the encoder to produce an attachment", __FILE__, __LINE__);
        return {QByteArray(), QString()};
    }
    return *attachment;
}

}  // namespace

// Covers the clipboard-to-attachment encoding policy: PNG by default, a capped
// long side, a JPEG fallback for oversized PNGs, and the null-image boundary.
class TestClipboardImageEncoder : public QObject {
    Q_OBJECT

public:
    explicit TestClipboardImageEncoder(QObject* parent = nullptr);

private slots:
    void test_nullImageProducesNothing();
    void test_pngEncodeIsLossless();
    void test_largeImageIsShrunkToTheLimit();
    void test_smallImageKeepsItsSize();
    void test_oversizedPngFallsBackToJpeg();
    void test_jpegFlattensTransparencyOnWhite();
    void test_pngAtTheSizeLimitIsAccepted();
};

TestClipboardImageEncoder::TestClipboardImageEncoder(QObject* parent) : QObject(parent) {
}

void TestClipboardImageEncoder::test_nullImageProducesNothing() {
    QCOMPARE(clipboard_image::encode(QImage()).has_value(), false);
}

void TestClipboardImageEncoder::test_pngEncodeIsLossless() {
    const ImageAttachment kAttachment = requireAttachment(clipboard_image::encode(markedImage()));
    QCOMPARE(kAttachment.mimeType(), QStringLiteral("image/png"));
    // PNG signature, not a JPEG one.
    QVERIFY(startsWith(kAttachment.data(), QByteArray::fromHex("89504e470d0a1a0a")));

    QImage decoded;
    QVERIFY(decoded.loadFromData(kAttachment.data(), "PNG"));
    QCOMPARE(decoded.size(), QSize(3, 2));
    QCOMPARE(decoded.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(decoded.pixelColor(1, 1), QColor(Qt::blue));
}

void TestClipboardImageEncoder::test_largeImageIsShrunkToTheLimit() {
    QImage image(3000, 1500, QImage::Format_RGB32);
    image.fill(Qt::green);

    const ImageAttachment kAttachment = requireAttachment(clipboard_image::encode(image));

    QImage decoded;
    QVERIFY(decoded.loadFromData(kAttachment.data()));
    // The long side is capped, the aspect ratio is kept.
    QCOMPARE(decoded.size(), QSize(clipboard_image::kMaxDimension, clipboard_image::kMaxDimension / 2));
}

void TestClipboardImageEncoder::test_smallImageKeepsItsSize() {
    const ImageAttachment kAttachment = requireAttachment(clipboard_image::encode(markedImage()));

    QImage decoded;
    QVERIFY(decoded.loadFromData(kAttachment.data()));
    QCOMPARE(decoded.size(), QSize(3, 2));
}

void TestClipboardImageEncoder::test_oversizedPngFallsBackToJpeg() {
    // A one-byte PNG budget forces the fallback without generating a real
    // multi-megabyte screenshot.
    const ImageAttachment kAttachment = requireAttachment(clipboard_image::encode(markedImage(), 1));
    QCOMPARE(kAttachment.mimeType(), QStringLiteral("image/jpeg"));
    QVERIFY(startsWith(kAttachment.data(), QByteArray::fromHex("ffd8ff")));

    QImage decoded;
    QVERIFY(decoded.loadFromData(kAttachment.data(), "JPEG"));
    QCOMPARE(decoded.size(), QSize(3, 2));
}

void TestClipboardImageEncoder::test_jpegFlattensTransparencyOnWhite() {
    QImage image(2, 2, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    const ImageAttachment kAttachment = requireAttachment(clipboard_image::encode(image, 1));
    QCOMPARE(kAttachment.mimeType(), QStringLiteral("image/jpeg"));

    QImage decoded;
    QVERIFY(decoded.loadFromData(kAttachment.data(), "JPEG"));
    // JPEG has no alpha; the transparent source must become white, not black.
    const QColor kPixel = decoded.pixelColor(0, 0);
    QVERIFY(kPixel.red() > 240);
    QVERIFY(kPixel.green() > 240);
    QVERIFY(kPixel.blue() > 240);
}

void TestClipboardImageEncoder::test_pngAtTheSizeLimitIsAccepted() {
    const ImageAttachment kReference = requireAttachment(clipboard_image::encode(markedImage()));

    const ImageAttachment kAtLimit =
            requireAttachment(clipboard_image::encode(markedImage(), kReference.data().size()));

    QCOMPARE(kAtLimit.mimeType(), QStringLiteral("image/png"));
    QCOMPARE(kAtLimit.data(), kReference.data());
}

QTEST_GUILESS_MAIN(TestClipboardImageEncoder)

#include "tst_clipboard_image_encoder.moc"
