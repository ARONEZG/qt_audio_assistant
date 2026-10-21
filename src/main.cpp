#include <QDir>
#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QStandardPaths>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <chrono>
#include <exception>
#include <memory>
#include <vector>

#include "app/ClipboardImageEncoder.h"
#include "app/OpenAiCompatibleProvider.h"
#include "app/OpenAiCompatibleTranscriber.h"
#include "app/PlaceholderProvider.h"
#include "app/SystemClipboard.h"
#include "audio/AudioBuffer.h"
#include "audio/AudioFormat.h"
#include "audio/MiniaudioCaptureSource.h"
#include "audio/SileroVoiceActivityDetector.h"
#include "audio/VadSpeechGate.h"
#include "domain/AudioCapture.h"
#include "domain/CaptureConfigFile.h"
#include "domain/CaptureConfigSource.h"
#include "domain/CapturePresets.h"
#include "domain/CaptureSettings.h"
#include "domain/Chat.h"
#include "domain/Chats.h"
#include "domain/NewChat.h"
#include "domain/PasteImage.h"
#include "domain/ProviderConfigFile.h"
#include "domain/ProviderConfigSource.h"
#include "domain/ProviderOffer.h"
#include "domain/ProviderSettings.h"
#include "domain/Providers.h"
#include "domain/VoiceInput.h"
#include "logging/LogBridge.hpp"

