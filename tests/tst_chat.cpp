#include <QByteArray>
#include <QModelIndex>
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

namespace {

// Runs one request through the fake provider and returns what the chat exposes
// after the failure, keeping the mapping test itself a flat table check.
QString errorTextAfterFailure(ProviderError error, const QString& detail) {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));
    fake->lastAnswer()->fail(error, detail);
    return chat.errorText();
}

// Mirrors SafeMarkdown's invariant: no '<' may reach a renderer unescaped.
[[nodiscard]] bool hasUnescapedAngleBracket(const QString& markdown) {
    int precedingBackslashes = 0;
    for (const QChar kCharacter : markdown) {
        if (kCharacter == QLatin1Char('<') && precedingBackslashes % 2 == 0) {
            return true;
        }
        precedingBackslashes = kCharacter == QLatin1Char('\\') ? precedingBackslashes + 1 : 0;
    }
    return false;
}

}  // namespace

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
    void test_failureExposesErrorText();
    void test_failureWithoutDetailUsesSummary();
    void test_failureTextMapsEveryProviderError_data();
    void test_failureTextMapsEveryProviderError();
    void test_sendClearsPreviousError();
    void test_cancelDoesNotSetError();
    void test_cancelledFailureDoesNotSetError();
    void test_sendWhileStreamingIsIgnored();
    void test_cancelWithoutStreamingIsNoOp();
    void test_sequentialSendsPreserveHistory();
    void test_historyIsPassedToProvider();
    void test_emptyTextIsIgnored();
    void test_providerDestroyingAnswerResetsStreaming();
    void test_modelExposesMessageRoles();
    void test_modelInsertsUserMessageRow();
    void test_modelInsertsReplyRow();
    void test_modelIgnoresUnknownRoleAndOutOfRangeRow();
    void test_streamingTextIsNotAModelRow();
    void test_markupRoleKeepsUserTextVerbatim();
    void test_markupRoleSanitisesAssistantText();
    void test_markupRoleIsMemoisedAcrossReads();
    void test_streamingMarkupSanitisesLiveAnswer();
    void test_streamingMarkupIsRecomputedOnlyWhenTextChanges();
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
    QCOMPARE(kChat.errorText(), QString());
    QCOMPARE(kChat.hasError(), false);
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
    const QSignalSpy kCancelledSpy(&chat, &Chat::cancelled);
    Answer* answer = fake->lastAnswer();
    answer->appendChunk(QStringLiteral("partial"));

    chat.cancel();

    QCOMPARE(chat.isStreaming(), false);
    QCOMPARE(chat.streamingText(), QString());
    QCOMPARE(chat.messages().size(), std::size_t{1});
    QCOMPARE(kReplySpy.count(), 0);
    QCOMPARE(kFailedSpy.count(), 0);
    QCOMPARE(kCancelledSpy.count(), 1);
    QCOMPARE(kStateSpy.count(), 1);
    QCOMPARE(kTextSpy.count(), 2);
    QCOMPARE(answer->isCancelled(), true);
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
    QCOMPARE(kFailedSpy.at(0).at(0).value<ProviderError>(), ProviderError::kNetwork);
    QCOMPARE(kFailedSpy.at(0).at(1).toString(), QStringLiteral("offline"));
    QCOMPARE(chat.hasError(), true);
    QCOMPARE(chat.errorText(), QStringLiteral("Network error: offline"));
    QCOMPARE(kStateSpy.count(), 1);
    QCOMPARE(kTextSpy.count(), 2);
}

void TestChat::test_failureExposesErrorText() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));
    const QSignalSpy kErrorSpy(&chat, &Chat::errorTextChanged);

    fake->lastAnswer()->fail(ProviderError::kAuthentication, QStringLiteral("invalid key"));

    QCOMPARE(chat.hasError(), true);
    QCOMPARE(chat.errorText(), QStringLiteral("Authentication failed: invalid key"));
    QCOMPARE(kErrorSpy.count(), 1);
}

void TestChat::test_failureWithoutDetailUsesSummary() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));

    fake->lastAnswer()->fail(ProviderError::kRateLimit, QString());

    // The typed summary alone is still a user-visible reason, so an empty detail
    // does not hide the failure.
    QCOMPARE(chat.hasError(), true);
    QCOMPARE(chat.errorText(), QStringLiteral("Rate limit exceeded"));
}

void TestChat::test_failureTextMapsEveryProviderError_data() {
    QTest::addColumn<ProviderError>("error");
    QTest::addColumn<QString>("expected");

    QTest::newRow("network") << ProviderError::kNetwork << QStringLiteral("Network error: detail");
    QTest::newRow("authentication") << ProviderError::kAuthentication
                                    << QStringLiteral("Authentication failed: detail");
    QTest::newRow("rate-limit") << ProviderError::kRateLimit << QStringLiteral("Rate limit exceeded: detail");
    QTest::newRow("invalid-response") << ProviderError::kInvalidResponse
                                      << QStringLiteral("Invalid response from provider: detail");
    QTest::newRow("unknown") << ProviderError::kUnknown << QStringLiteral("Unexpected error: detail");
}

