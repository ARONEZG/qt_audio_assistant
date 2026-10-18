#include <QAbstractItemModel>
#include <QByteArray>
#include <QModelIndex>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QtTest>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "FakeProvider.h"
#include "domain/Chat.h"
#include "domain/Chats.h"
#include "domain/Message.h"
#include "domain/Provider.h"
#include "domain/ProviderOffer.h"
#include "domain/Providers.h"

namespace {

// An offer whose minted provider reports the same identity as the offer.
ProviderOffer offerOf(const QString& id, const QString& displayName) {
    return {id, displayName, [id, displayName] { return std::make_unique<FakeProvider>(id, displayName); }};
}

// An offer that also records every provider it mints, so a test can act as the
// network for chats whose providers it does not own.
ProviderOffer recordingOffer(const QString& id, std::vector<FakeProvider*>& minted) {
    return {id, id, [id, &minted] {
                auto provider = std::make_unique<FakeProvider>(id, id);
                minted.push_back(provider.get());
                return provider;
            }};
}

Providers singleFakeOffer() {
    return Providers({offerOf(QStringLiteral("fake"), QStringLiteral("Fake Provider"))});
}

}  // namespace

class TestChats : public QObject {
    Q_OBJECT

public:
    explicit TestChats(QObject* parent = nullptr);

private slots:
    void test_emptyGridHasNoRows();
    void test_addCreatesOwnedChat();
    void test_addInsertsOneRow();
    void test_addRejectsNullProvider();
    void test_addRejectsProviderOfAnUnknownId();
    void test_atOutOfRangeIsNothing();
    void test_rowCountIgnoresChildren();
    void test_addedChatsKeepInsertionOrder();
    void test_removeDropsTheChat();
    void test_removeDestroysTheChat();
    void test_removeOutOfRangeIsNoOp();
    void test_clearRemovesEveryChat();
    void test_clearOnEmptyCollectionIsNoOp();
    void test_modelExposesTheChatOfARow();
    void test_modelRejectsUnknownRole();
    void test_modelRejectsInvalidIndex();
    void test_modelRejectsIndexOfAnotherModel();
    void test_eachChatOwnsItsOwnProviderInstance();
    void test_chatStreamsThroughTheProviderItWasGiven();
};

TestChats::TestChats(QObject* parent) : QObject(parent) {
}

void TestChats::test_emptyGridHasNoRows() {
    const Chats kChats;

    QCOMPARE(kChats.count(), 0);
    QCOMPARE(kChats.rowCount(), 0);
    QVERIFY(!kChats.data(QModelIndex(), Chats::kChatRole).isValid());
}

void TestChats::test_addCreatesOwnedChat() {
    const Providers kProviders(singleFakeOffer());
    Chats chats;

    Chat* chat = chats.add(kProviders.fresh(QStringLiteral("fake")));

    QVERIFY(chat != nullptr);
    QCOMPARE(chats.count(), 1);
    QCOMPARE(chats.at(0), chat);
    QCOMPARE(chat->providerId(), QStringLiteral("fake"));
    QCOMPARE(chat->providerName(), QStringLiteral("Fake Provider"));
    QCOMPARE(chat->messages().size(), std::size_t{0});
}

void TestChats::test_addInsertsOneRow() {
    const Providers kProviders(singleFakeOffer());
    Chats chats;
    const QSignalSpy kInsertedSpy(&chats, &Chats::rowsInserted);

    QVERIFY(chats.add(kProviders.fresh(QStringLiteral("fake"))) != nullptr);

    QCOMPARE(kInsertedSpy.count(), 1);
    QCOMPARE(kInsertedSpy.at(0).at(1).toInt(), 0);
    QCOMPARE(kInsertedSpy.at(0).at(2).toInt(), 0);
    QCOMPARE(chats.count(), 1);
    QCOMPARE(chats.rowCount(), 1);
}

void TestChats::test_addRejectsNullProvider() {
    Chats chats;
    const QSignalSpy kInsertedSpy(&chats, &Chats::rowsInserted);

    QVERIFY(chats.add(nullptr) == nullptr);

    QCOMPARE(chats.count(), 0);
    QCOMPARE(kInsertedSpy.count(), 0);
}

void TestChats::test_addRejectsProviderOfAnUnknownId() {
    const Providers kProviders(singleFakeOffer());
    Chats chats;
    const QSignalSpy kInsertedSpy(&chats, &Chats::rowsInserted);

    // The id is not offered, so Providers::fresh() has no provider to hand over.
    QVERIFY(chats.add(kProviders.fresh(QStringLiteral("gone"))) == nullptr);

    QCOMPARE(chats.count(), 0);
    QCOMPARE(kInsertedSpy.count(), 0);
}

