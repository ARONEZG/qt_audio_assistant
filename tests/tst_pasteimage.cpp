#include <QByteArray>
#include <QImage>
#include <QSemaphore>
#include <QSignalSpy>
#include <QString>
#include <QThread>
#include <QtTest>
#include <memory>
#include <optional>
#include <utility>

#include "FakeClipboardImageSource.h"
#include "FakeImageEncoder.h"
#include "FakeProvider.h"
#include "domain/Chat.h"
#include "domain/ImageAttachment.h"
#include "domain/Message.h"
#include "domain/PasteImage.h"

namespace {

[[nodiscard]] QImage sampleImage() {
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    return image;
}

[[nodiscard]] ImageAttachment sampleAttachment() {
    return {QByteArray("png-bytes"), QStringLiteral("image/png")};
}

// Wires the two test doubles and the command like the composition root does.
struct PasteFixture {
    FakeClipboardImageSource clipboard;
    FakeImageEncoder encoder;
    PasteImage command{clipboard, encoder};
};

}  // namespace

// Covers the seam between the clipboard and the conversation: an image paste is
// encoded off the GUI thread, posted with its optional caption, and confirmed
// only when the chat actually accepted it. No platform clipboard, no real
// encoding and no fixed sleeps are involved.
class TestPasteImage : public QObject {
    Q_OBJECT

public:
    explicit TestPasteImage(QObject* parent = nullptr);

private slots:
    void test_noClipboardImageIsNotAConsumedPaste();
    void test_pastePostsImageWithCaption();
    void test_pastePostsImageWithoutCaption();
    void test_pasteWithoutAnOpenChatIsRejected();
    void test_pasteWhileStreamingIsRejected();
    void test_pasteWithEmptyPayloadFails();
    void test_pasteReadsTheClipboardEachTime();
    void test_encodingRunsOffTheCallingThread();
    void test_repeatedPasteWhileEncodingIsRejected();
    void test_chatDestroyedWhileEncodingFailsCleanly();
};

TestPasteImage::TestPasteImage(QObject* parent) : QObject(parent) {
}

void TestPasteImage::test_noClipboardImageIsNotAConsumedPaste() {
    PasteFixture fixture;
    const Chat kChat(std::make_unique<FakeProvider>());

    // No image means the composer must keep the gesture and paste text instead.
    QCOMPARE(fixture.command.hasImage(), false);
    QCOMPARE(fixture.clipboard.readCount(), 1);
    QCOMPARE(kChat.messages().size(), std::size_t{0});
    QCOMPARE(fixture.encoder.encodeCount(), 0);
}

void TestPasteImage::test_pastePostsImageWithCaption() {
    PasteFixture fixture;
    fixture.clipboard.setImage(sampleImage());
    fixture.encoder.setResult(sampleAttachment());
    auto provider = std::make_unique<FakeProvider>();
    FakeProvider* fake = provider.get();
    Chat chat(std::move(provider));
    QSignalSpy kPastedSpy(&fixture.command, &PasteImage::imagePasted);

    QCOMPARE(fixture.command.hasImage(), true);
    QCOMPARE(fixture.command.paste(&chat, QStringLiteral("look at this")), true);
    QVERIFY(kPastedSpy.wait());

    QCOMPARE(chat.messages().size(), std::size_t{1});
    const Message& kMessage = chat.messages().front();
    QCOMPARE(kMessage.role(), MessageRole::kUser);
    QCOMPARE(kMessage.text(), QStringLiteral("look at this"));
    QVERIFY(kMessage.hasImage());
    const std::optional<ImageAttachment>& kImage = kMessage.image();
    if (!kImage.has_value()) {
        QFAIL("the pasted message must carry the attachment");
    }
    QCOMPARE(kImage->data(), QByteArray("png-bytes"));
    QCOMPARE(kImage->mimeType(), QStringLiteral("image/png"));
    QCOMPARE(chat.isStreaming(), true);
    QCOMPARE(fake->respondCount(), 1);
    QVERIFY(fake->lastHistory().front().hasImage());
}

void TestPasteImage::test_pastePostsImageWithoutCaption() {
    PasteFixture fixture;
    fixture.clipboard.setImage(sampleImage());
    fixture.encoder.setResult(sampleAttachment());
    Chat chat(std::make_unique<FakeProvider>());
    QSignalSpy kPastedSpy(&fixture.command, &PasteImage::imagePasted);

    QCOMPARE(fixture.command.hasImage(), true);
    QCOMPARE(fixture.command.paste(&chat), true);
    QVERIFY(kPastedSpy.wait());

    QCOMPARE(chat.messages().size(), std::size_t{1});
    QCOMPARE(chat.messages().front().text(), QString());
    QVERIFY(chat.messages().front().hasImage());
}

void TestPasteImage::test_pasteWithoutAnOpenChatIsRejected() {
    PasteFixture fixture;
    fixture.clipboard.setImage(sampleImage());
    fixture.encoder.setResult(sampleAttachment());

    QCOMPARE(fixture.command.hasImage(), true);
    // There is nowhere to post the image: the caption must be kept and no
    // encoding should start.
    QCOMPARE(fixture.command.paste(nullptr, QStringLiteral("caption")), false);
    QCOMPARE(fixture.encoder.encodeCount(), 0);
}

