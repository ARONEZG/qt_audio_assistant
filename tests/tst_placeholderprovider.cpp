#include <QSignalSpy>
#include <QString>
#include <QtTest>
#include <chrono>
#include <vector>

#include "app/PlaceholderProvider.h"
#include "domain/Answer.h"
#include "domain/Message.h"

namespace {

constexpr auto kReplyTimeout = std::chrono::seconds{5};

}  // namespace

// The placeholder provider is app-level glue for the MVP (the networked provider
// is the next increment), but it still honours the Provider contract: respond()
// returns immediately and the Answer is fed from the event loop, so it is worth
// pinning down in a test rather than trusting it.
class TestPlaceholderProvider : public QObject {
    Q_OBJECT

public:
    explicit TestPlaceholderProvider(QObject* parent = nullptr);

private slots:
    void test_exposesIdentity();
    void test_respondEchoesTheLastUserMessage();
    void test_respondWithoutHistory();
};

TestPlaceholderProvider::TestPlaceholderProvider(QObject* parent) : QObject(parent) {
}

void TestPlaceholderProvider::test_exposesIdentity() {
    const PlaceholderProvider kProvider;

    QCOMPARE(kProvider.id(), QStringLiteral("placeholder"));
    QVERIFY(kProvider.displayName().contains(QStringLiteral("offline")));
}

void TestPlaceholderProvider::test_respondEchoesTheLastUserMessage() {
    PlaceholderProvider provider;

    Answer* answer = provider.respond({Message(MessageRole::kUser, QStringLiteral("hi"))});
    QVERIFY(answer != nullptr);
    // respond() must not drive the answer synchronously.
    QCOMPARE(answer->isCompleted(), false);
    QSignalSpy completedSpy(answer, &Answer::completed);

    QVERIFY(completedSpy.wait(kReplyTimeout));

    QCOMPARE(answer->isCompleted(), true);
    QCOMPARE(answer->text(), QStringLiteral("Echo: hi"));
}

void TestPlaceholderProvider::test_respondWithoutHistory() {
    PlaceholderProvider provider;

    Answer* answer = provider.respond({});
    QVERIFY(answer != nullptr);
    QSignalSpy completedSpy(answer, &Answer::completed);

    QVERIFY(completedSpy.wait(kReplyTimeout));

    QCOMPARE(answer->isCompleted(), true);
    QCOMPARE(answer->text(), QStringLiteral("(no message to echo)"));
}

QTEST_GUILESS_MAIN(TestPlaceholderProvider)

#include "tst_placeholderprovider.moc"
