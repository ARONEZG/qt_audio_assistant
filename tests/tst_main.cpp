#include <qlogging.h>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QtTest>
#include <array>
#include <cstring>

#include "AppController.h"

namespace {
// Хранилище последнего warning/critical-сообщения Qt. Тривиальные типы POD,
// без нетривиальных деструкторов (-Wexit-time-destructors) и без неconst
// глобальных объектов с динамической памятью.
struct MessageCapture {
    int count = 0;
    std::array<char, 512> lastMessage{};
};

MessageCapture& capture() {
    static MessageCapture messageCapture;
    return messageCapture;
}

void qmlMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    static_cast<void>(context);
    if (type != QtWarningMsg && type != QtCriticalMsg) {
        return;
    }
    MessageCapture& state = capture();
    ++state.count;
    const QByteArray utf8 = message.toUtf8();
    const auto bytes = static_cast<size_t>(utf8.size());
    const size_t limit = state.lastMessage.size() - 1;
    const size_t toCopy = bytes < limit ? bytes : limit;
    std::memcpy(state.lastMessage.data(), utf8.constData(), toCopy);
    state.lastMessage.at(toCopy) = '\0';
}

// Включает перехват и сбрасывает буфер.
void startCapture() {
    capture() = MessageCapture{};
    qInstallMessageHandler(&qmlMessageHandler);
}

// Отключает перехват и возвращает число warning/critical-сообщений.
int stopCapture() {
    qInstallMessageHandler(nullptr);
    return capture().count;
}

// Формирует описание последнего перехваченного сообщения.
QString describeCapture() {
    const MessageCapture& state = capture();
    return QStringLiteral("Qt warning/critical message: %1").arg(QString::fromUtf8(state.lastMessage.data()));
}
}  // namespace

class TestQmlLoad : public QObject {
    Q_OBJECT
public:
    explicit TestQmlLoad(QObject* parent = nullptr);

private slots:
    void test_loadRootComponent();
    void test_statusBindingToController();
};

TestQmlLoad::TestQmlLoad(QObject* parent) : QObject(parent) {
}

void TestQmlLoad::test_loadRootComponent() {
    startCapture();

    // Main.qml ссылается на context property "controller"; без него движок
    // логирует ReferenceError. Тест повторяет проводку из main.cpp.
    // Порядок важен: controller обязан существовать раньше engine, иначе
    // первичное вычисление binding происходит до установки контекста.
    AppController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QString::fromUtf8(controllerContext::kPropertyName), &controller);

    engine.load(QUrl::fromUserInput(QStringLiteral("qrc:/qt/qml/AudioAssistant/Main.qml")));

    QCOMPARE(engine.rootObjects().isEmpty(), false);

    // Загрузка не должна порождать ReferenceError на context property.
    const int warningCount = stopCapture();
    QVERIFY2(warningCount == 0, qPrintable(describeCapture()));
}

// Тест проверяет несколько аспектов одного контракта (загрузка, поиск label,
// начальное значение, реакция на сигнал, отсутствие предупреждений) и по
// природе длиннее production-функции; поэтому NOLINT для readability-function-size.
// NOLINTNEXTLINE(readability-function-size)
void TestQmlLoad::test_statusBindingToController() {
    // Интеграционный шов C++↔QML: binding controller.status читается из
    // AppController и обновляется по сигналу statusChanged.
    startCapture();

    AppController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QString::fromUtf8(controllerContext::kPropertyName), &controller);

    engine.load(QUrl::fromUserInput(QStringLiteral("qrc:/qt/qml/AudioAssistant/Main.qml")));

    // findChild по objectName "statusLabel" (Main.qml).
    const auto* label = engine.rootObjects().first()->findChild<QQuickItem*>(QStringLiteral("statusLabel"));
    QVERIFY2(label != nullptr, "status label 'statusLabel' not found");

    // Текст статуса должен совпадать с начальным значением контроллера.
    QCOMPARE(label->property("text").toString(), controller.status());

    // NOTIFY statusChanged должен обновить binding в QML.
    controller.setStatus(QStringLiteral("processing"));
    QCOMPARE(label->property("text").toString(), controller.status());

    const int warningCount = stopCapture();
    QVERIFY2(warningCount == 0, qPrintable(describeCapture()));
}

QTEST_MAIN(TestQmlLoad)

#include "tst_main.moc"
