#include <QFile>
#include <QGuiApplication>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
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

// The rectangle of `item` that is actually painted, in `root` coordinates: the
// item's laid-out rect intersected with every clipping ancestor. A view clips
// its content, so the laid-out rect alone would report content that never
// reaches the screen.
[[nodiscard]] QRectF paintedRect(QQuickItem* item, QQuickItem* root) {
    QRectF painted = item->mapRectToItem(root, QRectF(0, 0, item->width(), item->height()));
    for (QQuickItem* ancestor = item->parentItem(); ancestor != nullptr; ancestor = ancestor->parentItem()) {
        if (!ancestor->clip()) {
            continue;
        }
        painted =
                painted.intersected(ancestor->mapRectToItem(root, QRectF(0, 0, ancestor->width(), ancestor->height())));
    }
    return painted;
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
        // Mirror main(): the application opens a single chat for the first
        // offered provider before the view is loaded.
        (void)newChat_.open(QStringLiteral("fake"));
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

// Opens the settings dialog and returns it, so the test drives it the way a user
// would before looking up or touching its controls.
[[nodiscard]] QObject* openSettingsDialog(Harness& harness) {
    QObject* dialog = harness.child(QStringLiteral("settingsDialog"));
    if (dialog == nullptr) {
        return nullptr;
    }
    if (!QMetaObject::invokeMethod(dialog, "open")) {
        return nullptr;
    }
    QCoreApplication::processEvents();
    return dialog;
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
    void test_openChatIsShownOnce();
    void test_settingsGearOpensTheDialog();
    void test_sendButtonForwardsTypedTextToTheChat();
    void test_streamingTailShowsTheLiveReply();
    void test_messageDelegateRendersAssistantMarkdownAndUserPlainText();
    void test_streamingTailRendersSanitisedMarkdown();
    void test_streamingTailFollowsEachAnswer();
    void test_streamingTailStaysInsideTheChatPanel();
    void test_chatFailureShowsTheError();
    void test_backgroundCaptureToggleStartsOff();
    void test_backgroundCaptureToggleUpdatesTheChat();
    void test_providerSelectionRebuildsTheChat();
    void test_providerSettingsFieldsAreBound();
    void test_apiKeyFieldIsMasked();
    void test_saveButtonPersistsProviderSettings();
    void test_saveButtonRebuildsTheChat();
};

TestQmlChat::TestQmlChat(QObject* parent) : QObject(parent) {
}

void TestQmlChat::test_loadsMainComponent() {
    // Logically const on the C++ side: the test only observes it (QML mutates the
    // injected model through the pointers it was handed, not through this object).
    const Harness kHarness;

    QVERIFY(kHarness.root() != nullptr);
    // Mirrors main(): a single chat is opened for the first offered provider.
    QCOMPARE(kHarness.chats().count(), 1);
}

void TestQmlChat::test_openChatIsShownOnce() {
    Harness harness;
    QCoreApplication::processEvents();
    auto* window = qobject_cast<QQuickWindow*>(harness.root());
    QVERIFY(window != nullptr);

    QCOMPARE(harness.chats().count(), 1);
    QVERIFY(QTest::qWaitFor(
            [window] { return collectItems(window->contentItem(), QStringLiteral("chatPanel")).size() == 1; }));
}

void TestQmlChat::test_settingsGearOpensTheDialog() {
    const Harness kHarness;
    QObject* gear = kHarness.child(QStringLiteral("settingsButton"));
    QVERIFY(gear != nullptr);
    QObject* dialog = kHarness.child(QStringLiteral("settingsDialog"));
    QVERIFY(dialog != nullptr);
    // The settings live in a dialog, not in the chat: it is closed until asked.
    QCOMPARE(dialog->property("visible").toBool(), false);

    QVERIFY(QMetaObject::invokeMethod(gear, "clicked"));
    QCoreApplication::processEvents();

    QCOMPARE(dialog->property("visible").toBool(), true);
}

void TestQmlChat::test_sendButtonForwardsTypedTextToTheChat() {
    Harness harness;
    // The view materialises the panel from the row the harness opened up front;
    // one event-loop pass is enough for the composed controls to exist.
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
    Chat* chat = harness.chats().at(0);
    chat->send(QStringLiteral("hi"));
    harness.minted().at(0)->lastAnswer()->appendChunk(QStringLiteral("**live** <script>bad</script>"));
    QCoreApplication::processEvents();

    QObject* tail = harness.child(QStringLiteral("streamingTail"));
    QVERIFY(tail != nullptr);
    QCOMPARE(tail->property("visible").toBool(), true);
    QCOMPARE(tail->property("textFormat").toInt(), static_cast<int>(Qt::MarkdownText));
    // The sanitised live markup is republished at the domain's throttle cadence,
    // so poll for the rebound value instead of assuming one event-loop pass.
    QVERIFY(QTest::qWaitFor([tail] { return tail->property("text").toString().contains(QStringLiteral("**live**")); }));
    const QString kText = tail->property("text").toString();
    QVERIFY(!hasUnescapedAngleBracket(kText));
}

// The tail mirrors the in-flight answer as it grows and is reused by the next
// answer: the footer binding is live, not a snapshot taken when the view loaded.
void TestQmlChat::test_streamingTailFollowsEachAnswer() {
    Harness harness;
    QCoreApplication::processEvents();

    const auto kTailText = [&harness] {
        QObject* tail = harness.child(QStringLiteral("streamingTail"));
        return tail == nullptr ? QString() : tail->property("text").toString();
    };

    Chat* chat = harness.chats().at(0);
    chat->send(QStringLiteral("hi"));
    harness.minted().at(0)->lastAnswer()->appendChunk(QStringLiteral("first"));
    QVERIFY(QTest::qWaitFor([&] { return kTailText().contains(QStringLiteral("first")); }));

    // A later chunk reaches the already-visible tail.
    harness.minted().at(0)->lastAnswer()->appendChunk(QStringLiteral(" second"));
    QVERIFY(QTest::qWaitFor([&] { return kTailText().contains(QStringLiteral("first second")); }));

    // Completing the answer freezes the text into a message and hides the tail.
    harness.minted().at(0)->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("first second")));
    QCoreApplication::processEvents();
    QObject* hiddenTail = harness.child(QStringLiteral("streamingTail"));
    QVERIFY(hiddenTail != nullptr);
    QCOMPARE(hiddenTail->property("visible").toBool(), false);

    // The next answer reuses the same tail.
    chat->send(QStringLiteral("again"));
    harness.minted().at(0)->lastAnswer()->appendChunk(QStringLiteral("next"));
    QVERIFY(QTest::qWaitFor([&] { return kTailText().contains(QStringLiteral("next")); }));
}

