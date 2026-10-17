#include <QString>
#include <QtTest>

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

QTEST_GUILESS_MAIN(TestMessage)

#include "tst_message.moc"