void TestChats::test_atOutOfRangeIsNothing() {
    const Providers kProviders(singleFakeOffer());
    Chats chats;
    QVERIFY(chats.add(kProviders.fresh(QStringLiteral("fake"))) != nullptr);

    QVERIFY(chats.at(-1) == nullptr);
    QVERIFY(chats.at(chats.count()) == nullptr);
    QCOMPARE(chats.count(), 1);
}

void TestChats::test_rowCountIgnoresChildren() {
    const Providers kProviders(singleFakeOffer());
    Chats chats;
    QVERIFY(chats.add(kProviders.fresh(QStringLiteral("fake"))) != nullptr);

    QCOMPARE(chats.rowCount(chats.index(0)), 0);
}

void TestChats::test_addedChatsKeepInsertionOrder() {
    const Providers kProviders({offerOf(QStringLiteral("alpha"), QStringLiteral("Alpha")),
                                offerOf(QStringLiteral("beta"), QStringLiteral("Beta"))});
    Chats chats;

    QVERIFY(chats.add(kProviders.fresh(QStringLiteral("beta"))) != nullptr);
    QVERIFY(chats.add(kProviders.fresh(QStringLiteral("alpha"))) != nullptr);

    QCOMPARE(chats.count(), 2);
    QCOMPARE(chats.at(0)->providerId(), QStringLiteral("beta"));
    QCOMPARE(chats.at(1)->providerId(), QStringLiteral("alpha"));
}

void TestChats::test_removeDropsTheChat() {
    const Providers kProviders({offerOf(QStringLiteral("alpha"), QStringLiteral("Alpha")),
                                offerOf(QStringLiteral("beta"), QStringLiteral("Beta"))});
    Chats chats;
    QVERIFY(chats.add(kProviders.fresh(QStringLiteral("alpha"))) != nullptr);
    Chat* second = chats.add(kProviders.fresh(QStringLiteral("beta")));
    QVERIFY(second != nullptr);
    const QSignalSpy kRemovedSpy(&chats, &Chats::rowsRemoved);

    QVERIFY(chats.remove(0));

    QCOMPARE(chats.count(), 1);
    QCOMPARE(chats.at(0), second);
    QCOMPARE(chats.at(0)->providerId(), QStringLiteral("beta"));
    QCOMPARE(kRemovedSpy.count(), 1);
    QCOMPARE(kRemovedSpy.at(0).at(1).toInt(), 0);
    QCOMPARE(kRemovedSpy.at(0).at(2).toInt(), 0);
}

void TestChats::test_removeDestroysTheChat() {
    const Providers kProviders(singleFakeOffer());
    Chats chats;
    Chat* chat = chats.add(kProviders.fresh(QStringLiteral("fake")));
    QVERIFY(chat != nullptr);
    const QSignalSpy kDestroyedSpy(chat, &QObject::destroyed);

    QVERIFY(chats.remove(0));

    QCOMPARE(kDestroyedSpy.count(), 1);
}

void TestChats::test_removeOutOfRangeIsNoOp() {
    const Providers kProviders(singleFakeOffer());
    Chats chats;
    QVERIFY(chats.add(kProviders.fresh(QStringLiteral("fake"))) != nullptr);
    const QSignalSpy kRemovedSpy(&chats, &Chats::rowsRemoved);

    QCOMPARE(chats.remove(-1), false);
    QCOMPARE(chats.remove(chats.count()), false);

    QCOMPARE(chats.count(), 1);
    QCOMPARE(kRemovedSpy.count(), 0);
}

// Switching the provider of the single open chat drops the previous chat as a
// whole: every row is removed, the model is reset and the chats are destroyed.
void TestChats::test_clearRemovesEveryChat() {
    const Providers kProviders({offerOf(QStringLiteral("alpha"), QStringLiteral("Alpha")),
                                offerOf(QStringLiteral("beta"), QStringLiteral("Beta"))});
    Chats chats;
    Chat* first = chats.add(kProviders.fresh(QStringLiteral("alpha")));
    QVERIFY(first != nullptr);
    QVERIFY(chats.add(kProviders.fresh(QStringLiteral("beta"))) != nullptr);
    const QSignalSpy kResetSpy(&chats, &QAbstractItemModel::modelReset);
    const QSignalSpy kCountSpy(&chats, &Chats::countChanged);
    const QSignalSpy kDestroyedSpy(first, &QObject::destroyed);

    chats.clear();

    QCOMPARE(chats.count(), 0);
    QCOMPARE(chats.rowCount(), 0);
    QCOMPARE(kResetSpy.count(), 1);
    QCOMPARE(kCountSpy.count(), 1);
    QCOMPARE(kDestroyedSpy.count(), 1);
}

