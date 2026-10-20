#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMetaObject>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariant>
#include <QtTest>
#include <chrono>
#include <memory>
#include <utility>
#include <vector>

#include "FakeAudioSource.h"
#include "FakeProvider.h"
#include "FakeTranscriber.h"
#include "audio/AudioBuffer.h"
#include "domain/AudioCapture.h"
#include "domain/CaptureConfigFile.h"
#include "domain/CapturePresets.h"
#include "domain/CaptureSettings.h"
#include "domain/Chat.h"
#include "domain/Chats.h"
#include "domain/Message.h"
#include "domain/NewChat.h"
#include "domain/ProviderConfigFile.h"
#include "domain/ProviderError.h"
#include "domain/ProviderOffer.h"
#include "domain/ProviderSettings.h"
#include "domain/Providers.h"
#include "domain/VoiceInput.h"

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

// The delegate whose `isUser` matches, or nullptr while no such delegate has
// been materialised yet.
[[nodiscard]] QObject* messageDelegate(QQuickWindow* window, bool isUser) {
    for (QQuickItem* delegate : collectItems(window->contentItem(), QStringLiteral("messageDelegate"))) {
        if (delegate->property("isUser").toBool() == isUser) {
            return delegate;
        }
    }
    return nullptr;
}

// The label of the one delegate whose `isUser` matches, or nullptr while no such
// delegate has been materialised yet.
[[nodiscard]] QObject* messageLabel(QQuickWindow* window, bool isUser) {
    QObject* delegate = messageDelegate(window, isUser);
    return delegate == nullptr ? nullptr
                               : findInItems(qobject_cast<QQuickItem*>(delegate), QStringLiteral("messageText"));
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

// The plain text of a rendered TextEdit document, unlike its `text` property:
// with Markdown the property normalises back to Markdown source, while getText
// returns what the reader actually sees.
[[nodiscard]] QString renderedText(QObject* textEdit) {
    QString rendered;
    const int kLength = textEdit->property("length").toInt();
    if (!QMetaObject::invokeMethod(
                textEdit, "getText", Q_RETURN_ARG(QString, rendered), Q_ARG(int, 0), Q_ARG(int, kLength))) {
        return {};
    }
    return rendered;
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
// list, the chats and the thin invokable that joins them, plus the capture
// pipeline behind an AudioCapture facade (with a fake source and transcriber, so
// no device or network is touched). The provider is a fake too, so the test acts
// as the network and no real request is ever made.
class Harness {
public:
    Harness()
        : configFile_(temporaryDirectory_.filePath(QStringLiteral("provider.env"))),
          settings_({}, {}, configFile_),
          captureConfigFile_(temporaryDirectory_.filePath(QStringLiteral("capture.env"))),
          captureSettings_({}, captureConfigFile_, &capturePresets_),
          providers_({ProviderOffer(QStringLiteral("fake"),
                                    QStringLiteral("Fake Provider"),
                                    [this] {
                                        auto provider = std::make_unique<FakeProvider>(QStringLiteral("fake"),
                                                                                       QStringLiteral("Fake Provider"));
                                        minted_.push_back(provider.get());
                                        return provider;
                                    })}),
          newChat_(chats_, providers_),
          captureBuffer_(std::chrono::seconds(30), canonicalFormat()),
          audioCapture_(captureBuffer_, captureSource_, transcriber_) {
        // Mirror main(): the application opens a single chat for the first
        // offered provider before the view is loaded.
        if (Chat* chat = newChat_.open(QStringLiteral("fake")); chat != nullptr) {
            // Mirror main(): the chat's capture switch drives the pipeline. The
            // fake source never fails, so main()'s revert-on-failure branch has no
            // double here; the switch -> start/stop mapping is what is covered.
            QObject::connect(chat, &Chat::backgroundCaptureChanged, &audioCapture_, [chat, this] {
                if (chat->isBackgroundCaptureEnabled()) {
                    audioCapture_.start();
                } else {
                    audioCapture_.stop();
                }
            });
        }
        // Mirror main(): the voice command sits between the capture facade and
        // the open chat and backs both Ctrl+R and the Transcribe button.
        voiceInput_ = std::make_unique<VoiceInput>(audioCapture_, chats_.at(0));
        engine_.setInitialProperties({
                {QStringLiteral("chats"), QVariant::fromValue(&chats_)},
                {QStringLiteral("providers"), QVariant::fromValue(&providers_)},
                {QStringLiteral("newChat"), QVariant::fromValue(&newChat_)},
                {QStringLiteral("providerSettings"), QVariant::fromValue(&settings_)},
                {QStringLiteral("audioCapture"), QVariant::fromValue(&audioCapture_)},
                {QStringLiteral("captureSettings"), QVariant::fromValue(&captureSettings_)},
                {QStringLiteral("voiceInput"), QVariant::fromValue(voiceInput_.get())},
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
    [[nodiscard]] CaptureSettings& captureSettings() noexcept {
        return captureSettings_;
    }
    [[nodiscard]] const CaptureConfigFile& captureConfigFile() const noexcept {
        return captureConfigFile_;
    }
    [[nodiscard]] const std::vector<FakeProvider*>& minted() const noexcept {
        return minted_;
    }
    [[nodiscard]] FakeAudioSource& captureSource() noexcept {
        return captureSource_;
    }
    [[nodiscard]] FakeTranscriber& transcriber() noexcept {
        return transcriber_;
    }
    [[nodiscard]] AudioCapture& capture() noexcept {
        return audioCapture_;
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
    CaptureConfigFile captureConfigFile_;
    CapturePresets capturePresets_;
    CaptureSettings captureSettings_;
    std::vector<FakeProvider*> minted_;
    Providers providers_;
    Chats chats_;
    NewChat newChat_;
    // Declaration order is a lifetime contract (as in main()): the buffer before
    // the source that writes into it, and both before the facade that references
    // them. They are declared before engine_, so the engine (and the QML objects
    // that reference the facade) is destroyed first.
    audio::AudioBuffer captureBuffer_;
    FakeAudioSource captureSource_;
    FakeTranscriber transcriber_;
    AudioCapture audioCapture_;
    // Declared after the facade and the chats: destroyed before both, so the
    // command never outlives its collaborators.
    std::unique_ptr<VoiceInput> voiceInput_;
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

// Sends the window-level Ctrl+R shortcut the way the platform would. A raw key
// event is used instead of QTest::keyClick: the latter's QPointer temporary
// trips clang-analyzer's NewDelete check inside the Qt header, which cannot be
// suppressed from project code.
void pressVoiceInputShortcut(QQuickWindow& window) {
    QKeyEvent press(QEvent::KeyPress, Qt::Key_R, Qt::ControlModifier, QStringLiteral("r"));
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_R, Qt::ControlModifier, QStringLiteral("r"));
    QCoreApplication::sendEvent(&window, &press);
    QCoreApplication::sendEvent(&window, &release);
}

// Sends a click through Qt Test's QPA path. qt_handleMouseEvent is called
// directly because QTest::mouseClick's internal QPointer temporary trips
// clang-analyzer's NewDelete check inside the Qt header.
void clickWindow(QQuickWindow& window,
                 QPoint position,
                 Qt::MouseButton button,
                 Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    const QPointF kGlobal = window.mapToGlobal(position);
    QTest::lastMouseTimestamp += 10;
    qt_handleMouseEvent(
            &window, position, kGlobal, button, button, QEvent::MouseButtonPress, modifiers, QTest::lastMouseTimestamp);
    qt_handleMouseEvent(&window,
                        position,
                        kGlobal,
                        Qt::NoButton,
                        button,
                        QEvent::MouseButtonRelease,
                        modifiers,
                        QTest::lastMouseTimestamp);
    QCoreApplication::processEvents();
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
    void test_messageTextIsSelectableAndCopyable();
    void test_messageContextMenuCopiesTheSelection();
    void test_contextMenuOpensOnRightAndControlClick();
    void test_messageLayoutKeepsLongTextInsideTheBubble();
    void test_streamingTailRendersSanitisedMarkdown();
    void test_streamingTailIsSelectableAndCopyable();
    void test_streamingTailFollowsEachAnswer();
    void test_streamingTailStaysInsideTheChatPanel();
    void test_chatFailureShowsTheError();
    void test_backgroundCaptureToggleStartsOff();
    void test_backgroundCaptureToggleStartsAndStopsCapture();
    void test_transcribeButtonIsDisabledWhileCaptureIsOff();
    void test_transcribeButtonSendsTheBufferAndPostsTheText();
    void test_ctrlRTranscribesAndPostsTheText();
    void test_voiceShortcutIsDisabledWhileChatIsStreaming();
    void test_failedTranscriptionShowsTheCaptureError();
    void test_backgroundCaptureToggleStaysInSyncAfterUserToggle();
    void test_providerSelectionSwitchesTheOpenChat();
    void test_providerSettingsFieldsAreBound();
    void test_apiKeyFieldIsMasked();
    void test_saveButtonPersistsProviderSettings();
    void test_saveButtonAppliesWithoutLosingHistory();
    void test_captureSettingsPanelSavesTheDuration();
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
    // A Markdown TextEdit re-serialises its `text` property, so the sanitisation
    // invariant is checked on the delegate's markup - the exact value bound to
    // the renderer - not on the normalised property.
    QObject* assistantDelegate = messageDelegate(window, false);
    QVERIFY(assistantDelegate != nullptr);
    const QString kAssistantMarkup = assistantDelegate->property("markup").toString();
    QVERIFY(kAssistantMarkup.contains(QStringLiteral("**bold**")));
    QVERIFY(!hasUnescapedAngleBracket(kAssistantMarkup));
    // The escaped tag reaches the reader as literal text rather than being
    // interpreted as HTML (which would drop or execute it).
    const QString kRendered = renderedText(assistantLabel);
    QVERIFY(kRendered.contains(QStringLiteral("bold")));
    QVERIFY(kRendered.contains(QStringLiteral("<script>x</script>")));
}

// A message used to be a static Text label: nothing could be selected or copied.
// It is now a read-only text editor, so a reply can be selected and copied out of
// the chat while the bubble keeps rendering the same content.
void TestQmlChat::test_messageTextIsSelectableAndCopyable() {
    Harness harness;
    QCoreApplication::processEvents();
    Chat* chat = harness.chats().at(0);
    chat->send(QStringLiteral("selectable question"));
    harness.minted().at(0)->lastAnswer()->complete(
            Message(MessageRole::kAssistant, QStringLiteral("selectable answer")));
    QCoreApplication::processEvents();

    auto* window = qobject_cast<QQuickWindow*>(harness.root());
    QVERIFY(window != nullptr);
    QVERIFY(QTest::qWaitFor(
            [window] { return collectItems(window->contentItem(), QStringLiteral("messageDelegate")).size() == 2; }));

    QObject* assistantLabel = messageLabel(window, false);
    QVERIFY(assistantLabel != nullptr);
    // Read-only, yet selectable by mouse and keyboard, and the selection survives
    // focus moving to the context menu.
    QCOMPARE(assistantLabel->property("readOnly").toBool(), true);
    QCOMPARE(assistantLabel->property("selectByMouse").toBool(), true);
    QCOMPARE(assistantLabel->property("selectByKeyboard").toBool(), true);
    QCOMPARE(assistantLabel->property("persistentSelection").toBool(), true);
    // Messages are not Tab stops: Tab must keep moving between controls, not
    // through the whole transcript. A click still focuses the passage.
    QCOMPARE(assistantLabel->property("activeFocusOnTab").toBool(), false);

    QVERIFY(QMetaObject::invokeMethod(assistantLabel, "selectAll"));
    // The selection is the rendered text, not the raw Markdown source.
    QCOMPARE(assistantLabel->property("selectedText").toString(), QStringLiteral("selectable answer"));

    // A selected passage is copied with the standard keyboard shortcut, as in
    // any other text control.
    auto* assistantItem = qobject_cast<QQuickItem*>(assistantLabel);
    QVERIFY(assistantItem != nullptr);
    assistantItem->forceActiveFocus();
    QCoreApplication::processEvents();
    QVERIFY(assistantItem->hasActiveFocus());

    QGuiApplication::clipboard()->clear();
    QKeyEvent press(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier, QStringLiteral("c"));
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_C, Qt::ControlModifier, QStringLiteral("c"));
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("selectable answer"));
}

// The right-click menu is the discoverable form of the copy action: Copy follows
// whether anything is selected, and Select All makes a whole message copyable.
void TestQmlChat::test_messageContextMenuCopiesTheSelection() {
    Harness harness;
    QCoreApplication::processEvents();
    Chat* chat = harness.chats().at(0);
    chat->send(QStringLiteral("copy this"));
    harness.minted().at(0)->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("done")));
    QCoreApplication::processEvents();

    auto* window = qobject_cast<QQuickWindow*>(harness.root());
    QVERIFY(window != nullptr);
    QVERIFY(QTest::qWaitFor(
            [window] { return collectItems(window->contentItem(), QStringLiteral("messageDelegate")).size() == 2; }));

    QObject* userLabel = messageLabel(window, true);
    QVERIFY(userLabel != nullptr);
    auto* menu = userLabel->findChild<QObject*>(QStringLiteral("textContextMenu"));
    QVERIFY(menu != nullptr);
    auto* copyItem = menu->findChild<QObject*>(QStringLiteral("copyMenuItem"));
    auto* selectAllItem = menu->findChild<QObject*>(QStringLiteral("selectAllMenuItem"));
    QVERIFY(copyItem != nullptr);
    QVERIFY(selectAllItem != nullptr);

    // Copying is an action on a selection, so it starts disabled and becomes
    // available once the message is selected.
    QCOMPARE(copyItem->property("enabled").toBool(), false);
    QVERIFY(QMetaObject::invokeMethod(selectAllItem, "triggered"));
    QCoreApplication::processEvents();
    QCOMPARE(userLabel->property("selectedText").toString(), QStringLiteral("copy this"));
    QCOMPARE(copyItem->property("enabled").toBool(), true);

    QGuiApplication::clipboard()->clear();
    QVERIFY(QMetaObject::invokeMethod(copyItem, "triggered"));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("copy this"));
}

// The context menu must be reachable by the platform's own gesture: right click
// everywhere, plus Control+click for a one-button mouse on macOS. The menu opens
// on press or release according to the platform hint.
void TestQmlChat::test_contextMenuOpensOnRightAndControlClick() {
    Harness harness;
    QCoreApplication::processEvents();
    Chat* chat = harness.chats().at(0);
    chat->send(QStringLiteral("open the menu"));
    harness.minted().at(0)->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("ok")));
    QCoreApplication::processEvents();

    auto* window = qobject_cast<QQuickWindow*>(harness.root());
    QVERIFY(window != nullptr);
    QVERIFY(QTest::qWaitFor(
            [window] { return collectItems(window->contentItem(), QStringLiteral("messageDelegate")).size() == 2; }));

    QObject* label = messageLabel(window, true);
    auto* labelItem = qobject_cast<QQuickItem*>(label);
    QVERIFY(labelItem != nullptr);
    auto* menu = label->findChild<QObject*>(QStringLiteral("textContextMenu"));
    QVERIFY(menu != nullptr);
    QCOMPARE(menu->property("visible").toBool(), false);

    const QPoint kCenter = labelItem->mapToScene(QPointF(labelItem->width() / 2, labelItem->height() / 2)).toPoint();

    clickWindow(*window, kCenter, Qt::RightButton);
    QCOMPARE(menu->property("visible").toBool(), true);

    QVERIFY(QMetaObject::invokeMethod(menu, "close"));
    QCoreApplication::processEvents();
    QCOMPARE(menu->property("visible").toBool(), false);

    clickWindow(*window, kCenter, Qt::LeftButton, Qt::ControlModifier);
    QCOMPARE(menu->property("visible").toBool(), true);
}

