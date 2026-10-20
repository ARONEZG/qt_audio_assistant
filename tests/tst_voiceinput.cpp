// Tests for the voice-input command (VoiceInput).
//
// The command joins the two existing seams: it drives the AudioCapture facade
// and posts the recognized text to the open Chat. The capture pipeline is the
// real facade over a ring buffer with doubles for the source and transcriber;
// the chat runs against the FakeProvider, so the test acts as both the speech
// service and the chat backend. No device, network or sleep is involved.
#include <QSignalSpy>
#include <QString>
#include <QtTest>
#include <chrono>
#include <cstddef>
#include <memory>
#include <utility>

#include "FakeAudioSource.h"
#include "FakeProvider.h"
#include "FakeTranscriber.h"
#include "audio/AudioBuffer.h"
#include "domain/AudioCapture.h"
#include "domain/Chat.h"
#include "domain/Message.h"
#include "domain/ProviderError.h"
#include "domain/VoiceInput.h"

namespace {

constexpr auto kBufferDuration = std::chrono::seconds{1};

// The same dependency chain as main(): the chat owns its provider, the capture
// facade borrows the buffer/source/transcriber, and the voice command borrows
// the facade and the chat. Members are declared in dependency order, so
// destruction runs in the opposite one.
class Fixture {
public:
    Fixture() {
        auto provider = std::make_unique<FakeProvider>();
        provider_ = provider.get();
        chat_ = std::make_unique<Chat>(std::move(provider));
        capture_ = std::make_unique<AudioCapture>(buffer_, source_, transcriber_);
        voice_ = std::make_unique<VoiceInput>(*capture_, chat_.get());
    }

    [[nodiscard]] FakeProvider& provider() noexcept {
        return *provider_;
    }
    [[nodiscard]] Chat& chat() noexcept {
        return *chat_;
    }
    [[nodiscard]] FakeTranscriber& transcriber() noexcept {
        return transcriber_;
    }
    [[nodiscard]] AudioCapture& capture() noexcept {
        return *capture_;
    }
    [[nodiscard]] VoiceInput& voice() noexcept {
        return *voice_;
    }

private:
    audio::AudioBuffer buffer_{kBufferDuration, canonicalFormat()};
    FakeAudioSource source_;
    FakeTranscriber transcriber_;
    FakeProvider* provider_ = nullptr;
    std::unique_ptr<Chat> chat_;
    std::unique_ptr<AudioCapture> capture_;
    std::unique_ptr<VoiceInput> voice_;
};

}  // namespace

class TestVoiceInput : public QObject {
    Q_OBJECT

public:
    explicit TestVoiceInput(QObject* parent = nullptr);

private slots:
    void test_transcribeAndSendStartsTranscription();
    void test_transcribedTextIsSentToTheChat();
    void test_textIsTrimmedBeforeSending();
    void test_emptyTranscriptionIsNotSent();
    void test_failedTranscriptionIsNotSent();
    void test_secondRequestWhileBusyIsIgnored();
    void test_requestWhileChatStreamingIsIgnored();
    void test_transcriptIsSentAfterTheStreamingReply();
    void test_externalTranscriptionIsNotSent();
    void test_externalFailureIsIgnored();
    void test_nullChatDoesNotSend();
};

TestVoiceInput::TestVoiceInput(QObject* parent) : QObject(parent) {
}

void TestVoiceInput::test_transcribeAndSendStartsTranscription() {
    Fixture fixture;
    const QSignalSpy kBusySpy(&fixture.voice(), &VoiceInput::busyChanged);
    fixture.voice().transcribeAndSend();

    QCOMPARE(fixture.transcriber().callCount(), 1);
    QCOMPARE(fixture.voice().isBusy(), true);
    QCOMPARE(kBusySpy.count(), 1);

    // The terminal signal always arrives from the event loop and clears busy.
    QTRY_COMPARE(fixture.voice().isBusy(), false);
    QCOMPARE(kBusySpy.count(), 2);
}

void TestVoiceInput::test_transcribedTextIsSentToTheChat() {
    Fixture fixture;
    fixture.transcriber().succeedWith(QStringLiteral("привет мир"));

    fixture.voice().transcribeAndSend();

    QTRY_COMPARE(fixture.chat().messages().size(), std::size_t{1});
    const Message& kMessage = fixture.chat().messages().front();
    QCOMPARE(kMessage.isUser(), true);
    QCOMPARE(kMessage.text(), QStringLiteral("привет мир"));
    // The text went through the normal send command, which asks the provider
    // for a reply.
    QCOMPARE(fixture.provider().respondCount(), 1);
    QCOMPARE(fixture.chat().isStreaming(), true);
    QCOMPARE(fixture.voice().isBusy(), false);
}

void TestVoiceInput::test_textIsTrimmedBeforeSending() {
    Fixture fixture;
    fixture.transcriber().succeedWith(QStringLiteral("  hello  "));

    fixture.voice().transcribeAndSend();

    QTRY_COMPARE(fixture.chat().messages().size(), std::size_t{1});
    QCOMPARE(fixture.chat().messages().front().text(), QStringLiteral("hello"));
}

void TestVoiceInput::test_emptyTranscriptionIsNotSent() {
    Fixture fixture;
    fixture.transcriber().succeedWith(QStringLiteral("   "));

    fixture.voice().transcribeAndSend();

    QTRY_COMPARE(fixture.voice().isBusy(), false);
    QCOMPARE(fixture.chat().messages().size(), std::size_t{0});
    QCOMPARE(fixture.provider().respondCount(), 0);
}

