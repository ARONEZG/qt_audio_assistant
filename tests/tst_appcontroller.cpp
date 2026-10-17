#include <AppController.h>

#include <QSignalSpy>
#include <QtTest>

class TestAppController : public QObject {
    Q_OBJECT
public:
    explicit TestAppController(QObject* parent = nullptr);

private slots:
    void test_initialStatus();
    void test_setStatus_emitsStatusChanged();
    void test_setStatus_unchanged_doesNotEmit();
};

TestAppController::TestAppController(QObject* parent) : QObject(parent) {
}

void TestAppController::test_initialStatus() {
    const AppController controller;

    QCOMPARE(controller.status(), QStringLiteral("Каркас приложения собран. Функции пока не реализованы."));
}

void TestAppController::test_setStatus_emitsStatusChanged() {
    AppController controller;
    const QSignalSpy spy(&controller, &AppController::statusChanged);

    controller.setStatus(QStringLiteral("запись идёт"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(controller.status(), QStringLiteral("запись идёт"));
    QCOMPARE(spy.first().first().toString(), QStringLiteral("запись идёт"));
}

void TestAppController::test_setStatus_unchanged_doesNotEmit() {
    AppController controller;
    const QSignalSpy spy(&controller, &AppController::statusChanged);

    controller.setStatus(controller.status());

    QCOMPARE(spy.count(), 0);
}

QTEST_GUILESS_MAIN(TestAppController)

#include "tst_appcontroller.moc"
