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

#include "app/OpenAiCompatibleProvider.h"
#include "app/OpenAiCompatibleTranscriber.h"
#include "app/PlaceholderProvider.h"
#include "audio/AudioBuffer.h"
#include "audio/AudioFormat.h"
#include "audio/MiniaudioCaptureSource.h"
#include "audio/SileroVoiceActivityDetector.h"
#include "audio/VadSpeechGate.h"
#include "domain/AudioCapture.h"
#include "domain/Chat.h"
#include "domain/Chats.h"
#include "domain/NewChat.h"
#include "domain/ProviderConfigFile.h"
#include "domain/ProviderConfigSource.h"
#include "domain/ProviderOffer.h"
#include "domain/ProviderSettings.h"
#include "domain/Providers.h"
#include "domain/SttLanguage.h"
#include "logging/LogBridge.hpp"

namespace {

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

// Startup configuration: the persisted file with the real environment overriding
// it per value (the API key is read only from the environment or the user's
// input, never from the file). A read or key problem is reported and never stops
// startup.
[[nodiscard]] ProviderConfigSource::Result resolveStartupConfig(const ProviderConfigFile& file) {
    const ProviderConfigSource::Result kResult =
            ProviderConfigSource(file, QProcessEnvironment::systemEnvironment()).resolve();
    if (!kResult.ok()) {
        qWarning().noquote() << kResult.error;
    }
    if (kResult.apiKey.isEmpty()) {
        qWarning(
                "POLZA_API_KEY is not set: set it in the environment or enter it in the provider "
                "settings panel; until then the OpenAI-compatible provider reports an "
                "authentication error.");
    }
    return kResult;
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

// Speech-to-text options are separate from the chat model (POLZA_MODEL): they
// are read from the environment with sensible defaults and are deliberately not
// part of the persisted provider settings. A missing/empty model falls back to
// whisper-1; language/prompt are resolved by the domain helpers (language
// defaults to "ru", prompt is omitted when empty).
struct SttOptions {
    QString model;
    QString language;
    QString prompt;
};

[[nodiscard]] QString sttModelFromEnvironment(const QProcessEnvironment& environment) {
    const QString kConfigured = environment.value(QStringLiteral("POLZA_STT_MODEL"));
    return kConfigured.isEmpty() ? QStringLiteral("openai/whisper-1") : kConfigured;
}

[[nodiscard]] SttOptions sttOptionsFromEnvironment() {
    const QProcessEnvironment kEnvironment = QProcessEnvironment::systemEnvironment();
    return {sttModelFromEnvironment(kEnvironment),
            sttLanguageFromEnvironment(kEnvironment),
            sttPromptFromEnvironment(kEnvironment)};
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

// Keeps the transcriber's endpoint/key in sync with the live settings and logs
// the startup configuration once. The API key is a session secret entered in the
// settings panel (never persisted), so a transcriber frozen at startup would
// otherwise keep an empty or stale key and reject every request. The speech-to-
// text model is a separate concern (POLZA_STT_MODEL) and stays fixed.
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
        // minSilenceFrames 16 = 512 ms: a natural pause inside a question no
        // longer cuts its tail off into a separate (discarded) interval.
        auto gate = std::make_unique<audio::VadSpeechGate>(*detector,
                                                           audio::SpeechSegmenter::Parameters{0.5F, 0.15F, 8, 16});
        return {std::move(detector), std::move(gate)};
    } catch (const std::exception& error) {
        qWarning() << "Voice activity detection disabled:" << error.what();
        return {};
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    // QGuiApplication must outlive the event loop; it is intentionally not
    // const even though exec() is static (canonical Qt entry-point pattern).
    QGuiApplication app(argc, argv);  // NOLINT(misc-const-correctness)

    // RAII guard: the bridge lives for the whole event loop. Non-const by
    // convention, mirroring std::lock_guard.
    audio_assistant::log::LogBridge logBridge;  // NOLINT(misc-const-correctness)

    // Composition root. The live ProviderSettings object is the single source
    // the provider recipe reads, so a saved change affects the next minted chat.
    const ProviderConfigFile kConfigFile(providerConfigPath());
    const ProviderConfigSource::Result kStartup = resolveStartupConfig(kConfigFile);
    // Not const: edited from QML and read through a reference by the recipe.
    ProviderSettings settings(kStartup.config, kStartup.apiKey, kConfigFile);
    Chats chats;
    Providers providers(providerOffers(settings));
    NewChat newChat(chats, providers);

    // The application opens a single chat up front with the first offered
    // provider. The settings dialog can switch the provider (rebuilding this
    // chat) or change the endpoint/model/key, which the rebuild picks up because
    // the provider recipe reads the live settings.
    if (providers.count() > 0) {
        (void)newChat.open(providers.idAt(0));
    }

    // The capture pipeline is owned here for the whole event loop; the source
    // and the facade only borrow the parts. Declaration order is a lifetime
    // contract: the buffer is declared before the source that writes into it,
    // and both before the facade that references them.
    const audio::AudioFormat kCaptureFormat(16000U, 1U, audio::SampleFormat::kFloat32);
    audio::AudioBuffer captureBuffer(std::chrono::seconds(30), kCaptureFormat);
    audio::MiniaudioCaptureSource captureSource(captureBuffer);
    const SttOptions kStt = sttOptionsFromEnvironment();
    OpenAiCompatibleTranscriber transcriber(
            settings.baseUrl(), settings.apiKey(), kStt.model, 0, kStt.language, kStt.prompt);

    // The VAD pipeline trims silence before transcription. The Silero model is
    // an embedded Qt resource (audio-assistant-models); the detector reads it
    // through ":models/silero_vad/silero_vad.onnx" and is never downloaded.
    // The detector and gate are declared before the facade they feed and live
    // for the whole event loop (lifetime contract, same as captureBuffer).
    const VadPipeline kVad = createVadPipeline();

    AudioCapture audioCapture(captureBuffer, captureSource, transcriber);
    if (kVad.gate != nullptr) {
        audioCapture.setVadGate(*kVad.gate);
    }
    wireTranscriberSettings(settings, transcriber, kStt.model);

    // The chat's capture switch drives the pipeline. Capture does not start at
    // launch: it runs only while the switch is on.
    if (Chat* chat = chats.at(0); chat != nullptr) {
        wireCaptureSwitch(*chat, audioCapture, captureSource);
    }

    QQmlApplicationEngine engine;
    const Composition kComposition{&chats, &providers, &newChat, &settings, &audioCapture};
    return loadMainQml(engine, kComposition) ? app.exec() : -1;
}
