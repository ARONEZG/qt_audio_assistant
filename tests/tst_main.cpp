#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>
#include <QtTest>

class TestQmlLoad : public QObject {
    Q_OBJECT
public:
    explicit TestQmlLoad(QObject* parent = nullptr);

private slots:
    void test_loadRootComponent_data();
    void test_loadRootComponent();
};

TestQmlLoad::TestQmlLoad(QObject* parent) : QObject(parent) {
}

void TestQmlLoad::test_loadRootComponent_data() {
    QTest::addColumn<QString>("resourceUrl");

    QTest::newRow("Main root component") << QStringLiteral("qrc:/qt/qml/AudioAssistant/Main.qml");
}

void TestQmlLoad::test_loadRootComponent() {
    QFETCH(QString, resourceUrl);  // NOLINT(misc-const-correctness): macro expansion

    QQmlApplicationEngine engine;
    engine.load(QUrl::fromUserInput(resourceUrl));

    QCOMPARE(engine.rootObjects().isEmpty(), false);
}

QTEST_MAIN(TestQmlLoad)

#include "tst_main.moc"