// Regression for the Text -> TextEdit switch: the bubble width comes from the
// label's implicit width and the height from its wrapped layout. A long reply
// (including one unbreakable token) must stay inside the 80% bubble limit and
// grow vertically instead of being clipped or overflowing the row.
void TestQmlChat::test_messageLayoutKeepsLongTextInsideTheBubble() {
    Harness harness;
    QCoreApplication::processEvents();
    Chat* chat = harness.chats().at(0);
    chat->send(QStringLiteral("short"));
    const QString kLongReply = QStringLiteral("A long reply that has to wrap: ") + QString(400, QLatin1Char('x')) +
                               QStringLiteral(" and then some trailing words.");
    harness.minted().at(0)->lastAnswer()->complete(Message(MessageRole::kAssistant, kLongReply));
    QCoreApplication::processEvents();

    auto* window = qobject_cast<QQuickWindow*>(harness.root());
    QVERIFY(window != nullptr);
    QVERIFY(QTest::qWaitFor(
            [window] { return collectItems(window->contentItem(), QStringLiteral("messageDelegate")).size() == 2; }));

    QObject* delegate = messageDelegate(window, false);
    auto* delegateItem = qobject_cast<QQuickItem*>(delegate);
    auto* shortItem = qobject_cast<QQuickItem*>(messageLabel(window, true));
    auto* longItem = qobject_cast<QQuickItem*>(messageLabel(window, false));
    QVERIFY(delegateItem != nullptr);
    QVERIFY(shortItem != nullptr);
    QVERIFY(longItem != nullptr);

    QVERIFY(longItem->width() > 0);
    QVERIFY(longItem->width() <= delegateItem->width() * 0.8 + 1.0);
    // Wrapped over several lines, the row grows to contain the text.
    QVERIFY(longItem->height() > shortItem->height());
    QVERIFY(longItem->height() <= delegateItem->height());
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
    // The binding source stays sanitised, and the escaped tag is displayed as
    // literal text rather than parsed as HTML.
    QVERIFY(!hasUnescapedAngleBracket(chat->streamingMarkup()));
    const QString kRendered = renderedText(tail);
    QVERIFY(kRendered.contains(QStringLiteral("live")));
    QVERIFY(kRendered.contains(QStringLiteral("<script>bad</script>")));
}

