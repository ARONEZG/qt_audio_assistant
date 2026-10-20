#include <QByteArray>
#include <QString>
#include <QtTest>
#include <optional>

#include "domain/ImageAttachment.h"
#include "domain/Message.h"

namespace {

// Forces a real by-value copy at the call boundary; this is the property the
// copy test is about, and it keeps clang-tidy from reading the copy as dead.
Message copyOf(const Message& message) {
    return message;
}

}  // namespace

class TestMessage : public QObject {
    Q_OBJECT

public:
    explicit TestMessage(QObject* parent = nullptr);

private slots:
    void test_constructsWithRoleAndText();
    void test_exposesRole();
    void test_exposesText();
    void test_equalValuesCompareEqual();
    void test_differentRoleComparesUnequal();
    void test_differentTextComparesUnequal();
    void test_emptyTextIsValid();
    void test_copyEqualsOriginal();
    void test_textMessageHasNoImage();
    void test_constructsWithAnImage();
    void test_imageOnlyMessageHasEmptyText();
    void test_differentImageComparesUnequal();
    void test_copyKeepsTheImage();
};

TestMessage::TestMessage(QObject* parent) : QObject(parent) {
}

void TestMessage::test_constructsWithRoleAndText() {
    const Message kMessage(MessageRole::kUser, QStringLiteral("hello"));

    QCOMPARE(kMessage.role(), MessageRole::kUser);
    QCOMPARE(kMessage.text(), QStringLiteral("hello"));
}

void TestMessage::test_exposesRole() {
    const Message kMessage(MessageRole::kSystem, QStringLiteral("rules"));

    QCOMPARE(kMessage.role(), MessageRole::kSystem);
}

void TestMessage::test_exposesText() {
    const Message kMessage(MessageRole::kAssistant, QStringLiteral("answer"));

    QCOMPARE(kMessage.text(), QStringLiteral("answer"));
}

void TestMessage::test_equalValuesCompareEqual() {
    const Message kFirst(MessageRole::kUser, QStringLiteral("same"));
    const Message kSecond(MessageRole::kUser, QStringLiteral("same"));

    QVERIFY(kFirst == kSecond);
}

void TestMessage::test_differentRoleComparesUnequal() {
    const Message kFirst(MessageRole::kUser, QStringLiteral("same"));
    const Message kSecond(MessageRole::kAssistant, QStringLiteral("same"));

    QVERIFY(!(kFirst == kSecond));
}

void TestMessage::test_differentTextComparesUnequal() {
    const Message kFirst(MessageRole::kUser, QStringLiteral("one"));
    const Message kSecond(MessageRole::kUser, QStringLiteral("two"));

    QVERIFY(!(kFirst == kSecond));
}

void TestMessage::test_emptyTextIsValid() {
    const Message kMessage(MessageRole::kAssistant, QString());

    QCOMPARE(kMessage.role(), MessageRole::kAssistant);
    QCOMPARE(kMessage.text(), QString());
}

void TestMessage::test_copyEqualsOriginal() {
    const Message kOriginal(MessageRole::kSystem, QStringLiteral("rules"));
    const Message kCopy = copyOf(kOriginal);

    QCOMPARE(kCopy, kOriginal);
    QCOMPARE(kCopy.text(), kOriginal.text());
}

void TestMessage::test_textMessageHasNoImage() {
    const Message kMessage(MessageRole::kUser, QStringLiteral("hello"));

    QCOMPARE(kMessage.hasImage(), false);
    QCOMPARE(kMessage.image().has_value(), false);
}

void TestMessage::test_constructsWithAnImage() {
    const ImageAttachment kImage(QByteArray("png-bytes"), QStringLiteral("image/png"));
    const Message kMessage(MessageRole::kUser, QStringLiteral("look"), kImage);

    QCOMPARE(kMessage.role(), MessageRole::kUser);
    QCOMPARE(kMessage.text(), QStringLiteral("look"));
    QCOMPARE(kMessage.hasImage(), true);
    const std::optional<ImageAttachment>& kAttachment = kMessage.image();
    if (!kAttachment.has_value()) {
        QFAIL("the message must carry the attachment");
    }
    QCOMPARE(*kAttachment, kImage);
}

void TestMessage::test_imageOnlyMessageHasEmptyText() {
    const Message kMessage(
            MessageRole::kUser, QString(), ImageAttachment(QByteArray("bytes"), QStringLiteral("image/jpeg")));

    // An image is content on its own: the caption is optional.
    QCOMPARE(kMessage.text(), QString());
    QCOMPARE(kMessage.hasImage(), true);
    const std::optional<ImageAttachment>& kAttachment = kMessage.image();
    if (!kAttachment.has_value()) {
        QFAIL("the message must carry the attachment");
    }
    QCOMPARE(kAttachment->mimeType(), QStringLiteral("image/jpeg"));
}

void TestMessage::test_differentImageComparesUnequal() {
    const Message kFirst(MessageRole::kUser,
                         QStringLiteral("look"),
                         ImageAttachment(QByteArray("one"), QStringLiteral("image/png")));
    const Message kSecond(MessageRole::kUser,
                          QStringLiteral("look"),
                          ImageAttachment(QByteArray("two"), QStringLiteral("image/png")));

    QVERIFY(!(kFirst == kSecond));
}

void TestMessage::test_copyKeepsTheImage() {
    const Message kOriginal(MessageRole::kUser,
                            QStringLiteral("look"),
                            ImageAttachment(QByteArray("bytes"), QStringLiteral("image/png")));
    const Message kCopy = copyOf(kOriginal);

    QCOMPARE(kCopy, kOriginal);
    QCOMPARE(kCopy.hasImage(), true);
    const std::optional<ImageAttachment>& kAttachment = kCopy.image();
    if (!kAttachment.has_value()) {
        QFAIL("the copy must keep the attachment");
    }
    QCOMPARE(kAttachment->data(), QByteArray("bytes"));
}

QTEST_GUILESS_MAIN(TestMessage)

#include "tst_message.moc"