namespace {

// The largest capture window the user can select. The ring buffer is sized for
// it; transcribeLast() trims the snapshot down to the currently selected window.
constexpr int kMaxCaptureSeconds = 120;

// Where the user's persisted provider endpoint/model live. QStandardPaths picks
// the per-user application config directory on each supported desktop, so no
// path is hardcoded; tests inject their own path instead of calling this.
[[nodiscard]] QString providerConfigPath() {
    const QString kDirectory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (kDirectory.isEmpty()) {
        return {};
    }
    return QDir(kDirectory).filePath(QStringLiteral("provider.env"));
}

// Capture and transcription settings live in their own file next to the
// provider configuration: the two concerns are saved by different panels and
// must not overwrite each other.
[[nodiscard]] QString captureConfigPath() {
    const QString kDirectory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (kDirectory.isEmpty()) {
        return {};
    }
    return QDir(kDirectory).filePath(QStringLiteral("capture.env"));
}

// Both persisted configurations, resolved once at startup. The real process
// environment overrides the speech-to-text fields per value (see the sources);
// the API key is read only from the environment. A read or key problem is
// reported and never stops startup.
struct StartupConfig {
    ProviderConfigSource::Result provider;
    CaptureConfigSource::Result capture;
};

[[nodiscard]] StartupConfig resolveStartupConfig(const ProviderConfigFile& providerFile,
                                                 const CaptureConfigFile& captureFile) {
    const QProcessEnvironment kEnvironment = QProcessEnvironment::systemEnvironment();
    StartupConfig startup{ProviderConfigSource(providerFile, kEnvironment).resolve(),
                          CaptureConfigSource(captureFile, kEnvironment).resolve()};
    if (!startup.provider.ok()) {
        qWarning().noquote() << startup.provider.error;
    }
    if (startup.provider.apiKey.isEmpty()) {
        qWarning(
                "POLZA_API_KEY is not set: set it in the environment or enter it in the provider "
                "settings panel; until then the OpenAI-compatible provider reports an "
                "authentication error.");
    }
    if (!startup.capture.ok()) {
        qWarning().noquote() << startup.capture.error;
    }
    return startup;
}

// Builds the providers offered for selection. The network recipe reads the live
// settings every time a chat is minted, so a newly saved endpoint/model/key
// reaches the next chat without rebuilding the list.
[[nodiscard]] std::vector<ProviderOffer> providerOffers(ProviderSettings& settings) {
    return {
            ProviderOffer(QStringLiteral("openai-compatible"),
                          QStringLiteral("OpenAI-compatible"),
                          [&settings] {
                              return std::make_unique<OpenAiCompatibleProvider>(
                                      settings.baseUrl(), settings.apiKey(), settings.model());
                          }),
            ProviderOffer(QStringLiteral("placeholder"),
                          QStringLiteral("Placeholder (offline)"),
                          [] { return std::make_unique<PlaceholderProvider>(); }),
    };
}

// Opens the single chat the application starts with, using the first offered
// provider. The settings dialog can switch the provider later (rebuilding this
// chat) or change the endpoint/model/key, which the rebuild picks up because the
// provider recipe reads the live settings. A provider list can be empty in a
// misconfigured build; that is a supported no-chat state.
void openInitialChat(NewChat& newChat, const Providers& providers) {
    if (providers.count() > 0) {
        (void)newChat.open(providers.idAt(0));
    }
}

// The objects the QML module binds to, grouped so the loader takes one argument
// instead of one per view model. Pointers rather than references: the guideline
// forbids reference data members.
struct Composition {
    Chats* chats;
    Providers* providers;
    NewChat* newChat;
    ProviderSettings* settings;
    AudioCapture* audioCapture;
    CaptureSettings* captureSettings;
    VoiceInput* voiceInput;
    PasteImage* pasteImage;
};

// The clipboard adapter and the paste command. Members are declared in
// dependency order: the command borrows the adapter and is destroyed first.
struct PasteStack {
    SystemClipboard clipboard;
    ClipboardImageEncoder encoder;
    PasteImage command{clipboard, encoder};
};

// Loads the root QML component and hands the composition objects to it. False
// means the component could not be created and the caller exits non-zero.
[[nodiscard]] bool loadMainQml(QQmlApplicationEngine& engine, const Composition& composition) {
    engine.setInitialProperties({
            {QStringLiteral("chats"), QVariant::fromValue(composition.chats)},
            {QStringLiteral("providers"), QVariant::fromValue(composition.providers)},
            {QStringLiteral("newChat"), QVariant::fromValue(composition.newChat)},
            {QStringLiteral("providerSettings"), QVariant::fromValue(composition.settings)},
            {QStringLiteral("audioCapture"), QVariant::fromValue(composition.audioCapture)},
            {QStringLiteral("captureSettings"), QVariant::fromValue(composition.captureSettings)},
            {QStringLiteral("voiceInput"), QVariant::fromValue(composition.voiceInput)},
            {QStringLiteral("pasteImage"), QVariant::fromValue(composition.pasteImage)},
    });
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/AudioAssistant/Main.qml")));
    return !engine.rootObjects().isEmpty();
}

// Starts/stops capture with the chat's switch and reports the outcome honestly.
// A failed start reverts the switch, so the UI never claims to record when the
// microphone is unavailable; health is read from the concrete source, so the
// AudioSource role interface stays unchanged.
void wireCaptureSwitch(Chat& chat, AudioCapture& audioCapture, audio::MiniaudioCaptureSource& source) {
    QObject::connect(&chat, &Chat::backgroundCaptureChanged, &audioCapture, [&chat, &audioCapture, &source] {
        if (!chat.isBackgroundCaptureEnabled()) {
            audioCapture.stop();
            return;
        }
        audioCapture.start();
        if (source.lastError().has_value()) {
            qWarning() << "Audio capture did not start: the microphone is unavailable; the switch is reverted";
            chat.setBackgroundCaptureEnabled(false);
        } else {
            qInfo() << "Audio capture started (16 kHz, mono, f32)";
        }
    });
}

// Wires the open chat's capture switch to the pipeline. Capture does not start at
// launch: it runs only while the switch is on.
void wireChatCaptureSwitch(Chats& chats, AudioCapture& capture, audio::MiniaudioCaptureSource& source) {
    if (Chat* chat = chats.at(0); chat != nullptr) {
        wireCaptureSwitch(*chat, capture, source);
    }
}

// Keeps the transcriber's endpoint/key in sync with the live settings and logs
// the startup configuration once. The API key is a session secret entered in the
// settings panel (never persisted), so a transcriber frozen at startup would
// otherwise keep an empty or stale key and reject every request. The
// speech-to-text model/language/prompt are a separate concern and are kept in
// sync by wireCaptureSettings().
void wireTranscriberSettings(ProviderSettings& settings,
                             OpenAiCompatibleTranscriber& transcriber,
                             const QString& sttModel) {
    QObject::connect(&settings, &ProviderSettings::saved, &transcriber, [&settings, &transcriber] {
        transcriber.setConfiguration(settings.baseUrl(), settings.apiKey());
        // Report the new value (or <unset>) like the startup log, so a "saved
        // but still failing" scenario is diagnosable without inferring state.
        qInfo().noquote() << "Speech-to-text configuration updated: baseUrl="
                          << (settings.baseUrl().isEmpty() ? QStringLiteral("<unset>") : settings.baseUrl())
                          << "apiKey="
                          << (settings.apiKey().isEmpty() ? QStringLiteral("<unset>") : QStringLiteral("set"));
    });
    qInfo().noquote() << "Speech-to-text:" << "baseUrl="
                      << (settings.baseUrl().isEmpty() ? QStringLiteral("<unset>") : settings.baseUrl())
                      << "apiKey=" << (settings.apiKey().isEmpty() ? QStringLiteral("<unset>") : QStringLiteral("set"))
                      << "model=" << sttModel;
}

// Applies saved capture settings to the live pipeline: the next request uses the
// new window, VAD/guard options and speech-to-text model/language/context. The
// log reports the effective values so a "saved but not applied" scenario is
// diagnosable without inferring state.
void wireCaptureSettings(CaptureSettings& settings,
                         AudioCapture& audioCapture,
                         OpenAiCompatibleTranscriber& transcriber) {
    QObject::connect(&settings, &CaptureSettings::saved, &audioCapture, [&settings, &audioCapture, &transcriber] {
        audioCapture.setCaptureOptions(
                settings.captureDurationSeconds(), settings.vadEnabled(), settings.minSpeechSeconds());
        transcriber.setSpeechOptions(settings.sttModel(), settings.sttLanguage(), settings.sttPrompt());
        qInfo().noquote() << "Capture settings updated:" << "windowSeconds=" << settings.captureDurationSeconds()
                          << "vad=" << settings.vadEnabled() << "minSpeechSeconds=" << settings.minSpeechSeconds()
                          << "model=" << settings.sttModel() << "language="
                          << (settings.sttLanguage().isEmpty() ? QStringLiteral("<auto>") : settings.sttLanguage())
                          << "prompt="
                          << (settings.sttPrompt().isEmpty() ? QStringLiteral("<none>") : settings.sttPrompt());
    });
}

// The optional VAD pipeline: the Silero detector (model from the embedded Qt
// resource) and the speech gate that trims silence. Both are owned by this
// object, which the caller keeps alive for the whole event loop (the gate
// borrows the detector; AudioCapture borrows the gate). A null gate means VAD
// is disabled.
struct VadPipeline {
    std::unique_ptr<audio::SileroVoiceActivityDetector> detector;
    std::unique_ptr<audio::VadSpeechGate> gate;
};

// Builds the VAD pipeline. Graceful degradation: a missing resource or
// onnxruntime failure is logged and yields an empty pipeline — capture and
// transcription then work without the gate, exactly like an unavailable
// microphone is reported instead of aborting the process.
[[nodiscard]] VadPipeline createVadPipeline() {
    try {
        auto detector = std::make_unique<audio::SileroVoiceActivityDetector>();
        // minSilenceFrames 16 = 512 ms bridges short pauses inside a phrase.
        // Longer natural pauses still split the stream into intervals; the gate
        // re-merges the fresh ones into one utterance (1.5 s merge window, see
        // VadSpeechGate.cpp), so the phrase is not truncated to its tail.
        auto gate = std::make_unique<audio::VadSpeechGate>(*detector,
                                                           audio::SpeechSegmenter::Parameters{0.5F, 0.15F, 8, 16});
        return {std::move(detector), std::move(gate)};
    } catch (const std::exception& error) {
        qWarning() << "Voice activity detection disabled:" << error.what();
        return {};
    }
}

// The capture pipeline owned by main() for the whole event loop. Members are
// declared in dependency order - the buffer before the source that writes into
// it, then the transcriber, the VAD pipeline and the facade that borrows all of
// them - so destruction runs in the opposite order. The composition root wires
// the resolved capture configuration in once; later changes arrive through
// CaptureSettings::saved (wireCaptureSettings).
class CapturePipeline {
public:
    CapturePipeline(audio::AudioFormat format, ProviderSettings& providerSettings, const CaptureConfig& config)
        : buffer_(std::chrono::seconds{kMaxCaptureSeconds}, format),
          source_(buffer_),
          transcriber_(providerSettings.baseUrl(),
                       providerSettings.apiKey(),
                       config.sttModel,
                       0,
                       config.sttLanguage,
                       config.sttPrompt),
          vad_(createVadPipeline()),
          capture_(buffer_, source_, transcriber_) {
        if (vad_.gate != nullptr) {
            capture_.setVadGate(*vad_.gate);
        }
        capture_.setCaptureOptions(config.captureDurationSeconds, config.vadEnabled, config.minSpeechSeconds);
    }

