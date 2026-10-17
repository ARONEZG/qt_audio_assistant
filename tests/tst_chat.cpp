#include <QSignalSpy>
#include <QString>
#include <QtTest>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "FakeProvider.h"
#include "domain/Chat.h"
#include "domain/Message.h"
#include "domain/ProviderError.h"

class TestChat : public QObject {
    Q_OBJECT

public:
    explicit TestChat(QObject* parent = nullptr);

private slots:
    void test_exposesProviderIdentity();
    void test_initialState();
    void test_sendAppendsUserMessageAndStartsStreaming();
    void test_chunksAccumulateIntoStreamingText();
    void test_completeAppendsReplyAndResetsStreaming();
    void test_cancelResetsStreamingWithoutReply();
    void test_failResetsStreamingAndEmitsFailed();
    void test_sendWhileStreamingIsIgnored();
    void test_sequentialSendsPreserveHistory();
    void test_historyIsPassedToProvider();
    void test_emptyTextIsAccepted();
};

TestChat::TestChat(QObject* parent) : QObject(parent) {
}

void TestChat::test_exposesProviderIdentity() {
    auto provider = std::make_unique<FakeProvider>();
    const Chat kChat(std::move(provider));

    QCOMPARE(kChat.providerId(), QStringLiteral("fake"));
    QCOMPARE(kChat.providerName(), QStringLiteral("Fake Provider"));
}

void TestChat::test_initialState() {
    auto provider = std::make_unique<FakeProvider>();
    const Chat kChat(std::move(provider));

    QCOMPARE(kChat.messages().size(), std::size_t{0});
    QCOMPARE(kChat.streamingText(), QString());
    QCOMPARE(kChat.isStreaming(), false);
}

void TestChat::test_sendAppendsUserMessageAndStartsStreaming() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    const QSignalSpy kStateSpy(&chat, &Chat::stateChanged);

    chat.send(QStringLiteral("hello"));

    QCOMPARE(chat.messages().size(), std::size_t{1});
    QCOMPARE(chat.messages().at(0).role(), MessageRole::kUser);
    QCOMPARE(chat.messages().at(0).text(), QStringLiteral("hello"));
    QCOMPARE(chat.isStreaming(), true);
    QCOMPARE(kStateSpy.count(), 1);
    QCOMPARE(fake->lastHistory().size(), std::size_t{1});
    QCOMPARE(fake->lastHistory().at(0).text(), QStringLiteral("hello"));
}

void TestChat::test_chunksAccumulateIntoStreamingText() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));
    const QSignalSpy kTextSpy(&chat, &Chat::streamingTextChanged);

    fake->lastAnswer()->appendChunk(QStringLiteral("Hel"));
    fake->lastAnswer()->appendChunk(QStringLiteral("lo"));

    QCOMPARE(chat.streamingText(), QStringLiteral("Hello"));
    QCOMPARE(kTextSpy.count(), 2);
}

void TestChat::test_completeAppendsReplyAndResetsStreaming() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));
    const QSignalSpy kTextSpy(&chat, &Chat::streamingTextChanged);
    const QSignalSpy kStateSpy(&chat, &Chat::stateChanged);
    const QSignalSpy kReplySpy(&chat, &Chat::replyReceived);
    const Message kReply(MessageRole::kAssistant, QStringLiteral("ok"));

    fake->lastAnswer()->appendChunk(QStringLiteral("ok"));
    fake->lastAnswer()->complete(kReply);

    QCOMPARE(chat.messages().size(), std::size_t{2});
    QCOMPARE(chat.messages().at(1), kReply);
    QCOMPARE(chat.isStreaming(), false);
    QCOMPARE(chat.streamingText(), QString());
    QCOMPARE(kReplySpy.count(), 1);
    QCOMPARE(kStateSpy.count(), 1);
    QCOMPARE(kTextSpy.count(), 2);
}

