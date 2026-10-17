#include <QSignalSpy>
#include <QString>
#include <QtTest>
#include <utility>

#include "domain/Answer.h"
#include "domain/Message.h"
#include "domain/ProviderError.h"

namespace {

Message assistant(QString text) {
    return {MessageRole::kAssistant, std::move(text)};
}

}  // namespace

class TestAnswer : public QObject {
    Q_OBJECT

public:
    explicit TestAnswer(QObject* parent = nullptr);

private slots:
    void test_initialState();
    void test_appendChunkAccumulatesText();
    void test_appendChunkEmitsReceivedChunk();
    void test_emptyChunkIsAccepted();
    void test_completeEmitsCompletedOnce();
    void test_appendChunkAfterCompleteIsIgnored();
    void test_completeAfterCompleteIsIgnored();
    void test_failRecordsError();
    void test_failEmitsFailedOnce();
    void test_appendChunkAfterFailIsIgnored();
    void test_completeAfterFailIsIgnored();
    void test_cancelStopsFurtherStreaming();
    void test_cancelAfterCompleteIsIgnored();
    void test_failAfterCancelIsIgnored();
};

TestAnswer::TestAnswer(QObject* parent) : QObject(parent) {
}

void TestAnswer::test_initialState() {
    const Answer kAnswer;

    QCOMPARE(kAnswer.text(), QString());
    QCOMPARE(kAnswer.isCompleted(), false);
    QCOMPARE(kAnswer.isFailed(), false);
    QCOMPARE(kAnswer.error(), ProviderError::kUnknown);
    QCOMPARE(kAnswer.errorText(), QString());
}

void TestAnswer::test_appendChunkAccumulatesText() {
    Answer answer;

    answer.appendChunk(QStringLiteral("Hel"));
    answer.appendChunk(QStringLiteral("lo"));

    QCOMPARE(answer.text(), QStringLiteral("Hello"));
}

void TestAnswer::test_appendChunkEmitsReceivedChunk() {
    Answer answer;
    const QSignalSpy kSpy(&answer, &Answer::chunkReceived);

    answer.appendChunk(QStringLiteral("a"));
    answer.appendChunk(QStringLiteral("b"));

    QCOMPARE(kSpy.count(), 2);
    QCOMPARE(kSpy.at(0).at(0).toString(), QStringLiteral("a"));
    QCOMPARE(kSpy.at(1).at(0).toString(), QStringLiteral("b"));
}

void TestAnswer::test_emptyChunkIsAccepted() {
    Answer answer;
    const QSignalSpy kSpy(&answer, &Answer::chunkReceived);

    answer.appendChunk(QString());

    QCOMPARE(kSpy.count(), 1);
    QCOMPARE(answer.text(), QString());
}

void TestAnswer::test_completeEmitsCompletedOnce() {
    Answer answer;
    answer.appendChunk(QStringLiteral("hi"));
    const QSignalSpy kSpy(&answer, &Answer::completed);

    answer.complete(assistant(QStringLiteral("hi")));

    QCOMPARE(kSpy.count(), 1);
    QCOMPARE(answer.isCompleted(), true);
    QCOMPARE(answer.isFailed(), false);
}

void TestAnswer::test_appendChunkAfterCompleteIsIgnored() {
    Answer answer;
    answer.complete(assistant(QStringLiteral("done")));
    const QSignalSpy kSpy(&answer, &Answer::chunkReceived);

    answer.appendChunk(QStringLiteral("late"));

    QCOMPARE(kSpy.count(), 0);
    QCOMPARE(answer.text(), QString());
}

void TestAnswer::test_completeAfterCompleteIsIgnored() {
    Answer answer;
    answer.complete(assistant(QStringLiteral("first")));
    const QSignalSpy kSpy(&answer, &Answer::completed);

    answer.complete(assistant(QStringLiteral("second")));

    QCOMPARE(kSpy.count(), 0);
}

void TestAnswer::test_failRecordsError() {
    Answer answer;

    answer.fail(ProviderError::kNetwork, QStringLiteral("boom"));

    QCOMPARE(answer.isFailed(), true);
    QCOMPARE(answer.isCompleted(), false);
    QCOMPARE(answer.error(), ProviderError::kNetwork);
    QCOMPARE(answer.errorText(), QStringLiteral("boom"));
}

void TestAnswer::test_failEmitsFailedOnce() {
    Answer answer;
    const QSignalSpy kSpy(&answer, &Answer::failed);

    answer.fail(ProviderError::kRateLimit, QStringLiteral("slow down"));

    QCOMPARE(kSpy.count(), 1);
    QCOMPARE(kSpy.at(0).at(0).value<ProviderError>(), ProviderError::kRateLimit);
    QCOMPARE(kSpy.at(0).at(1).toString(), QStringLiteral("slow down"));
}

void TestAnswer::test_appendChunkAfterFailIsIgnored() {
    Answer answer;
    answer.fail(ProviderError::kUnknown, QStringLiteral("x"));
    const QSignalSpy kSpy(&answer, &Answer::chunkReceived);

    answer.appendChunk(QStringLiteral("late"));

    QCOMPARE(kSpy.count(), 0);
}

void TestAnswer::test_completeAfterFailIsIgnored() {
    Answer answer;
    answer.fail(ProviderError::kUnknown, QStringLiteral("x"));
    const QSignalSpy kSpy(&answer, &Answer::completed);

    answer.complete(assistant(QStringLiteral("late")));

    QCOMPARE(kSpy.count(), 0);
    QCOMPARE(answer.isCompleted(), false);
}

void TestAnswer::test_cancelStopsFurtherStreaming() {
    Answer answer;
    answer.appendChunk(QStringLiteral("partial"));

    answer.cancel();

    QCOMPARE(answer.isCompleted(), false);
    QCOMPARE(answer.isFailed(), false);
    QCOMPARE(answer.text(), QStringLiteral("partial"));

    const QSignalSpy kChunkSpy(&answer, &Answer::chunkReceived);
    const QSignalSpy kCompletedSpy(&answer, &Answer::completed);
    const QSignalSpy kFailedSpy(&answer, &Answer::failed);
    answer.appendChunk(QStringLiteral("late"));
    answer.complete(assistant(QStringLiteral("late")));
    answer.fail(ProviderError::kUnknown, QStringLiteral("late"));

    QCOMPARE(kChunkSpy.count(), 0);
    QCOMPARE(kCompletedSpy.count(), 0);
    QCOMPARE(kFailedSpy.count(), 0);
}

void TestAnswer::test_cancelAfterCompleteIsIgnored() {
    Answer answer;
    answer.complete(assistant(QStringLiteral("done")));

    answer.cancel();

    QCOMPARE(answer.isCompleted(), true);
    QCOMPARE(answer.isFailed(), false);
}

void TestAnswer::test_failAfterCancelIsIgnored() {
    Answer answer;
    answer.cancel();
    const QSignalSpy kSpy(&answer, &Answer::failed);

    answer.fail(ProviderError::kNetwork, QStringLiteral("late"));

    QCOMPARE(kSpy.count(), 0);
    QCOMPARE(answer.isFailed(), false);
}

QTEST_GUILESS_MAIN(TestAnswer)

#include "tst_answer.moc"
