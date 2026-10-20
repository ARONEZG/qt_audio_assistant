#include <QByteArray>
#include <QString>
#include <QtTest>

#include "domain/ImageAttachment.h"

namespace {

// Forces a real by-value copy at the call boundary; the copy test is about that,
// and it keeps clang-tidy from reading the copy as dead.
ImageAttachment copyOf(const ImageAttachment& attachment) {
    return attachment;
}

}  // namespace

class TestImageAttachment : public QObject {
    Q_OBJECT

public:
    explicit TestImageAttachment(QObject* parent = nullptr);

private slots:
    void test_exposesDataAndMimeType();
    void test_equalValuesCompareEqual();
    void test_differentDataComparesUnequal();
    void test_differentMimeTypeComparesUnequal();
    void test_copyEqualsOriginal();
};

TestImageAttachment::TestImageAttachment(QObject* parent) : QObject(parent) {
}

void TestImageAttachment::test_exposesDataAndMimeType() {
    const ImageAttachment kAttachment(QByteArray("png-bytes"), QStringLiteral("image/png"));

    QCOMPARE(kAttachment.data(), QByteArray("png-bytes"));
    QCOMPARE(kAttachment.mimeType(), QStringLiteral("image/png"));
}

void TestImageAttachment::test_equalValuesCompareEqual() {
    const ImageAttachment kFirst(QByteArray("bytes"), QStringLiteral("image/png"));
    const ImageAttachment kSecond(QByteArray("bytes"), QStringLiteral("image/png"));

    QVERIFY(kFirst == kSecond);
}

void TestImageAttachment::test_differentDataComparesUnequal() {
    const ImageAttachment kFirst(QByteArray("one"), QStringLiteral("image/png"));
    const ImageAttachment kSecond(QByteArray("two"), QStringLiteral("image/png"));

    QVERIFY(!(kFirst == kSecond));
}

void TestImageAttachment::test_differentMimeTypeComparesUnequal() {
    const ImageAttachment kFirst(QByteArray("bytes"), QStringLiteral("image/png"));
    const ImageAttachment kSecond(QByteArray("bytes"), QStringLiteral("image/jpeg"));

    QVERIFY(!(kFirst == kSecond));
}

void TestImageAttachment::test_copyEqualsOriginal() {
    const ImageAttachment kOriginal(QByteArray("bytes"), QStringLiteral("image/png"));
    const ImageAttachment kCopy = copyOf(kOriginal);

    QCOMPARE(kCopy, kOriginal);
}

QTEST_GUILESS_MAIN(TestImageAttachment)

#include "tst_imageattachment.moc"
