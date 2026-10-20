// Tests for the capture/transcription value type (CaptureConfig): defaults,
// boundary values and the user-facing validation reasons.
#include <QString>
#include <QtTest>

#include "domain/CaptureConfig.h"

class TestCaptureConfig : public QObject {
    Q_OBJECT

public:
    explicit TestCaptureConfig(QObject* parent = nullptr);

private slots:
    void test_defaultsMatchTheProductContract();
    void test_acceptsBoundaryValues_data();
    void test_acceptsBoundaryValues();
    void test_rejectsDurationOutOfRange_data();
    void test_rejectsDurationOutOfRange();
    void test_rejectsMissingModel();
    void test_rejectsMultilineModel();
    void test_rejectsMultilineLanguage();
    void test_rejectsMultilinePrompt();
    void test_rejectsMinSpeechOutOfRange_data();
    void test_rejectsMinSpeechOutOfRange();
    void test_allowsEmptyLanguageAndPrompt();
};

TestCaptureConfig::TestCaptureConfig(QObject* parent) : QObject(parent) {
}

void TestCaptureConfig::test_defaultsMatchTheProductContract() {
    const CaptureConfig kConfig;

    QCOMPARE(kConfig.captureDurationSeconds, 20);
    QCOMPARE(kConfig.sttModel, QStringLiteral("openai/whisper-1"));
    QCOMPARE(kConfig.sttLanguage, QStringLiteral("ru"));
    QVERIFY(kConfig.sttPrompt.isEmpty());
    QCOMPARE(kConfig.vadEnabled, true);
    QCOMPARE(kConfig.minSpeechSeconds, 0.75);
    QVERIFY(kConfig.validationError().isEmpty());
}

void TestCaptureConfig::test_acceptsBoundaryValues_data() {
    QTest::addColumn<int>("duration");
    QTest::addColumn<double>("minSpeech");

    QTest::newRow("minimum duration") << 5 << 0.75;
    QTest::newRow("maximum duration") << 120 << 0.75;
    QTest::newRow("minimum speech") << 20 << 0.25;
    QTest::newRow("maximum speech") << 20 << 3.0;
    QTest::newRow("both minima") << 5 << 0.25;
    QTest::newRow("both maxima") << 120 << 3.0;
}

void TestCaptureConfig::test_acceptsBoundaryValues() {
    QFETCH(int, duration);      // NOLINT(misc-const-correctness) - QFETCH cannot be const
    QFETCH(double, minSpeech);  // NOLINT(misc-const-correctness) - QFETCH cannot be const

    CaptureConfig config;
    config.captureDurationSeconds = duration;
    config.minSpeechSeconds = minSpeech;

    QVERIFY(config.validationError().isEmpty());
}

void TestCaptureConfig::test_rejectsDurationOutOfRange_data() {
    QTest::addColumn<int>("duration");

    QTest::newRow("below minimum") << 4;
    QTest::newRow("above maximum") << 121;
    QTest::newRow("negative") << -1;
    QTest::newRow("zero") << 0;
}

void TestCaptureConfig::test_rejectsDurationOutOfRange() {
    QFETCH(int, duration);  // NOLINT(misc-const-correctness) - QFETCH cannot be const

    CaptureConfig config;
    config.captureDurationSeconds = duration;

    QCOMPARE(config.validationError(), QStringLiteral("Capture duration must be between 5 and 120 seconds."));
}

void TestCaptureConfig::test_rejectsMissingModel() {
    CaptureConfig blank;
    blank.sttModel = QString();
    QCOMPARE(blank.validationError(), QStringLiteral("Speech-to-text model is required."));

    CaptureConfig spaces;
    spaces.sttModel = QStringLiteral("   ");
    QCOMPARE(spaces.validationError(), QStringLiteral("Speech-to-text model is required."));
}

void TestCaptureConfig::test_rejectsMultilineModel() {
    CaptureConfig newline;
    newline.sttModel = QStringLiteral("whisper\nlarge");
    QCOMPARE(newline.validationError(), QStringLiteral("Speech-to-text model must be a single line."));

    CaptureConfig carriageReturn;
    carriageReturn.sttModel = QStringLiteral("whisper\rlarge");
    QCOMPARE(carriageReturn.validationError(), QStringLiteral("Speech-to-text model must be a single line."));
}

void TestCaptureConfig::test_rejectsMultilineLanguage() {
    CaptureConfig config;
    config.sttLanguage = QStringLiteral("ru\nen");

    QCOMPARE(config.validationError(), QStringLiteral("Speech-to-text language must be a single line."));
}

void TestCaptureConfig::test_rejectsMultilinePrompt() {
    CaptureConfig newline;
    newline.sttPrompt = QStringLiteral("first line\nsecond line");
    QCOMPARE(newline.validationError(), QStringLiteral("Context must be a single line."));

    CaptureConfig carriageReturn;
    carriageReturn.sttPrompt = QStringLiteral("first\rsecond");
    QCOMPARE(carriageReturn.validationError(), QStringLiteral("Context must be a single line."));
}

void TestCaptureConfig::test_rejectsMinSpeechOutOfRange_data() {
    QTest::addColumn<double>("minSpeech");

    QTest::newRow("below minimum") << 0.24;
    QTest::newRow("above maximum") << 3.01;
    QTest::newRow("negative") << -1.0;
    QTest::newRow("zero") << 0.0;
}

void TestCaptureConfig::test_rejectsMinSpeechOutOfRange() {
    QFETCH(double, minSpeech);  // NOLINT(misc-const-correctness) - QFETCH cannot be const

    CaptureConfig config;
    config.minSpeechSeconds = minSpeech;

    QCOMPARE(config.validationError(), QStringLiteral("Minimum speech duration must be between 0.25 and 3.0 seconds."));
}

void TestCaptureConfig::test_allowsEmptyLanguageAndPrompt() {
    // Empty language means "detect automatically"; empty prompt means "send no
    // context". Both are valid, configurable states rather than errors.
    CaptureConfig config;
    config.sttLanguage = QString();
    config.sttPrompt = QString();

    QVERIFY(config.validationError().isEmpty());
}

QTEST_GUILESS_MAIN(TestCaptureConfig)

#include "tst_capture_config.moc"