void TestChat::test_failureTextMapsEveryProviderError() {
    // QFETCH cannot declare a const variable; clang-tidy's const-correctness
    // check is suppressed for the same reason as in the other data-driven tests.
    QFETCH(ProviderError, error);  // NOLINT(misc-const-correctness)
    QFETCH(QString, expected);     // NOLINT(misc-const-correctness)

    QCOMPARE(errorTextAfterFailure(error, QStringLiteral("detail")), expected);
}

void TestChat::test_sendClearsPreviousError() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));
    fake->lastAnswer()->fail(ProviderError::kAuthentication, QStringLiteral("invalid key"));
    QCOMPARE(chat.hasError(), true);
    const QSignalSpy kErrorSpy(&chat, &Chat::errorTextChanged);

    chat.send(QStringLiteral("try again"));

    QCOMPARE(chat.hasError(), false);
    QCOMPARE(chat.errorText(), QString());
    QCOMPARE(kErrorSpy.count(), 1);
    QCOMPARE(chat.messages().size(), std::size_t{2});
    QCOMPARE(fake->respondCount(), 2);
}

void TestChat::test_cancelDoesNotSetError() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));

    chat.cancel();

    // Cancellation is a user intent, not a failure: it must not leave an error
    // banner behind.
    QCOMPARE(chat.hasError(), false);
    QCOMPARE(chat.errorText(), QString());
    QCOMPARE(fake->lastAnswer()->isCancelled(), true);
}

void TestChat::test_cancelledFailureDoesNotSetError() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));
    const QSignalSpy kFailedSpy(&chat, &Chat::failed);

    // Defensive: a provider must not turn kCancelled into a visible error even
    // if it reports it through fail() instead of cancel().
    fake->lastAnswer()->fail(ProviderError::kCancelled, QStringLiteral("cancelled by user"));

    QCOMPARE(chat.hasError(), false);
    QCOMPARE(chat.errorText(), QString());
    QCOMPARE(kFailedSpy.count(), 1);
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

void TestChat::test_cancelWithoutStreamingIsNoOp() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    const QSignalSpy kStateSpy(&chat, &Chat::stateChanged);
    const QSignalSpy kCancelledSpy(&chat, &Chat::cancelled);

    chat.cancel();

    QCOMPARE(chat.isStreaming(), false);
    QCOMPARE(chat.messages().size(), std::size_t{0});
    QCOMPARE(kCancelledSpy.count(), 0);
    QCOMPARE(kStateSpy.count(), 0);
    QCOMPARE(fake->respondCount(), 0);
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

void TestChat::test_emptyTextIsIgnored() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));

    chat.send(QString());

    QCOMPARE(chat.messages().size(), std::size_t{0});
    QCOMPARE(chat.isStreaming(), false);
    QCOMPARE(chat.rowCount(), 0);
    QCOMPARE(fake->respondCount(), 0);
}

void TestChat::test_providerDestroyingAnswerResetsStreaming() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));
    fake->lastAnswer()->appendChunk(QStringLiteral("partial"));
    const QSignalSpy kStateSpy(&chat, &Chat::stateChanged);

    fake->destroyLastAnswer();

    QCOMPARE(chat.isStreaming(), false);
    QCOMPARE(chat.streamingText(), QString());
    QCOMPARE(chat.messages().size(), std::size_t{1});
    QCOMPARE(kStateSpy.count(), 1);

    // The chat stays usable after the provider disposed of its answer.
    chat.send(QStringLiteral("again"));
    QCOMPARE(chat.isStreaming(), true);
    QCOMPARE(chat.messages().size(), std::size_t{2});
}

void TestChat::test_modelExposesMessageRoles() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));
    fake->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("hello")));

    QCOMPARE(chat.roleNames().value(Chat::kTextRole), QByteArrayLiteral("text"));
    QCOMPARE(chat.roleNames().value(Chat::kIsUserRole), QByteArrayLiteral("isUser"));
    QCOMPARE(chat.roleNames().value(Chat::kMarkupRole), QByteArrayLiteral("markup"));
    QCOMPARE(chat.data(chat.index(0), Chat::kTextRole).toString(), QStringLiteral("hi"));
    QCOMPARE(chat.data(chat.index(0), Chat::kIsUserRole).toBool(), true);
    QCOMPARE(chat.data(chat.index(1), Chat::kTextRole).toString(), QStringLiteral("hello"));
    QCOMPARE(chat.data(chat.index(1), Chat::kIsUserRole).toBool(), false);
}

void TestChat::test_modelInsertsUserMessageRow() {
    auto provider = std::make_unique<FakeProvider>();
    Chat chat(std::move(provider));
    const QSignalSpy kInsertedSpy(&chat, &Chat::rowsInserted);

    chat.send(QStringLiteral("hi"));

    QCOMPARE(kInsertedSpy.count(), 1);
    QCOMPARE(kInsertedSpy.at(0).at(1).toInt(), 0);
    QCOMPARE(kInsertedSpy.at(0).at(2).toInt(), 0);
    QCOMPARE(chat.rowCount(), 1);
}