// The live answer is a SelectableText too, so it can be copied while it is the
// only place the text exists. (Incoming chunks replace the text and therefore
// start a new selection; copy works on the text as currently shown.)
void TestQmlChat::test_streamingTailIsSelectableAndCopyable() {
    Harness harness;
    Chat* chat = harness.chats().at(0);
    chat->send(QStringLiteral("hi"));
    harness.minted().at(0)->lastAnswer()->appendChunk(QStringLiteral("live answer"));
    QCoreApplication::processEvents();

    QObject* tail = harness.child(QStringLiteral("streamingTail"));
    QVERIFY(tail != nullptr);
    QCOMPARE(tail->property("visible").toBool(), true);
    QVERIFY(QTest::qWaitFor(
            [tail] { return tail->property("text").toString().contains(QStringLiteral("live answer")); }));

    QVERIFY(QMetaObject::invokeMethod(tail, "selectAll"));
    QCOMPARE(tail->property("selectedText").toString(), QStringLiteral("live answer"));

    QGuiApplication::clipboard()->clear();
    QVERIFY(QMetaObject::invokeMethod(tail, "copy"));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("live answer"));
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

    // The switch lives inside the chat and mirrors the chat's flag; it starts off,
    // so capture was never started at load.
    QCOMPARE(toggle->property("checked").toBool(), false);
    QCOMPARE(harness.chats().at(0)->isBackgroundCaptureEnabled(), false);
    QCOMPARE(harness.captureSource().startCount(), 0);
}