void TestChat::test_cancelResetsStreamingWithoutReply() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));
    const QSignalSpy kTextSpy(&chat, &Chat::streamingTextChanged);
    const QSignalSpy kStateSpy(&chat, &Chat::stateChanged);
    const QSignalSpy kReplySpy(&chat, &Chat::replyReceived);
    const QSignalSpy kFailedSpy(&chat, &Chat::failed);
    Answer* answer = fake->lastAnswer();
    answer->appendChunk(QStringLiteral("partial"));

    chat.cancel();

    QCOMPARE(chat.isStreaming(), false);
    QCOMPARE(chat.streamingText(), QString());
    QCOMPARE(chat.messages().size(), std::size_t{1});
    QCOMPARE(kReplySpy.count(), 0);
    QCOMPARE(kFailedSpy.count(), 0);
    QCOMPARE(kStateSpy.count(), 1);
    QCOMPARE(kTextSpy.count(), 2);
    QCOMPARE(answer->isCompleted(), false);
    QCOMPARE(answer->isFailed(), false);

    answer->appendChunk(QStringLiteral("late"));
    QCOMPARE(chat.streamingText(), QString());
}

void TestChat::test_failResetsStreamingAndEmitsFailed() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));
    const QSignalSpy kTextSpy(&chat, &Chat::streamingTextChanged);
    const QSignalSpy kStateSpy(&chat, &Chat::stateChanged);
    const QSignalSpy kReplySpy(&chat, &Chat::replyReceived);
    const QSignalSpy kFailedSpy(&chat, &Chat::failed);
    fake->lastAnswer()->appendChunk(QStringLiteral("partial"));

    fake->lastAnswer()->fail(ProviderError::kNetwork, QStringLiteral("offline"));

    QCOMPARE(chat.isStreaming(), false);
    QCOMPARE(chat.streamingText(), QString());
    QCOMPARE(chat.messages().size(), std::size_t{1});
    QCOMPARE(kReplySpy.count(), 0);
    QCOMPARE(kFailedSpy.count(), 1);
    QCOMPARE(kFailedSpy.at(0).at(0).toString(), QStringLiteral("offline"));
    QCOMPARE(kStateSpy.count(), 1);
    QCOMPARE(kTextSpy.count(), 2);
}

void TestChat::test_sendWhileStreamingIsIgnored() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));

    chat.send(QStringLiteral("first"));
    chat.send(QStringLiteral("second"));

    QCOMPARE(chat.messages().size(), std::size_t{1});
    QCOMPARE(chat.messages().at(0).text(), QStringLiteral("first"));
    QCOMPARE(chat.isStreaming(), true);
    QCOMPARE(fake->respondCount(), 1);
}

void TestChat::test_sequentialSendsPreserveHistory() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));

    chat.send(QStringLiteral("one"));
    fake->lastAnswer()->appendChunk(QStringLiteral("a"));
    fake->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("a")));
    chat.send(QStringLiteral("two"));
    fake->lastAnswer()->appendChunk(QStringLiteral("b"));
    fake->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("b")));

    const std::vector<Message>& history = chat.messages();
    QCOMPARE(history.size(), std::size_t{4});
    QCOMPARE(history.at(0).text(), QStringLiteral("one"));
    QCOMPARE(history.at(1).text(), QStringLiteral("a"));
    QCOMPARE(history.at(2).text(), QStringLiteral("two"));
    QCOMPARE(history.at(3).text(), QStringLiteral("b"));
    QCOMPARE(history.at(0).role(), MessageRole::kUser);
    QCOMPARE(history.at(1).role(), MessageRole::kAssistant);
    QCOMPARE(fake->respondCount(), 2);
}

void TestChat::test_historyIsPassedToProvider() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));

    chat.send(QStringLiteral("one"));
    fake->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("a")));
    chat.send(QStringLiteral("two"));

    const std::vector<Message>& history = fake->lastHistory();
    QCOMPARE(history.size(), std::size_t{3});
    QCOMPARE(history.at(0).text(), QStringLiteral("one"));
    QCOMPARE(history.at(1).text(), QStringLiteral("a"));
    QCOMPARE(history.at(2).text(), QStringLiteral("two"));
    QCOMPARE(history.at(2).role(), MessageRole::kUser);
}

void TestChat::test_emptyTextIsAccepted() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));

    chat.send(QString());

    QCOMPARE(chat.messages().size(), std::size_t{1});
    QCOMPARE(chat.messages().at(0).text(), QString());
    QCOMPARE(chat.isStreaming(), true);
    QCOMPARE(fake->respondCount(), 1);
}

QTEST_GUILESS_MAIN(TestChat)

#include "tst_chat.moc"