// Regression: the live reasoning text is longer than the transcript, so an
// uncontained tail painted over the panel border and its neighbours until the
// answer was frozen into a message. The painted tail must stay inside the panel
// it belongs to while it streams.
void TestQmlChat::test_streamingTailStaysInsideTheChatPanel() {
    Harness harness;
    QCoreApplication::processEvents();
    auto* window = qobject_cast<QQuickWindow*>(harness.root());
    QVERIFY(window != nullptr);

    Chat* chat = harness.chats().at(0);
    chat->send(QStringLiteral("hi"));
    // Far taller than the (now window-high) panel: without containment this
    // overflowed the chat. The assistant text is Markdown, where a single newline
    // is not a line break, so the reply wraps by width too - enough text must be
    // supplied for it to exceed the panel whichever way it wraps.
    QString longReply;
    for (int line = 0; line < 1000; ++line) {
        longReply += QStringLiteral("reasoning line %1\n").arg(line);
    }
    harness.minted().at(0)->lastAnswer()->appendChunk(longReply);
    QCoreApplication::processEvents();

    QQuickItem* windowRoot = window->contentItem();
    QQuickItem* tail = nullptr;
    QQuickItem* panel = nullptr;
    // Wait until the long tail is laid out and does overflow the panel, so the
    // assertion below exercises the actual defect rather than an empty layout.
    QVERIFY(QTest::qWaitFor([&] {
        tail = qobject_cast<QQuickItem*>(findInItems(windowRoot, QStringLiteral("streamingTail")));
        panel = qobject_cast<QQuickItem*>(findInItems(windowRoot, QStringLiteral("chatPanel")));
        return tail != nullptr && panel != nullptr && panel->width() > 0 && tail->height() > panel->height();
    }));

    const QRectF kPanel = panel->mapRectToItem(windowRoot, QRectF(0, 0, panel->width(), panel->height()));
    const QRectF kPaintedTail = paintedRect(tail, windowRoot);

    // The tail is on screen (its painted part is non-empty) ...
    QVERIFY(kPaintedTail.height() > 0);
    // ... and entirely within the panel that owns it.
    QVERIFY(kPanel.contains(kPaintedTail));

    // The transcript scrolls itself to the end, so a clipped tail shows the
    // newest reasoning rather than only its first lines.
    QQuickItem* transcript = qobject_cast<QQuickItem*>(findInItems(windowRoot, QStringLiteral("transcript")));
    QVERIFY(transcript != nullptr);
    QVERIFY(transcript->property("contentHeight").toReal() > transcript->height());
    QVERIFY(QTest::qWaitFor([transcript] {
        return transcript->property("contentY").toReal() + transcript->height() >=
               transcript->property("contentHeight").toReal() - 1.0;
    }));
}