void TestVoiceInput::test_failedTranscriptionIsNotSent() {
    Fixture fixture;
    fixture.transcriber().failWith(ProviderError::kNetwork, QStringLiteral("boom"));

    fixture.voice().transcribeAndSend();

    QTRY_COMPARE(fixture.voice().isBusy(), false);
    QCOMPARE(fixture.chat().messages().size(), std::size_t{0});
    QCOMPARE(fixture.provider().respondCount(), 0);
    // The failure itself stays on the capture facade, which the view mirrors.
    QCOMPARE(fixture.capture().hasError(), true);
}

void TestVoiceInput::test_secondRequestWhileBusyIsIgnored() {
    Fixture fixture;
    fixture.transcriber().succeedWith(QStringLiteral("first"));

    fixture.voice().transcribeAndSend();
    fixture.voice().transcribeAndSend();

    // One in-flight transcription at a time: a repeated shortcut does not start
    // a second request.
    QCOMPARE(fixture.transcriber().callCount(), 1);
    QTRY_COMPARE(fixture.chat().messages().size(), std::size_t{1});

    // After the first round finishes the command accepts a new request (the
    // reply is abandoned only so the chat is idle again).
    fixture.chat().cancel();
    fixture.transcriber().succeedWith(QStringLiteral("second"));
    fixture.voice().transcribeAndSend();
    QCOMPARE(fixture.transcriber().callCount(), 2);
    QTRY_COMPARE(fixture.chat().messages().size(), std::size_t{2});
    QCOMPARE(fixture.chat().messages().at(1).text(), QStringLiteral("second"));
}

void TestVoiceInput::test_requestWhileChatStreamingIsIgnored() {
    Fixture fixture;
    fixture.chat().send(QStringLiteral("typed"));
    QCOMPARE(fixture.provider().respondCount(), 1);

    fixture.voice().transcribeAndSend();

    // A reply is already in flight: the voice command does not even start a
    // transcription, so the recognized text cannot be silently dropped by
    // Chat::send().
    QCOMPARE(fixture.transcriber().callCount(), 0);
    QCOMPARE(fixture.voice().isBusy(), false);
    QCOMPARE(fixture.chat().messages().size(), std::size_t{1});
}

// The user can type and send while speech recognition is still running. The
// transcript must not be dropped by Chat::send(); it is held until the reply
// finishes and then posted.
void TestVoiceInput::test_transcriptIsSentAfterTheStreamingReply() {
    Fixture fixture;
    fixture.voice().transcribeAndSend();
    fixture.chat().send(QStringLiteral("typed"));
    QCOMPARE(fixture.chat().isStreaming(), true);

    fixture.transcriber().succeedWith(QStringLiteral("spoken"));
    // The fake's queued terminal signal is delivered once the event loop runs.
    QTRY_COMPARE(fixture.voice().isBusy(), false);
    // Still queued: the chat is busy with the typed message.
    QCOMPARE(fixture.chat().messages().size(), std::size_t{1});

    fixture.provider().lastAnswer()->complete(Message(MessageRole::kAssistant, QStringLiteral("answer")));

    QCOMPARE(fixture.chat().messages().size(), std::size_t{3});
    QCOMPARE(fixture.chat().messages().at(1).isUser(), false);
    QCOMPARE(fixture.chat().messages().at(2).text(), QStringLiteral("spoken"));
    QCOMPARE(fixture.chat().messages().at(2).isUser(), true);
    QCOMPARE(fixture.provider().respondCount(), 2);
}

// Only results of a request the command itself started are auto-sent: a
// transcription requested directly on the facade stays untouched.
void TestVoiceInput::test_externalTranscriptionIsNotSent() {
    Fixture fixture;
    fixture.transcriber().succeedWith(QStringLiteral("external"));
    const QSignalSpy kTranscribedSpy(&fixture.capture(), &AudioCapture::transcribed);

    fixture.capture().transcribeLast();

    QTRY_COMPARE(kTranscribedSpy.count(), 1);
    QCOMPARE(fixture.chat().messages().size(), std::size_t{0});
    QCOMPARE(fixture.provider().respondCount(), 0);
}

void TestVoiceInput::test_externalFailureIsIgnored() {
    Fixture fixture;
    fixture.transcriber().failWith(ProviderError::kNetwork, QStringLiteral("boom"));
    const QSignalSpy kFailedSpy(&fixture.capture(), &AudioCapture::failed);

    fixture.capture().transcribeLast();

    QTRY_COMPARE(kFailedSpy.count(), 1);
    // A failure that this command did not start does not touch its state.
    QCOMPARE(fixture.voice().isBusy(), false);
    QCOMPARE(fixture.chat().messages().size(), std::size_t{0});
}

void TestVoiceInput::test_nullChatDoesNotSend() {
    audio::AudioBuffer buffer(kBufferDuration, canonicalFormat());
    FakeAudioSource source;
    FakeTranscriber transcriber;
    AudioCapture capture(buffer, source, transcriber);
    VoiceInput voice(capture, nullptr);
    transcriber.succeedWith(QStringLiteral("nowhere"));

    voice.transcribeAndSend();

    // There is nowhere to send the text, so the command does not even spend a
    // recognition request.
    QCOMPARE(voice.isBusy(), false);
    QCOMPARE(transcriber.callCount(), 0);
}

QTEST_GUILESS_MAIN(TestVoiceInput)

#include "tst_voiceinput.moc"