    [[nodiscard]] AudioCapture& capture() noexcept {
        return capture_;
    }
    [[nodiscard]] audio::MiniaudioCaptureSource& source() noexcept {
        return source_;
    }
    [[nodiscard]] OpenAiCompatibleTranscriber& transcriber() noexcept {
        return transcriber_;
    }

private:
    audio::AudioBuffer buffer_;
    audio::MiniaudioCaptureSource source_;
    OpenAiCompatibleTranscriber transcriber_;
    VadPipeline vad_;
    AudioCapture capture_;
};

}  // namespace

int main(int argc, char* argv[]) {
    // Must outlive the event loop; non-const is the canonical Qt entry-point pattern.
    QGuiApplication app(argc, argv);  // NOLINT(misc-const-correctness)

    // RAII guard: the bridge lives for the whole event loop.
    audio_assistant::log::LogBridge logBridge;  // NOLINT(misc-const-correctness)

    // Composition root: live settings shared by the provider recipe and the pipeline.
    const ProviderConfigFile kConfigFile(providerConfigPath());
    const CaptureConfigFile kCaptureConfigFile(captureConfigPath());
    const StartupConfig kStartup = resolveStartupConfig(kConfigFile, kCaptureConfigFile);
    // Not const: edited from QML and read through references by the recipes.
    ProviderSettings settings(kStartup.provider.config, kStartup.provider.apiKey, kConfigFile);
    // The presets are owned here and borrowed by the settings view model; the
    // capture file is owned by CaptureSettings (the persistence command).
    CapturePresets capturePresets;
    CaptureSettings captureSettings(kStartup.capture.config, kCaptureConfigFile, &capturePresets);
    Chats chats;
    Providers providers(providerOffers(settings));
    NewChat newChat(chats, providers);

    openInitialChat(newChat, providers);

    // The capture pipeline is owned here for the whole event loop: the buffer is
    // sized for the largest selectable window and transcribeLast() trims each
    // request to the configured (possibly shorter) one. Declaration order in
    // CapturePipeline is a lifetime contract, as is declaring the pipeline
    // before the engine that exposes it to QML.
    const audio::AudioFormat kCaptureFormat(16000U, 1U, audio::SampleFormat::kFloat32);
    CapturePipeline pipeline(kCaptureFormat, settings, kStartup.capture.config);
    wireTranscriberSettings(settings, pipeline.transcriber(), kStartup.capture.config.sttModel);
    wireCaptureSettings(captureSettings, pipeline.capture(), pipeline.transcriber());

    // The chat's capture switch drives the pipeline (capture is off at launch).
    wireChatCaptureSwitch(chats, pipeline.capture(), pipeline.source());

    // The single path behind Ctrl+R and the Transcribe button: recognize the
    // last utterance and post it as a chat message. Declared after the pipeline
    // and before the engine, so it outlives every QML object that references it.
    // A null chat (no provider offered) is a supported no-chat state: the
    // command is disabled because there is nowhere to post the text.
    VoiceInput voiceInput(pipeline.capture(), chats.at(0));

    // The clipboard adapter and the paste command. The adapter reads the system
    // clipboard; the command is the single path behind the composer's paste
    // gesture. Declared before the engine (which exposes the command to QML), so
    // it outlives every QML object that references it.
    PasteStack pasteStack;

    QQmlApplicationEngine engine;
    const Composition kComposition{&chats,
                                   &providers,
                                   &newChat,
                                   &settings,
                                   &pipeline.capture(),
                                   &captureSettings,
                                   &voiceInput,
                                   &pasteStack.command};
    return loadMainQml(engine, kComposition) ? app.exec() : -1;
}
