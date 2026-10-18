#include <QFile>
#include <QGuiApplication>
#include <QMetaObject>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>
#include <QTemporaryDir>
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
#include "domain/ProviderConfigFile.h"
#include "domain/ProviderError.h"
#include "domain/ProviderOffer.h"
#include "domain/ProviderSettings.h"
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

// Reads a UTF-8 text file for assertions; returns a null QString when the file
// cannot be opened, which fails the following comparison.
[[nodiscard]] QString readTextFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

// Every item with the given objectName, in contrast to findInItems which stops at
// the first match: a transcript has one delegate (and one label) per message.
[[nodiscard]] std::vector<QQuickItem*> collectItems(QQuickItem* root, const QString& name) {
    std::vector<QQuickItem*> found;
    std::vector<QQuickItem*> pending{root};
    while (!pending.empty()) {
        QQuickItem* item = pending.back();
        pending.pop_back();
        if (item == nullptr) {
            continue;
        }
        if (item->objectName() == name) {
            found.push_back(item);
        }
        const QList<QQuickItem*> kChildren = item->childItems();
        pending.insert(pending.end(), kChildren.cbegin(), kChildren.cend());
    }
    return found;
}

// The label Text of the one delegate whose `isUser` matches, or nullptr while no
// such delegate has been materialised yet.
[[nodiscard]] QObject* messageLabel(QQuickWindow* window, bool isUser) {
    for (QQuickItem* delegate : collectItems(window->contentItem(), QStringLiteral("messageDelegate"))) {
        if (delegate->property("isUser").toBool() == isUser) {
            return findInItems(delegate, QStringLiteral("messageText"));
        }
    }
    return nullptr;
}

// Mirrors SafeMarkdown's invariant: no '<' may reach the renderer unescaped.
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