void TestQmlChat::test_backgroundCaptureToggleStartsAndStopsCapture() {
    Harness harness;
    QCoreApplication::processEvents();
    QObject* toggle = harness.child(QStringLiteral("backgroundCaptureToggle"));
    QVERIFY(toggle != nullptr);

    // Switching capture on forwards the flag to the chat and starts the source;
    // switching it off stops it.
    QVERIFY(QMetaObject::invokeMethod(toggle, "toggle"));
    QCOMPARE(harness.chats().at(0)->isBackgroundCaptureEnabled(), true);
    QCOMPARE(toggle->property("checked").toBool(), true);
    QCOMPARE(harness.captureSource().startCount(), 1);
    QCOMPARE(harness.captureSource().stopCount(), 0);

    QVERIFY(QMetaObject::invokeMethod(toggle, "toggle"));
    QCOMPARE(harness.chats().at(0)->isBackgroundCaptureEnabled(), false);
    QCOMPARE(harness.captureSource().startCount(), 1);
    QCOMPARE(harness.captureSource().stopCount(), 1);
}

void TestQmlChat::test_transcribeButtonIsDisabledWhileCaptureIsOff() {
    Harness harness;
    QCoreApplication::processEvents();
    QObject* button = harness.child(QStringLiteral("transcribeButton"));
    QVERIFY(button != nullptr);

    // Transcribing the buffer is only meaningful while capture is on, for the
    // button and its keyboard form alike.
    QCOMPARE(button->property("enabled").toBool(), false);
    auto* window = qobject_cast<QQuickWindow*>(harness.root());
    QVERIFY(window != nullptr);
    pressVoiceInputShortcut(*window);
    QCoreApplication::processEvents();
    QCOMPARE(harness.transcriber().callCount(), 0);
    harness.chats().at(0)->setBackgroundCaptureEnabled(true);
    QCoreApplication::processEvents();
    QCOMPARE(button->property("enabled").toBool(), true);
}

