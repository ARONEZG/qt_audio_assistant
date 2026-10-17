#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>
#include <QVariant>
#include <memory>

#include "app/PlaceholderProvider.h"
#include "domain/Chats.h"
#include "domain/NewChat.h"
#include "domain/ProviderOffer.h"
#include "domain/Providers.h"
#include "logging/LogBridge.hpp"

int main(int argc, char* argv[]) {
    // QGuiApplication must outlive the event loop; it is intentionally not
    // const even though exec() is static (canonical Qt entry-point pattern).
    QGuiApplication app(argc, argv);  // NOLINT(misc-const-correctness)

    // RAII guard: the bridge lives for the whole event loop. Non-const by
    // convention, mirroring std::lock_guard.
    audio_assistant::log::LogBridge logBridge;  // NOLINT(misc-const-correctness)

    // Composition root. The domain objects are built here, and only here, and
    // injected into the view; the view creates none of them. The provider is a
    // local in-memory placeholder for now (the networked Polza provider is the
    // next increment), offered through the same recipe value a real one will use.
    Chats chats;
    // Not const: both objects are handed to QML by non-const pointer through
    // setInitialProperties below.
    Providers providers({
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