// Everything a QML run needs, composed exactly as main() does it: a provider
// list, a chat grid and the thin invokable that joins them. The provider is a
// fake, so the test acts as the network and no real request is ever made.
class Harness {
public:
    Harness()
        : configFile_(temporaryDirectory_.filePath(QStringLiteral("provider.env"))),
          settings_({}, {}, configFile_),
          providers_({ProviderOffer(QStringLiteral("fake"),
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
                {QStringLiteral("providerSettings"), QVariant::fromValue(&settings_)},
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
    [[nodiscard]] ProviderSettings& settings() noexcept {
        return settings_;
    }
    [[nodiscard]] const ProviderConfigFile& configFile() const noexcept {
        return configFile_;
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

    QTemporaryDir temporaryDirectory_;
    ProviderConfigFile configFile_;
    ProviderSettings settings_;
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
    void test_messageDelegateRendersAssistantMarkdownAndUserPlainText();
    void test_streamingTailRendersSanitisedMarkdown();
    void test_chatFailureShowsTheError();
    void test_providerSettingsFieldsAreBound();
    void test_apiKeyFieldIsMasked();
    void test_saveButtonPersistsProviderSettings();
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

void TestQmlChat::test_messageDelegateRendersAssistantMarkdownAndUserPlainText() {
    Harness harness;
    QVERIFY(harness.newChat().open(QStringLiteral("fake")) != nullptr);
    QCoreApplication::processEvents();
    Chat* chat = harness.chats().at(0);
    chat->send(QStringLiteral("<b>hi</b>"));
    harness.minted().at(0)->lastAnswer()->complete(
            Message(MessageRole::kAssistant, QStringLiteral("**bold** <script>x</script>")));
    QCoreApplication::processEvents();

    auto* window = qobject_cast<QQuickWindow*>(harness.root());
    QVERIFY(window != nullptr);
    // The ListView materialises delegates across event-loop passes, so poll for
    // both rows before inspecting them (QTest::qWaitFor, no fixed sleep).
    QVERIFY(QTest::qWaitFor(
            [window] { return collectItems(window->contentItem(), QStringLiteral("messageDelegate")).size() == 2; }));

    QObject* userLabel = messageLabel(window, true);
    QObject* assistantLabel = messageLabel(window, false);
    QVERIFY(userLabel != nullptr);
    QVERIFY(assistantLabel != nullptr);

    // The user's own message is plain text, shown exactly as typed.
    QCOMPARE(userLabel->property("textFormat").toInt(), static_cast<int>(Qt::PlainText));
    QCOMPARE(userLabel->property("text").toString(), QStringLiteral("<b>hi</b>"));

    // The assistant's reply is Markdown, sanitised in C++ first.
    QCOMPARE(assistantLabel->property("textFormat").toInt(), static_cast<int>(Qt::MarkdownText));
    const QString kAssistantText = assistantLabel->property("text").toString();
    QVERIFY(kAssistantText.contains(QStringLiteral("**bold**")));
    QVERIFY(!hasUnescapedAngleBracket(kAssistantText));
}

void TestQmlChat::test_streamingTailRendersSanitisedMarkdown() {
    Harness harness;
    QVERIFY(harness.newChat().open(QStringLiteral("fake")) != nullptr);
    Chat* chat = harness.chats().at(0);
    chat->send(QStringLiteral("hi"));
    harness.minted().at(0)->lastAnswer()->appendChunk(QStringLiteral("**live** <script>bad</script>"));
    QCoreApplication::processEvents();

    QObject* tail = harness.child(QStringLiteral("streamingTail"));
    QVERIFY(tail != nullptr);
    QCOMPARE(tail->property("visible").toBool(), true);
    QCOMPARE(tail->property("textFormat").toInt(), static_cast<int>(Qt::MarkdownText));
    const QString kText = tail->property("text").toString();
    QVERIFY(kText.contains(QStringLiteral("**live**")));
    QVERIFY(!hasUnescapedAngleBracket(kText));
}

void TestQmlChat::test_chatFailureShowsTheError() {
    Harness harness;
    QVERIFY(harness.newChat().open(QStringLiteral("fake")) != nullptr);
    QCoreApplication::processEvents();
    QObject* errorLabel = harness.child(QStringLiteral("chatError"));
    QVERIFY(errorLabel != nullptr);
    QCOMPARE(errorLabel->property("visible").toBool(), false);

    Chat* chat = harness.chats().at(0);
    chat->send(QStringLiteral("hi"));
    harness.minted().at(0)->lastAnswer()->fail(ProviderError::kAuthentication, QStringLiteral("invalid key"));
    QCoreApplication::processEvents();

    // The panel only mirrors the chat's state: the failure text and visibility
    // come from the C++ properties, not from logic in the view.
    QCOMPARE(chat->hasError(), true);
    QCOMPARE(errorLabel->property("visible").toBool(), true);
    QCOMPARE(errorLabel->property("text").toString(), chat->errorText());
    QVERIFY(errorLabel->property("text").toString().contains(QStringLiteral("invalid key")));

    // A new request clears the error in C++, and the view follows.
    chat->send(QStringLiteral("retry"));
    QCoreApplication::processEvents();
    QCOMPARE(errorLabel->property("visible").toBool(), false);
    QCOMPARE(errorLabel->property("text").toString(), QString());
}

void TestQmlChat::test_providerSettingsFieldsAreBound() {
    Harness harness;
    QVERIFY(harness.settings().save(QStringLiteral("https://bound.test/v1"), QStringLiteral("bound-model"), QString()));
    QCoreApplication::processEvents();

    QObject* baseUrlField = harness.child(QStringLiteral("providerBaseUrlField"));
    QObject* modelField = harness.child(QStringLiteral("providerModelField"));
    QObject* apiKeyField = harness.child(QStringLiteral("providerApiKeyField"));
    QVERIFY(baseUrlField != nullptr);
    QVERIFY(modelField != nullptr);
    QVERIFY(apiKeyField != nullptr);

    QCOMPARE(baseUrlField->property("text").toString(), QStringLiteral("https://bound.test/v1"));
    QCOMPARE(modelField->property("text").toString(), QStringLiteral("bound-model"));
    // The key is write-only: the field never renders the stored secret back.
    QCOMPARE(apiKeyField->property("text").toString(), QString());
}

void TestQmlChat::test_apiKeyFieldIsMasked() {
    Harness harness;
    QVERIFY(harness.settings().save(
            QStringLiteral("https://mask.test/v1"), QStringLiteral("mask-model"), QStringLiteral("secret-key")));
    QCoreApplication::processEvents();

    QObject* apiKeyField = harness.child(QStringLiteral("providerApiKeyField"));
    QVERIFY(apiKeyField != nullptr);

    // Masked input. QQuickTextInput is a private class, so its Password echo
    // mode is pinned numerically here.
    QCOMPARE(apiKeyField->property("echoMode").toInt(), 2);

    // Write-only contract: the secret is not a readable QML property; only the
    // boolean "configured" state is exposed, and the stored key is not rendered
    // back into the field.
    const QMetaObject* kSettingsMeta = harness.settings().metaObject();
    QCOMPARE(kSettingsMeta->indexOfProperty("apiKey"), -1);
    QVERIFY(kSettingsMeta->indexOfProperty("isKeyConfigured") >= 0);
    QCOMPARE(apiKeyField->property("text").toString(), QString());
    QVERIFY(harness.settings().isKeyConfigured());
}

void TestQmlChat::test_saveButtonPersistsProviderSettings() {
    Harness harness;
    QCoreApplication::processEvents();

    QObject* baseUrlField = harness.child(QStringLiteral("providerBaseUrlField"));
    QObject* modelField = harness.child(QStringLiteral("providerModelField"));
    QObject* apiKeyField = harness.child(QStringLiteral("providerApiKeyField"));
    QObject* saveButton = harness.child(QStringLiteral("saveProviderSettingsButton"));
    QVERIFY(baseUrlField != nullptr);
    QVERIFY(modelField != nullptr);
    QVERIFY(apiKeyField != nullptr);
    QVERIFY(saveButton != nullptr);

    // The form holds a local draft; the key is write-only and reaches C++ only
    // when the save command is invoked.
    QVERIFY(baseUrlField->setProperty("text", QStringLiteral("https://panel.test/v1")));
    QVERIFY(modelField->setProperty("text", QStringLiteral("panel-model")));
    QVERIFY(apiKeyField->setProperty("text", QStringLiteral("panel-secret")));
    QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));

    // A failed save leaves the reason in lastError; assert it before reading the
    // file so a failure points at the cause, not at a missing file.
    QVERIFY(harness.settings().lastError().isEmpty());
    QCOMPARE(harness.settings().baseUrl(), QStringLiteral("https://panel.test/v1"));
    QCOMPARE(harness.settings().model(), QStringLiteral("panel-model"));
    QCOMPARE(harness.settings().apiKey(), QStringLiteral("panel-secret"));
    QVERIFY(harness.settings().isKeyConfigured());
    // The key field is cleared after a successful save: the secret lives only in
    // the C++ session, not in the view.
    QCOMPARE(apiKeyField->property("text").toString(), QString());

    const QString kText = readTextFile(harness.configFile().path());
    QVERIFY(kText.contains(QStringLiteral("POLZA_BASE_URL=https://panel.test/v1")));
    QVERIFY(kText.contains(QStringLiteral("POLZA_MODEL=panel-model")));
    QVERIFY(!kText.contains(QStringLiteral("panel-secret")));
    QVERIFY(!kText.contains(QStringLiteral("POLZA_API_KEY")));
}

QTEST_MAIN(TestQmlChat)

#include "tst_main.moc"