void TestPasteImage::test_pasteWhileStreamingIsRejected() {
    PasteFixture fixture;
    fixture.clipboard.setImage(sampleImage());
    fixture.encoder.setResult(sampleAttachment());
    auto provider = std::make_unique<FakeProvider>();
    Chat chat(std::move(provider));
    chat.send(QStringLiteral("first"));

    QCOMPARE(fixture.command.hasImage(), true);
    QCOMPARE(fixture.command.paste(&chat, QStringLiteral("second")), false);

    QCOMPARE(fixture.encoder.encodeCount(), 0);
    QCOMPARE(chat.messages().size(), std::size_t{1});
    QCOMPARE(chat.messages().front().text(), QStringLiteral("first"));
}

void TestPasteImage::test_pasteWithEmptyPayloadFails() {
    PasteFixture fixture;
    fixture.clipboard.setImage(sampleImage());
    fixture.encoder.setResult(ImageAttachment(QByteArray(), QStringLiteral("image/png")));
    Chat chat(std::make_unique<FakeProvider>());
    QSignalSpy kFailedSpy(&fixture.command, &PasteImage::pasteFailed);

    QCOMPARE(fixture.command.hasImage(), true);
    QCOMPARE(fixture.command.paste(&chat), true);
    QVERIFY(kFailedSpy.wait());

    // An attachment with no bytes never reaches the conversation, and the
    // failure is observable so the view keeps the caption.
    QCOMPARE(chat.messages().size(), std::size_t{0});
}

void TestPasteImage::test_pasteReadsTheClipboardEachTime() {
    PasteFixture fixture;
    fixture.encoder.setResult(sampleAttachment());
    Chat chat(std::make_unique<FakeProvider>());
    QSignalSpy kPastedSpy(&fixture.command, &PasteImage::imagePasted);

    QCOMPARE(fixture.command.hasImage(), false);
    fixture.clipboard.setImage(sampleImage());
    QCOMPARE(fixture.command.hasImage(), true);
    QCOMPARE(fixture.command.paste(&chat), true);
    QVERIFY(kPastedSpy.wait());

    // hasImage() reads once; the following paste() reuses the remembered image
    // instead of reading the clipboard a second time.
    QCOMPARE(fixture.clipboard.readCount(), 2);
    QCOMPARE(chat.messages().size(), std::size_t{1});
    QVERIFY(chat.messages().front().hasImage());
}

void TestPasteImage::test_encodingRunsOffTheCallingThread() {
    PasteFixture fixture;
    fixture.clipboard.setImage(sampleImage());
    fixture.encoder.setResult(sampleAttachment());
    Chat chat(std::make_unique<FakeProvider>());
    QSignalSpy kPastedSpy(&fixture.command, &PasteImage::imagePasted);

    QCOMPARE(fixture.command.hasImage(), true);
    QCOMPARE(fixture.command.paste(&chat), true);
    QVERIFY(kPastedSpy.wait());

    // The CPU-bound work really runs on a worker thread, not the GUI thread that
    // called paste().
    QVERIFY(fixture.encoder.encodeThread() != QThread::currentThread());
    QCOMPARE(fixture.encoder.encodeCount(), 1);
}

void TestPasteImage::test_repeatedPasteWhileEncodingIsRejected() {
    PasteFixture fixture;
    fixture.clipboard.setImage(sampleImage());
    fixture.encoder.setResult(sampleAttachment());
    QSemaphore gate;
    fixture.encoder.setOnEncode([&gate] { gate.acquire(); });
    Chat chat(std::make_unique<FakeProvider>());
    QSignalSpy kPastedSpy(&fixture.command, &PasteImage::imagePasted);

    QCOMPARE(fixture.command.hasImage(), true);
    QCOMPARE(fixture.command.paste(&chat, QStringLiteral("first")), true);
    QCOMPARE(fixture.command.isBusy(), true);
    QTRY_COMPARE(fixture.encoder.encodeCount(), 1);

    // A second gesture while the first is still encoding is rejected and must
    // not start another job.
    QCOMPARE(fixture.command.hasImage(), true);
    QCOMPARE(fixture.command.paste(&chat, QStringLiteral("second")), false);
    QCOMPARE(fixture.encoder.encodeCount(), 1);

    gate.release();
    QVERIFY(kPastedSpy.wait());
    QCOMPARE(fixture.command.isBusy(), false);
    QCOMPARE(chat.messages().size(), std::size_t{1});
    QCOMPARE(chat.messages().front().text(), QStringLiteral("first"));
}

void TestPasteImage::test_chatDestroyedWhileEncodingFailsCleanly() {
    PasteFixture fixture;
    fixture.clipboard.setImage(sampleImage());
    fixture.encoder.setResult(sampleAttachment());
    QSemaphore gate;
    fixture.encoder.setOnEncode([&gate] { gate.acquire(); });
    auto chat = std::make_unique<Chat>(std::make_unique<FakeProvider>());
    QSignalSpy kFailedSpy(&fixture.command, &PasteImage::pasteFailed);

    QCOMPARE(fixture.command.hasImage(), true);
    QCOMPARE(fixture.command.paste(chat.get()), true);
    // The chat is deleted while the encode is still running; the command must
    // not dereference the stale pointer when the result arrives.
    chat.reset();
    gate.release();

    QVERIFY(kFailedSpy.wait());
    QCOMPARE(fixture.command.isBusy(), false);
}

QTEST_GUILESS_MAIN(TestPasteImage)

#include "tst_pasteimage.moc"
