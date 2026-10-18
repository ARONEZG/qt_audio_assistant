#include <QDir>
#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QStandardPaths>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <memory>
#include <vector>

#include "app/OpenAiCompatibleProvider.h"
#include "app/PlaceholderProvider.h"
#include "domain/Chats.h"
#include "domain/NewChat.h"
#include "domain/ProviderConfigFile.h"
#include "domain/ProviderConfigSource.h"
#include "domain/ProviderOffer.h"
#include "domain/ProviderSettings.h"
#include "domain/Providers.h"
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

}  // namespace

int main(int argc, char* argv[]) {
    // QGuiApplication must outlive the event loop; it is intentionally not
    // const even though exec() is static (canonical Qt entry-point pattern).
    QGuiApplication app(argc, argv);  // NOLINT(misc-const-correctness)

    // RAII guard: the bridge lives for the whole event loop. Non-const by
    // convention, mirroring std::lock_guard.
    audio_assistant::log::LogBridge logBridge;  // NOLINT(misc-const-correctness)

    // Composition root. Startup configuration is resolved once from the
    // persisted file with the real environment overriding it per value; the API
    // key is read only from the environment or the user's input, never from the
    // file. After this point the live ProviderSettings object is the single
    // source the recipes read, so a saved change affects the next minted chat.
    const ProviderConfigFile kConfigFile(providerConfigPath());
    const ProviderConfigSource::Result kStartup =
            ProviderConfigSource(kConfigFile, QProcessEnvironment::systemEnvironment()).resolve();
    if (!kStartup.ok()) {
        qWarning().noquote() << kStartup.error;
    }
    if (kStartup.apiKey.isEmpty()) {
        qWarning(
                "POLZA_API_KEY is not set: set it in the environment or enter it in the provider "
                "settings panel; until then the OpenAI-compatible provider reports an "
                "authentication error.");
    }

    // Not const: the settings are edited from QML and read through a reference
    // by the provider recipe below.
    ProviderSettings settings(kStartup.config, kStartup.apiKey, kConfigFile);
    Chats chats;
    // Not const: both objects are handed to QML by non-const pointer through
    // setInitialProperties below.
    Providers providers(providerOffers(settings));
    NewChat newChat(chats, providers);

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
            {QStringLiteral("chats"), QVariant::fromValue(&chats)},
            {QStringLiteral("providers"), QVariant::fromValue(&providers)},
            {QStringLiteral("newChat"), QVariant::fromValue(&newChat)},
            {QStringLiteral("providerSettings"), QVariant::fromValue(&settings)},
    });
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/AudioAssistant/Main.qml")));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
