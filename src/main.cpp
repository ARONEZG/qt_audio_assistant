#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QUrl>
#include <QVariant>
#include <memory>

#include "app/OpenAiCompatibleProvider.h"
#include "app/PlaceholderProvider.h"
#include "domain/Chats.h"
#include "domain/NewChat.h"
#include "domain/ProviderOffer.h"
#include "domain/Providers.h"
#include "logging/LogBridge.hpp"

namespace {

// Reads the network provider's configuration from the environment (MVP: no
// settings store yet). The key is a secret and is never logged, committed or
// defaulted; url and model have no sensible universal default either, so all
// three are configuration the user supplies.
struct ProviderConfig {
    QString baseUrl;
    QString apiKey;
    QString model;
};

ProviderConfig readProviderConfig() {
    const QProcessEnvironment kEnv = QProcessEnvironment::systemEnvironment();
    return ProviderConfig{
            kEnv.value(QStringLiteral("POLZA_BASE_URL")),
            kEnv.value(QStringLiteral("POLZA_API_KEY")),
            kEnv.value(QStringLiteral("POLZA_MODEL")),
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

    // Composition root. The domain objects are built here, and only here, and
    // injected into the view; the view creates none of them. The provider list
    // carries one recipe per selectable provider; the network recipe captures
    // the environment configuration, so each chat mints its own client.
    const ProviderConfig kConfig = readProviderConfig();
    if (kConfig.apiKey.isEmpty()) {
        qWarning(
                "POLZA_API_KEY is not set: the OpenAI-compatible provider will report an "
                "authentication error until it is configured.");
    }

    Chats chats;
    // Not const: both objects are handed to QML by non-const pointer through
    // setInitialProperties below.
    Providers providers({
            ProviderOffer(QStringLiteral("openai-compatible"),
                          QStringLiteral("OpenAI-compatible"),
                          [kConfig] {
                              return std::make_unique<OpenAiCompatibleProvider>(
                                      kConfig.baseUrl, kConfig.apiKey, kConfig.model);
                          }),
            ProviderOffer(QStringLiteral("placeholder"),
                          QStringLiteral("Placeholder (offline)"),
                          [] { return std::make_unique<PlaceholderProvider>(); }),
    });
    NewChat newChat(chats, providers);

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
            {QStringLiteral("chats"), QVariant::fromValue(&chats)},
            {QStringLiteral("providers"), QVariant::fromValue(&providers)},
            {QStringLiteral("newChat"), QVariant::fromValue(&newChat)},
    });
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/AudioAssistant/Main.qml")));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
