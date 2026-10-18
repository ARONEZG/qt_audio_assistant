#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QtTest>
#include <memory>
#include <utility>
#include <vector>

#include "FakeProvider.h"
#include "domain/Chat.h"
#include "domain/Chats.h"
#include "domain/Message.h"
#include "domain/NewChat.h"
#include "domain/ProviderOffer.h"
#include "domain/Providers.h"

namespace {

// An offer whose minted provider is recorded, so the test can act as the
// "network" for a chat it does not own.
ProviderOffer recordingOffer(const QString& id, std::vector<FakeProvider*>& minted) {
    return {id, id, [id, &minted] {
                auto provider = std::make_unique<FakeProvider>(id, id);
                minted.push_back(provider.get());
                return provider;
            }};
}

// An offer whose recipe deliberately cannot build a provider.
ProviderOffer barrenOffer(const QString& id) {
    return {id, id, [] { return std::unique_ptr<Provider>(); }};
}

}  // namespace

// Covers the composition seam that lets a QML intent reach
// chats.add(providers.fresh(id)) without a controller: NewChat carries exactly
// that one call.
class TestNewChat : public QObject {
    Q_OBJECT

public:
    explicit TestNewChat(QObject* parent = nullptr);

private slots:
    void test_openCreatesAChatFromTheOfferedProvider();
    void test_openWithUnknownIdCreatesNothing();
    void test_openWhenRecipeBuildsNothingCreatesNothing();
    void test_openedChatIsOwnedByTheGrid();
    void test_reopeningReplacesTheProviderKeepingTheChat();
    void test_reopeningPreservesTheConversation();
    void test_reopeningWithUnknownIdKeepsTheOpenChat();
    void test_openedChatStreamsThroughTheMintedProvider();
};

TestNewChat::TestNewChat(QObject* parent) : QObject(parent) {
}

void TestNewChat::test_openCreatesAChatFromTheOfferedProvider() {
    std::vector<FakeProvider*> minted;
    const Providers kProviders({recordingOffer(QStringLiteral("fake"), minted)});
    Chats chats;
    NewChat newChat(chats, kProviders);
    const QSignalSpy kInsertedSpy(&chats, &Chats::rowsInserted);

    Chat* chat = newChat.open(QStringLiteral("fake"));

    QVERIFY(chat != nullptr);
    QCOMPARE(chats.count(), 1);
    QCOMPARE(chats.at(0), chat);
    QCOMPARE(chat->providerId(), QStringLiteral("fake"));
    QCOMPARE(kInsertedSpy.count(), 1);
    QCOMPARE(minted.size(), std::size_t{1});
}

void TestNewChat::test_openWithUnknownIdCreatesNothing() {
    std::vector<FakeProvider*> minted;
    const Providers kProviders({recordingOffer(QStringLiteral("fake"), minted)});
    Chats chats;
    NewChat newChat(chats, kProviders);
    const QSignalSpy kInsertedSpy(&chats, &Chats::rowsInserted);

    QVERIFY(newChat.open(QStringLiteral("gone")) == nullptr);

    QCOMPARE(chats.count(), 0);
    QCOMPARE(kInsertedSpy.count(), 0);
    QCOMPARE(minted.size(), std::size_t{0});
}

void TestNewChat::test_openWhenRecipeBuildsNothingCreatesNothing() {
    const Providers kProviders({barrenOffer(QStringLiteral("fake"))});
    Chats chats;
    NewChat newChat(chats, kProviders);

    QVERIFY(newChat.open(QStringLiteral("fake")) == nullptr);

    QCOMPARE(chats.count(), 0);
}

void TestNewChat::test_openedChatIsOwnedByTheGrid() {
    std::vector<FakeProvider*> minted;
    const Providers kProviders({recordingOffer(QStringLiteral("fake"), minted)});
    Chats chats;
    NewChat newChat(chats, kProviders);
    Chat* chat = newChat.open(QStringLiteral("fake"));
    QVERIFY(chat != nullptr);
    const QSignalSpy kDestroyedSpy(chat, &QObject::destroyed);

    QVERIFY(chats.remove(0));

    QCOMPARE(kDestroyedSpy.count(), 1);
}

void TestNewChat::test_reopeningReplacesTheProviderKeepingTheChat() {
    std::vector<FakeProvider*> minted;
    const Providers kProviders({recordingOffer(QStringLiteral("fake"), minted)});
    Chats chats;
    NewChat newChat(chats, kProviders);
    Chat* chat = newChat.open(QStringLiteral("fake"));
    QVERIFY(chat != nullptr);
    const QSignalSpy kProviderSpy(chat, &Chat::providerChanged);

    // The application holds a single chat: re-opening swaps its provider instead
    // of appending a second chat.
    QCOMPARE(newChat.open(QStringLiteral("fake")), chat);

    QCOMPARE(chats.count(), 1);
    QCOMPARE(chats.at(0), chat);
    QCOMPARE(minted.size(), std::size_t{2});
    QVERIFY(minted.at(0) != minted.at(1));
    QCOMPARE(kProviderSpy.count(), 1);
}

void TestNewChat::test_reopeningPreservesTheConversation() {
    std::vector<FakeProvider*> minted;
    const Providers kProviders({recordingOffer(QStringLiteral("fake"), minted)});
    Chats chats;
    NewChat newChat(chats, kProviders);
    Chat* chat = newChat.open(QStringLiteral("fake"));
    QVERIFY(chat != nullptr);
    chat->send(QStringLiteral("hi"));
    minted.at(0)->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("hello")));
    QCOMPARE(chat->messages().size(), std::size_t{2});

    QCOMPARE(newChat.open(QStringLiteral("fake")), chat);

    // The conversation and the model rows survive the provider swap.
    QCOMPARE(chats.count(), 1);
    QCOMPARE(chat->messages().size(), std::size_t{2});
    QCOMPARE(chat->rowCount(), 2);
}

void TestNewChat::test_reopeningWithUnknownIdKeepsTheOpenChat() {
    std::vector<FakeProvider*> minted;
    const Providers kProviders({recordingOffer(QStringLiteral("fake"), minted)});
    Chats chats;
    NewChat newChat(chats, kProviders);
    Chat* chat = newChat.open(QStringLiteral("fake"));
    QVERIFY(chat != nullptr);

    QVERIFY(newChat.open(QStringLiteral("gone")) == nullptr);

    // A failed lookup must not close the open chat or mint a provider.
    QCOMPARE(chats.count(), 1);
    QCOMPARE(chats.at(0), chat);
    QCOMPARE(minted.size(), std::size_t{1});
}

void TestNewChat::test_openedChatStreamsThroughTheMintedProvider() {
    std::vector<FakeProvider*> minted;
    const Providers kProviders({recordingOffer(QStringLiteral("fake"), minted)});
    Chats chats;
    NewChat newChat(chats, kProviders);
    Chat* chat = newChat.open(QStringLiteral("fake"));
    QVERIFY(chat != nullptr);

    chat->send(QStringLiteral("hi"));
    QCOMPARE(minted.size(), std::size_t{1});
    minted.at(0)->lastAnswer()->appendChunk(QStringLiteral("hello"));
    QCOMPARE(chat->streamingText(), QStringLiteral("hello"));
    minted.at(0)->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("hello")));

    QCOMPARE(chat->messages().size(), std::size_t{2});
    QCOMPARE(chat->streamingText(), QString());
}

QTEST_GUILESS_MAIN(TestNewChat)

#include "tst_newchat.moc"
