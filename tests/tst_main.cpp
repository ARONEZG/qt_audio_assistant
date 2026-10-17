#include <QGuiApplication>
#include <QMetaObject>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>
#include <QUrl>
#include <QVariant>
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

// Walks the item tree (not the QObject tree): items created by a positioner or a
// view are visual children, which QObject::findChild does not reach from the
// window. Iterative rather than recursive: the tree depth is data, not a
// compile-time bound.
QObject* findInItems(QQuickItem* root, const QString& name) {
    std::vector<QQuickItem*> pending{root};
    while (!pending.empty()) {
        QQuickItem* item = pending.back();
        pending.pop_back();
        if (item == nullptr) {
            continue;
        }
        if (item->objectName() == name) {
            return item;
        }
        const QList<QQuickItem*> kChildren = item->childItems();
        pending.insert(pending.end(), kChildren.cbegin(), kChildren.cend());
    }
    return nullptr;
}

// Everything a QML run needs, composed exactly as main() does it: a provider
// list, a chat grid and the thin invokable that joins them. The provider is a
// fake, so the test acts as the network and no real request is ever made.
class Harness {
public:
    Harness()
        : providers_({ProviderOffer(QStringLiteral("fake"),
                                    QStringLiteral("Fake Provider"),
                                    [this] {
                                        auto provider = std::make_unique<FakeProvider>(QStringLiteral("fake"),
                                                                                       QStringLiteral("Fake Provider"));
                                        minted_.push_back(provider.get());
                                        return provider;
                                    })}),
          newChat_(chats_, providers_) {
        engine_.setInitialProperties({
                {QStringLiteral("chats"), QVariant::fromValue(&chats_)},
                {QStringLiteral("providers"), QVariant::fromValue(&providers_)},
                {QStringLiteral("newChat"), QVariant::fromValue(&newChat_)},
        });
        engine_.load(QUrl(QStringLiteral("qrc:/qt/qml/AudioAssistant/Main.qml")));
        // A delegate is only instantiated once the window is shown and polished;
        // the offscreen platform needs this explicit nudge.
        (void)QTest::qWaitForWindowExposed(rootWindow());
    }

    [[nodiscard]] Chats& chats() noexcept {
        return chats_;
    }
    [[nodiscard]] const Chats& chats() const noexcept {
        return chats_;
    }
    [[nodiscard]] NewChat& newChat() noexcept {
        return newChat_;
    }
    [[nodiscard]] const std::vector<FakeProvider*>& minted() const noexcept {
        return minted_;
    }
    [[nodiscard]] QObject* root() const {
        return engine_.rootObjects().value(0);
    }
    [[nodiscard]] QObject* child(const QString& name) const;

private:
    [[nodiscard]] QQuickWindow* rootWindow() const {
        return qobject_cast<QQuickWindow*>(root());
    }

    std::vector<FakeProvider*> minted_;
    Providers providers_;
    Chats chats_;
    NewChat newChat_;
    QQmlApplicationEngine engine_;
};

QObject* Harness::child(const QString& name) const {
    if (auto* found = root()->findChild<QObject*>(name)) {
        return found;
    }
    QQuickWindow* window = rootWindow();
    return window == nullptr ? nullptr : findInItems(window->contentItem(), name);
}

}  // namespace

// Offscreen smoke tests for the QML layer: the real module is loaded and driven
// through the controls a user would touch (add a chat, type and send), with a
// fake provider standing in for the network.
class TestQmlChat : public QObject {
    Q_OBJECT

public:
    explicit TestQmlChat(QObject* parent = nullptr);

private slots:
    void test_loadsMainComponent();
    void test_addChatButtonCreatesAChat();
    void test_sendButtonForwardsTypedTextToTheChat();
    void test_streamingTailShowsTheLiveReply();
};

TestQmlChat::TestQmlChat(QObject* parent) : QObject(parent) {
}

void TestQmlChat::test_loadsMainComponent() {
    // Logically const on the C++ side: the test only observes it (QML mutates the
    // injected model through the pointers it was handed, not through this object).
    const Harness kHarness;

    QVERIFY(kHarness.root() != nullptr);
    QCOMPARE(kHarness.chats().count(), 0);
}

void TestQmlChat::test_addChatButtonCreatesAChat() {
    const Harness kHarness;
    QVERIFY(kHarness.root() != nullptr);
    QObject* addButton = kHarness.child(QStringLiteral("addChatButton"));
    QVERIFY(addButton != nullptr);

    QVERIFY(QMetaObject::invokeMethod(addButton, "clicked"));

    QCOMPARE(kHarness.chats().count(), 1);
    QCOMPARE(kHarness.chats().at(0)->providerId(), QStringLiteral("fake"));
    QCOMPARE(kHarness.minted().size(), std::size_t{1});
}

void TestQmlChat::test_sendButtonForwardsTypedTextToTheChat() {
    Harness harness;
    QVERIFY(harness.newChat().open(QStringLiteral("fake")) != nullptr);
    // The cell's Repeater materialises the panel as soon as the model gains a
    // row; one event-loop pass is enough for the composed controls to exist.
    QCoreApplication::processEvents();
    QObject* input = harness.child(QStringLiteral("messageInput"));
    QObject* sendButton = harness.child(QStringLiteral("sendButton"));
    QVERIFY(input != nullptr);
    QVERIFY(sendButton != nullptr);

    QVERIFY(input->setProperty("text", QStringLiteral("hi")));
    QVERIFY(QMetaObject::invokeMethod(sendButton, "clicked"));

    Chat* chat = harness.chats().at(0);
    QCOMPARE(chat->isStreaming(), true);
    QCOMPARE(chat->messages().size(), std::size_t{1});
    QCOMPARE(chat->messages().at(0).text(), QStringLiteral("hi"));
    QCOMPARE(harness.minted().at(0)->lastHistory().size(), std::size_t{1});
}

void TestQmlChat::test_streamingTailShowsTheLiveReply() {
    Harness harness;
    QVERIFY(harness.newChat().open(QStringLiteral("fake")) != nullptr);
    Chat* chat = harness.chats().at(0);
    chat->send(QStringLiteral("hi"));

    harness.minted().at(0)->lastAnswer()->appendChunk(QStringLiteral("Hel"));
    harness.minted().at(0)->lastAnswer()->appendChunk(QStringLiteral("lo"));
    QCOMPARE(chat->streamingText(), QStringLiteral("Hello"));

    // The streaming text is a separate property, not a model row: only the user
    // message is in the transcript while the reply is in flight.
    QCOMPARE(chat->rowCount(), 1);

    harness.minted().at(0)->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("Hello")));
    QCOMPARE(chat->rowCount(), 2);
    QCOMPARE(chat->streamingText(), QString());
}

QTEST_MAIN(TestQmlChat)

#include "tst_main.moc"