void TestQmlChat::test_chatFailureShowsTheError() {
    Harness harness;
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

void TestQmlChat::test_backgroundCaptureToggleStartsOff() {
    Harness harness;
    QCoreApplication::processEvents();
    QObject* toggle = harness.child(QStringLiteral("backgroundCaptureToggle"));
    QVERIFY(toggle != nullptr);

    // The switch lives inside the chat and mirrors the chat's flag; it starts off.
    QCOMPARE(toggle->property("checked").toBool(), false);
    QCOMPARE(harness.chats().at(0)->isBackgroundCaptureEnabled(), false);
}

void TestQmlChat::test_backgroundCaptureToggleUpdatesTheChat() {
    Harness harness;
    QCoreApplication::processEvents();
    QObject* toggle = harness.child(QStringLiteral("backgroundCaptureToggle"));
    QVERIFY(toggle != nullptr);

    // The toggle forwards its state to the chat; the flag itself is inert (a
    // stub), but the binding must hold so the future capture logic reads it.
    QVERIFY(QMetaObject::invokeMethod(toggle, "toggle"));

    QCOMPARE(harness.chats().at(0)->isBackgroundCaptureEnabled(), true);
    QCOMPARE(toggle->property("checked").toBool(), true);
}

void TestQmlChat::test_providerSelectionRebuildsTheChat() {
    Harness harness;
    QCoreApplication::processEvents();
    QVERIFY(openSettingsDialog(harness) != nullptr);
    QObject* picker = harness.child(QStringLiteral("providerPicker"));
    QVERIFY(picker != nullptr);

    const QPointer<Chat> kBefore(harness.chats().at(0));
    const std::size_t kMintedBefore = harness.minted().size();

    // Selecting a provider applies it: the single chat is rebuilt from a freshly
    // minted provider and the previous chat is dropped.
    QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, 0)));
    QCoreApplication::processEvents();

    // The previous chat is destroyed and a new one (with a newly minted provider)
    // takes its place. QPointer survives allocator address reuse, unlike a raw
    // pointer comparison.
    QVERIFY(kBefore.isNull());
    QCOMPARE(harness.chats().count(), 1);
    QVERIFY(harness.chats().at(0) != nullptr);
    QCOMPARE(harness.minted().size(), kMintedBefore + 1);
}

void TestQmlChat::test_providerSettingsFieldsAreBound() {
    Harness harness;
    QVERIFY(harness.settings().save(QStringLiteral("https://bound.test/v1"), QStringLiteral("bound-model"), QString()));
    QCoreApplication::processEvents();
    QVERIFY(openSettingsDialog(harness) != nullptr);

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
    QVERIFY(openSettingsDialog(harness) != nullptr);

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
    QVERIFY(openSettingsDialog(harness) != nullptr);

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

void TestQmlChat::test_saveButtonRebuildsTheChat() {
    Harness harness;
    QCoreApplication::processEvents();
    QVERIFY(openSettingsDialog(harness) != nullptr);

    QObject* baseUrlField = harness.child(QStringLiteral("providerBaseUrlField"));
    QObject* modelField = harness.child(QStringLiteral("providerModelField"));
    QObject* saveButton = harness.child(QStringLiteral("saveProviderSettingsButton"));
    QVERIFY(baseUrlField != nullptr);
    QVERIFY(modelField != nullptr);
    QVERIFY(saveButton != nullptr);

    const QPointer<Chat> kBefore(harness.chats().at(0));
    QVERIFY(baseUrlField->setProperty("text", QStringLiteral("https://rebuild.test/v1")));
    QVERIFY(modelField->setProperty("text", QStringLiteral("rebuild-model")));
    QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
    QCoreApplication::processEvents();

    // The saved endpoint/model reaches the open chat at once: it is rebuilt with a
    // freshly minted provider (the previous chat is dropped).
    QCOMPARE(harness.settings().baseUrl(), QStringLiteral("https://rebuild.test/v1"));
    QVERIFY(kBefore.isNull());
    QCOMPARE(harness.chats().count(), 1);
    QVERIFY(harness.chats().at(0) != nullptr);
}

QTEST_MAIN(TestQmlChat)

#include "tst_main.moc"