void TestChat::test_modelInsertsReplyRow() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));
    const QSignalSpy kInsertedSpy(&chat, &Chat::rowsInserted);

    fake->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("hello")));

    QCOMPARE(kInsertedSpy.count(), 1);
    QCOMPARE(kInsertedSpy.at(0).at(1).toInt(), 1);
    QCOMPARE(kInsertedSpy.at(0).at(2).toInt(), 1);
    QCOMPARE(chat.rowCount(), 2);
    QCOMPARE(chat.data(chat.index(1), Chat::kTextRole).toString(), QStringLiteral("hello"));
}

void TestChat::test_modelIgnoresUnknownRoleAndOutOfRangeRow() {
    auto provider = std::make_unique<FakeProvider>();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));

    QVERIFY(!chat.data(chat.index(0), Qt::DisplayRole).isValid());
    QVERIFY(!chat.data(QModelIndex(), Chat::kTextRole).isValid());
    QVERIFY(!chat.data(chat.index(1), Chat::kTextRole).isValid());
    QVERIFY(!chat.data(chat.index(1), Chat::kMarkupRole).isValid());
    QCOMPARE(chat.rowCount(chat.index(0)), 0);
}

void TestChat::test_streamingTextIsNotAModelRow() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));

    fake->lastAnswer()->appendChunk(QStringLiteral("partial"));

    // The live reply never touches the history: only the user message is a row,
    // and the streaming text is exposed as its own property.
    QCOMPARE(chat.rowCount(), 1);
    QCOMPARE(chat.data(chat.index(0), Chat::kIsUserRole).toBool(), true);
    QCOMPARE(chat.streamingText(), QStringLiteral("partial"));
}

void TestChat::test_markupRoleKeepsUserTextVerbatim() {
    auto provider = std::make_unique<FakeProvider>();
    Chat chat(std::move(provider));

    // A user's own message is shown as plain text, so it must reach the view
    // exactly as typed - Markdown-looking characters are not rewritten.
    chat.send(QStringLiteral("<b>hi</b> **not bold**"));

    QCOMPARE(chat.data(chat.index(0), Chat::kMarkupRole).toString(), QStringLiteral("<b>hi</b> **not bold**"));
}

void TestChat::test_markupRoleSanitisesAssistantText() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));
    fake->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("**bold** <script>x</script>")));

    const QString kMarkup = chat.data(chat.index(1), Chat::kMarkupRole).toString();

    // The assistant's Markdown is rendered, but its HTML never reaches the view.
    QVERIFY(kMarkup.contains(QStringLiteral("**bold**")));
    QVERIFY(!hasUnescapedAngleBracket(kMarkup));
}

void TestChat::test_streamingMarkupSanitisesLiveAnswer() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));

    fake->lastAnswer()->appendChunk(QStringLiteral("**bold** <img src=x>"));

    // The raw streaming text is kept for tests/telemetry, while the display
    // property is the sanitised Markdown the streaming tail renders.
    QCOMPARE(chat.streamingText(), QStringLiteral("**bold** <img src=x>"));
    const QString kMarkup = chat.streamingMarkup();
    QVERIFY(kMarkup.contains(QStringLiteral("**bold**")));
    QVERIFY(!hasUnescapedAngleBracket(kMarkup));
}

void TestChat::test_markupRoleIsMemoisedAcrossReads() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));
    fake->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("**bold** <script>x</script>")));

    // The display markup is stable across the many reads a view performs.
    const QString kFirst = chat.data(chat.index(1), Chat::kMarkupRole).toString();
    const QString kSecond = chat.data(chat.index(1), Chat::kMarkupRole).toString();
    QCOMPARE(kFirst, kSecond);

    // Appending another row must not shift or invalidate an earlier row's cache.
    chat.send(QStringLiteral("again"));
    fake->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("second")));
    QCOMPARE(chat.data(chat.index(1), Chat::kMarkupRole).toString(), kFirst);
}

void TestChat::test_streamingMarkupIsRecomputedOnlyWhenTextChanges() {
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("hi"));
    fake->lastAnswer()->appendChunk(QStringLiteral("**bold**"));

    const QString kFirst = chat.streamingMarkup();
    // Repeated reads of the same live text return the memoised value.
    QCOMPARE(chat.streamingMarkup(), kFirst);
    QVERIFY(chat.streamingMarkup().contains(QStringLiteral("**bold**")));

    // A new chunk invalidates the memo and is sanitised again.
    fake->lastAnswer()->appendChunk(QStringLiteral(" <img src=x>"));
    QVERIFY(chat.streamingMarkup() != kFirst);
    QVERIFY(!hasUnescapedAngleBracket(chat.streamingMarkup()));
}

// The chat model exposes sanitised Markdown (SafeMarkdown), which uses
// QTextDocument and therefore needs a QGuiApplication (QTEST_MAIN, offscreen).
QTEST_MAIN(TestChat)

#include "tst_chat.moc"