void TestQmlChat::test_transcribeButtonSendsTheBufferAndPostsTheText() {
    Harness harness;
    QCoreApplication::processEvents();
    harness.chats().at(0)->setBackgroundCaptureEnabled(true);
    QCoreApplication::processEvents();
    QObject* button = harness.child(QStringLiteral("transcribeButton"));
    QVERIFY(button != nullptr);

    harness.transcriber().succeedWith(QStringLiteral("hello from audio"));
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));

    // The button asks the facade to transcribe the buffer ...
    QTRY_COMPARE(harness.transcriber().callCount(), 1);
    // ... and the recognized text becomes a chat message (no draft copy).
    Chat* chat = harness.chats().at(0);
    QTRY_COMPARE(chat->messages().size(), std::size_t{1});
    QCOMPARE(chat->messages().front().isUser(), true);
    QCOMPARE(chat->messages().front().text(), QStringLiteral("hello from audio"));
    QCOMPARE(harness.minted().at(0)->respondCount(), 1);

    QObject* input = harness.child(QStringLiteral("messageInput"));
    QVERIFY(input != nullptr);
    QCOMPARE(input->property("text").toString(), QString());
}

// Ctrl+R is wired to the same command as the button: recognize the last
// utterance and post it to the open chat.
void TestQmlChat::test_ctrlRTranscribesAndPostsTheText() {
    Harness harness;
    QCoreApplication::processEvents();
    harness.chats().at(0)->setBackgroundCaptureEnabled(true);
    QCoreApplication::processEvents();
    auto* window = qobject_cast<QQuickWindow*>(harness.root());
    QVERIFY(window != nullptr);

    harness.transcriber().succeedWith(QStringLiteral("spoken"));
    pressVoiceInputShortcut(*window);

    QTRY_COMPARE(harness.transcriber().callCount(), 1);
    Chat* chat = harness.chats().at(0);
    QTRY_COMPARE(chat->messages().size(), std::size_t{1});
    QCOMPARE(chat->messages().front().text(), QStringLiteral("spoken"));
    QCOMPARE(harness.minted().at(0)->respondCount(), 1);
}

