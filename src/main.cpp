#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>

#include "AppController.h"

int main(int argc, char* argv[]) {
    // QGuiApplication должен пережить event loop; это канонический Qt entry-point,
    // поэтому переменная не const даже при static-вызове exec().
    QGuiApplication app(argc, argv);  // NOLINT(misc-const-correctness)

    // AppController живёт в стеке main и переживает engine; владение отдавать
    // ему нельзя, поэтому в context property передаётся невладеющий указатель.
    AppController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QString::fromUtf8(controllerContext::kPropertyName), &controller);

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/AudioAssistant/Main.qml")));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