void TestChats::test_clearOnEmptyCollectionIsNoOp() {
    Chats chats;
    const QSignalSpy kResetSpy(&chats, &QAbstractItemModel::modelReset);
    const QSignalSpy kCountSpy(&chats, &Chats::countChanged);

    chats.clear();

    QCOMPARE(chats.count(), 0);
    QCOMPARE(kResetSpy.count(), 0);
    QCOMPARE(kCountSpy.count(), 0);
}

void TestChats::test_modelExposesTheChatOfARow() {
    const Providers kProviders(singleFakeOffer());
    Chats chats;
    Chat* chat = chats.add(kProviders.fresh(QStringLiteral("fake")));
    QVERIFY(chat != nullptr);

    QCOMPARE(chats.roleNames().value(Chats::kChatRole), QByteArrayLiteral("chat"));
    QCOMPARE(chats.data(chats.index(0), Chats::kChatRole).value<Chat*>(), chat);
}

void TestChats::test_modelRejectsUnknownRole() {
    const Providers kProviders(singleFakeOffer());
    Chats chats;
    QVERIFY(chats.add(kProviders.fresh(QStringLiteral("fake"))) != nullptr);

    QVERIFY(!chats.data(chats.index(0), Qt::DisplayRole).isValid());
}

void TestChats::test_modelRejectsInvalidIndex() {
    const Chats kChats;

    QVERIFY(!kChats.data(QModelIndex(), Chats::kChatRole).isValid());
    QVERIFY(!kChats.data(kChats.index(0), Chats::kChatRole).isValid());
}

void TestChats::test_modelRejectsIndexOfAnotherModel() {
    const Providers kProviders(singleFakeOffer());
    Chats chats;
    QVERIFY(chats.add(kProviders.fresh(QStringLiteral("fake"))) != nullptr);
    Chats other;
    QVERIFY(other.add(kProviders.fresh(QStringLiteral("fake"))) != nullptr);
    QVERIFY(other.add(kProviders.fresh(QStringLiteral("fake"))) != nullptr);

    // A valid index of another model can name a row this model does not have.
    QVERIFY(!chats.data(other.index(1), Chats::kChatRole).isValid());
}

void TestChats::test_eachChatOwnsItsOwnProviderInstance() {
    std::vector<FakeProvider*> minted;
    const Providers kProviders({recordingOffer(QStringLiteral("fake"), minted)});
    Chats chats;

    Chat* first = chats.add(kProviders.fresh(QStringLiteral("fake")));
    Chat* second = chats.add(kProviders.fresh(QStringLiteral("fake")));
    QVERIFY(first != nullptr);
    QVERIFY(second != nullptr);

    QCOMPARE(minted.size(), std::size_t{2});
    QVERIFY(minted.at(0) != minted.at(1));

    first->send(QStringLiteral("one"));

    QCOMPARE(first->isStreaming(), true);
    QCOMPARE(second->isStreaming(), false);
    QCOMPARE(minted.at(1)->respondCount(), 0);

    minted.at(0)->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("a")));

    QCOMPARE(first->messages().size(), std::size_t{2});
    QCOMPARE(second->messages().size(), std::size_t{0});
}

void TestChats::test_chatStreamsThroughTheProviderItWasGiven() {
    std::vector<FakeProvider*> minted;
    const Providers kProviders({recordingOffer(QStringLiteral("fake"), minted)});
    Chats chats;
    Chat* chat = chats.add(kProviders.fresh(QStringLiteral("fake")));
    QVERIFY(chat != nullptr);

    chat->send(QStringLiteral("hi"));

    QCOMPARE(minted.size(), std::size_t{1});
    QCOMPARE(minted.at(0)->lastHistory().size(), std::size_t{1});
    QCOMPARE(minted.at(0)->lastHistory().at(0).text(), QStringLiteral("hi"));

    minted.at(0)->lastAnswer()->appendChunk(QStringLiteral("hello"));
    QCOMPARE(chat->streamingText(), QStringLiteral("hello"));

    minted.at(0)->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("hello")));
    QCOMPARE(chat->messages().size(), std::size_t{2});
    QCOMPARE(chat->messages().at(1).text(), QStringLiteral("hello"));
    QCOMPARE(chat->streamingText(), QString());
}

QTEST_GUILESS_MAIN(TestChats)

#include "tst_chats.moc"