void TestQmlChat::test_voiceShortcutIsDisabledWhileChatIsStreaming() {
    Harness harness;
    QCoreApplication::processEvents();
    Chat* chat = harness.chats().at(0);
    chat->setBackgroundCaptureEnabled(true);
    QCoreApplication::processEvents();
    auto* window = qobject_cast<QQuickWindow*>(harness.root());
    QVERIFY(window != nullptr);

    chat->send(QStringLiteral("hi"));
    QCoreApplication::processEvents();
    harness.transcriber().succeedWith(QStringLiteral("spoken"));

    pressVoiceInputShortcut(*window);
    QCoreApplication::processEvents();

    // A reply is streaming, so the shortcut did not even start recognition.
    QCOMPARE(harness.transcriber().callCount(), 0);
    QCOMPARE(chat->messages().size(), std::size_t{1});
}

void TestQmlChat::test_failedTranscriptionShowsTheCaptureError() {
    Harness harness;
    QCoreApplication::processEvents();
    harness.chats().at(0)->setBackgroundCaptureEnabled(true);
    QCoreApplication::processEvents();
    QObject* errorLabel = harness.child(QStringLiteral("captureError"));
    QObject* button = harness.child(QStringLiteral("transcribeButton"));
    QVERIFY(errorLabel != nullptr);
    QVERIFY(button != nullptr);
    QCOMPARE(errorLabel->property("visible").toBool(), false);

    harness.transcriber().failWith(ProviderError::kAuthentication, QStringLiteral("invalid key"));
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));

    // The facade exposes the already-formatted reason; the view mirrors it.
    QTRY_COMPARE(errorLabel->property("visible").toBool(), true);
    QVERIFY(errorLabel->property("text").toString().contains(QStringLiteral("invalid key")));
    QCOMPARE(harness.capture().hasError(), true);

    // A new request clears it (mirrors a failed chat reply).
    harness.transcriber().succeedWith(QStringLiteral("ok"));
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QTRY_COMPARE(errorLabel->property("visible").toBool(), false);
}

