// Tests for the STT environment mapping (SttLanguage).
//
// sttLanguageFromEnvironment maps POLZA_STT_LANGUAGE to the language sent to
// the speech-to-text endpoint: "ru" when the variable is absent, the empty
// string when it is explicitly set to empty (autodetection), and the value
// otherwise. sttPromptFromEnvironment maps POLZA_STT_PROMPT (absent/empty means
// no prompt field). The environment is injected, so no process environment is
// mutated.
#include <QProcessEnvironment>
#include <QString>
#include <QtTest>

#include "domain/SttLanguage.h"

class TestSttLanguage : public QObject {
    Q_OBJECT

public:
    explicit TestSttLanguage(QObject* parent = nullptr);

private slots:
    void test_absentVariableDefaultsToRussian();
    void test_emptyVariableMeansAutodetect();
    void test_configuredValueIsReturnedVerbatim();
    void test_absentPromptIsEmpty();
    void test_emptyPromptIsEmpty();
    void test_configuredPromptIsReturnedVerbatim();
};

TestSttLanguage::TestSttLanguage(QObject* parent) : QObject(parent) {
}

void TestSttLanguage::test_absentVariableDefaultsToRussian() {
    const QProcessEnvironment kEnvironment;

    QCOMPARE(sttLanguageFromEnvironment(kEnvironment), QStringLiteral("ru"));
}

void TestSttLanguage::test_emptyVariableMeansAutodetect() {
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("POLZA_STT_LANGUAGE"), QString());

    QCOMPARE(sttLanguageFromEnvironment(environment), QString());
}

void TestSttLanguage::test_configuredValueIsReturnedVerbatim() {
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("POLZA_STT_LANGUAGE"), QStringLiteral("en"));

    QCOMPARE(sttLanguageFromEnvironment(environment), QStringLiteral("en"));
}

void TestSttLanguage::test_absentPromptIsEmpty() {
    const QProcessEnvironment kEnvironment;

    QCOMPARE(sttPromptFromEnvironment(kEnvironment), QString());
}

void TestSttLanguage::test_emptyPromptIsEmpty() {
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("POLZA_STT_PROMPT"), QString());

    QCOMPARE(sttPromptFromEnvironment(environment), QString());
}

void TestSttLanguage::test_configuredPromptIsReturnedVerbatim() {
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("POLZA_STT_PROMPT"), QStringLiteral("умные указатели, C++"));

    QCOMPARE(sttPromptFromEnvironment(environment), QStringLiteral("умные указатели, C++"));
}

QTEST_GUILESS_MAIN(TestSttLanguage)

#include "tst_stt_language.moc"