void TestQmlChat::test_backgroundCaptureToggleStaysInSyncAfterUserToggle() {
    Harness harness;
    QCoreApplication::processEvents();
    QObject* toggle = harness.child(QStringLiteral("backgroundCaptureToggle"));
    QVERIFY(toggle != nullptr);

    // The user switches it on ...
    QVERIFY(QMetaObject::invokeMethod(toggle, "toggle"));
    QCOMPARE(harness.chats().at(0)->isBackgroundCaptureEnabled(), true);

    // ... and a later change from C++ still reaches the control: the two-way
    // Binding survives the control's own write (an inline `checked:` binding
    // would have been destroyed by it).
    harness.chats().at(0)->setBackgroundCaptureEnabled(false);
    QCoreApplication::processEvents();
    QCOMPARE(toggle->property("checked").toBool(), false);
}

void TestQmlChat::test_providerSelectionSwitchesTheOpenChat() {
    Harness harness;
    QCoreApplication::processEvents();
    QVERIFY(openSettingsDialog(harness) != nullptr);
    QObject* picker = harness.child(QStringLiteral("providerPicker"));
    QVERIFY(picker != nullptr);

    Chat* chat = harness.chats().at(0);
    chat->send(QStringLiteral("hi"));
    harness.minted().at(0)->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("hello")));
    const std::size_t kMintedBefore = harness.minted().size();

    // Selecting a provider applies it to the open chat: the same chat object, a
    // freshly minted provider, and the conversation kept.
    QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, 0)));
    QCoreApplication::processEvents();

    QCOMPARE(harness.chats().count(), 1);
    QCOMPARE(harness.chats().at(0), chat);
    QCOMPARE(harness.minted().size(), kMintedBefore + 1);
    QCOMPARE(chat->messages().size(), std::size_t{2});
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
    // The key field is cleared after a successful save: the view never renders
    // the stored secret back.
    QCOMPARE(apiKeyField->property("text").toString(), QString());

    const QString kText = readTextFile(harness.configFile().path());
    QVERIFY(kText.contains(QStringLiteral("POLZA_BASE_URL=https://panel.test/v1")));
    QVERIFY(kText.contains(QStringLiteral("POLZA_MODEL=panel-model")));
    QVERIFY(kText.contains(QStringLiteral("POLZA_API_KEY=panel-secret")));
}

void TestQmlChat::test_saveButtonAppliesWithoutLosingHistory() {
    Harness harness;
    QCoreApplication::processEvents();
    QVERIFY(openSettingsDialog(harness) != nullptr);

    QObject* baseUrlField = harness.child(QStringLiteral("providerBaseUrlField"));
    QObject* modelField = harness.child(QStringLiteral("providerModelField"));
    QObject* saveButton = harness.child(QStringLiteral("saveProviderSettingsButton"));
    QVERIFY(baseUrlField != nullptr);
    QVERIFY(modelField != nullptr);
    QVERIFY(saveButton != nullptr);

    Chat* chat = harness.chats().at(0);
    chat->send(QStringLiteral("hi"));
    harness.minted().at(0)->lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("hello")));
    const std::size_t kMintedBefore = harness.minted().size();

    QVERIFY(baseUrlField->setProperty("text", QStringLiteral("https://apply.test/v1")));
    QVERIFY(modelField->setProperty("text", QStringLiteral("apply-model")));
    QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
    QCoreApplication::processEvents();

    // The saved endpoint/model is applied to the open chat at once (a fresh
    // provider is minted) without losing the conversation.
    QCOMPARE(harness.settings().baseUrl(), QStringLiteral("https://apply.test/v1"));
    QCOMPARE(harness.chats().count(), 1);
    QCOMPARE(harness.chats().at(0), chat);
    QCOMPARE(harness.minted().size(), kMintedBefore + 1);
    QCOMPARE(chat->messages().size(), std::size_t{2});
}

// Smoke test for the capture settings panel: the dialog opens, the duration is
// edited, and Save persists it through the C++ command.
void TestQmlChat::test_captureSettingsPanelSavesTheDuration() {
    Harness harness;
    QCoreApplication::processEvents();
    QVERIFY(openSettingsDialog(harness) != nullptr);

    QObject* spinBox = harness.child(QStringLiteral("captureDurationSpinBox"));
    QObject* saveButton = harness.child(QStringLiteral("saveCaptureSettingsButton"));
    QVERIFY(spinBox != nullptr);
    QVERIFY(saveButton != nullptr);
    QCOMPARE(spinBox->property("value").toInt(), 20);

    QVERIFY(spinBox->setProperty("value", 45));
    QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));

    QVERIFY(harness.captureSettings().lastError().isEmpty());
    QCOMPARE(harness.captureSettings().captureDurationSeconds(), 45);
    QVERIFY(readTextFile(harness.captureConfigFile().path()).contains(QStringLiteral("AA_CAPTURE_SECONDS=45")));
}

QTEST_MAIN(TestQmlChat)

#include "tst_main.moc"
