#include <spdlog/logger.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <QCoreApplication>
#include <QMessageLogContext>
#include <QProcess>
#include <QString>
#include <QtTest>
#include <memory>
#include <string>

#include "logging/LogBridge.hpp"

namespace {

/// A spdlog sink that captures the most recent log message for verification.
/// sink_it_() is invoked by spdlog with base_sink's mutex held; the accessors
/// are read from single-threaded test code.
class StringSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    [[nodiscard]] std::string lastMessage() const {
        return lastMessage_;
    }

    [[nodiscard]] spdlog::level::level_enum lastLevel() const {
        return lastLevel_;
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        lastMessage_ = std::string(msg.payload.data(), msg.payload.size());
        lastLevel_ = msg.level;
    }

    void flush_() override {
    }

private:
    std::string lastMessage_;
    spdlog::level::level_enum lastLevel_{spdlog::level::off};
};

/// Redirects the current spdlog default logger into `sink`, preserving the
/// logger's level so level filtering keeps working.
void redirectDefaultLoggerTo(const std::shared_ptr<StringSink>& sink) {
    auto logger = spdlog::default_logger();
    logger->sinks().clear();
    logger->sinks().push_back(sink);
}

/// Returns the currently installed Qt message handler without leaving it
/// modified (qInstallMessageHandler swaps and returns the previous handler).
QtMessageHandler currentHandler() {
    QtMessageHandler handler = qInstallMessageHandler(nullptr);
    qInstallMessageHandler(handler);
    return handler;
}

/// Outcome of running the test binary in `--fatal-child` mode.
struct FatalChildResult {
    QByteArray output;
    bool terminated{false};
};

/// Re-invokes this test binary as a child, which emits a single qFatal message,
/// and reports the child's merged output and whether it terminated abnormally.
FatalChildResult runFatalChild() {
    QProcess child;
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--fatal-child")});
    const bool kFinished = child.waitForFinished(30000);

    FatalChildResult result;
    result.output = child.readAllStandardOutput();
    // std::abort() raises SIGABRT on POSIX (QProcess reports a crash); on
    // Windows it terminates with a non-zero exit code and no crash status.
    result.terminated = kFinished && (child.exitStatus() == QProcess::CrashExit || child.exitCode() != 0);
    return result;
}

}  // anonymous namespace

class TestLogging : public QObject {
    Q_OBJECT

public:
    explicit TestLogging(QObject* parent = nullptr) : QObject(parent) {
    }

private slots:
    void test_routesDebugMessage();
    void test_routesWarningMessage();
    void test_routesCriticalMessage();
    void test_filtersMessagesBelowLevel();
    void test_restoresQtHandler();
    void test_restoresDefaultLogger();
    void test_fatalAbortsProcess();
};

void TestLogging::test_routesDebugMessage() {
    // RAII guard; non-const by convention (mirrors std::lock_guard).
    audio_assistant::log::LogBridge bridge{spdlog::level::trace};  // NOLINT(misc-const-correctness)
    auto sink = std::make_shared<StringSink>();
    redirectDefaultLoggerTo(sink);

    qDebug().noquote() << QStringLiteral("debug_routed");

    QVERIFY(sink->lastMessage().find("debug_routed") != std::string::npos);
    QCOMPARE(sink->lastLevel(), spdlog::level::debug);
}

void TestLogging::test_routesWarningMessage() {
    audio_assistant::log::LogBridge bridge;  // NOLINT(misc-const-correctness)
    auto sink = std::make_shared<StringSink>();
    redirectDefaultLoggerTo(sink);

    qWarning().noquote() << QStringLiteral("warning_routed");

    QVERIFY(sink->lastMessage().find("warning_routed") != std::string::npos);
    QCOMPARE(sink->lastLevel(), spdlog::level::warn);
}

void TestLogging::test_routesCriticalMessage() {
    audio_assistant::log::LogBridge bridge;  // NOLINT(misc-const-correctness)
    auto sink = std::make_shared<StringSink>();
    redirectDefaultLoggerTo(sink);

    qCritical().noquote() << QStringLiteral("critical_routed");

    QVERIFY(sink->lastMessage().find("critical_routed") != std::string::npos);
    QCOMPARE(sink->lastLevel(), spdlog::level::err);
}

void TestLogging::test_filtersMessagesBelowLevel() {
    audio_assistant::log::LogBridge bridge{spdlog::level::warn};  // NOLINT(misc-const-correctness)
    auto sink = std::make_shared<StringSink>();
    redirectDefaultLoggerTo(sink);

    // Debug is below the configured level and must not reach the sink
    qDebug().noquote() << QStringLiteral("debug_suppressed");
    QCOMPARE(sink->lastLevel(), spdlog::level::off);

    // Warning is at the configured level and must pass through
    qWarning().noquote() << QStringLiteral("warning_kept");
    QCOMPARE(sink->lastLevel(), spdlog::level::warn);
}

void TestLogging::test_restoresQtHandler() {
    const QtMessageHandler kOriginalHandler = currentHandler();

    {
        audio_assistant::log::LogBridge bridge;  // NOLINT(misc-const-correctness)
        QVERIFY(currentHandler() != kOriginalHandler);
    }

    QVERIFY(currentHandler() == kOriginalHandler);
}

void TestLogging::test_restoresDefaultLogger() {
    const auto kOriginalLogger = spdlog::default_logger();

    {
        audio_assistant::log::LogBridge bridge;  // NOLINT(misc-const-correctness)
        QVERIFY(spdlog::default_logger() != kOriginalLogger);
    }

    QVERIFY(spdlog::default_logger() == kOriginalLogger);
}

void TestLogging::test_fatalAbortsProcess() {
    // qFatal() terminates the process, so it cannot be observed in-process; the
    // test binary re-invokes itself as a child and asserts on the outcome.
    const FatalChildResult kResult = runFatalChild();
    QVERIFY2(kResult.output.contains("fatal_routed"), kResult.output.constData());
    QVERIFY2(kResult.terminated, "qFatal must terminate the process");
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);  // NOLINT(misc-const-correctness)

    // Child mode for test_fatalAbortsProcess(): install the bridge, emit one
    // fatal message and let the handler terminate the process.
    if (app.arguments().contains(QStringLiteral("--fatal-child"))) {
        audio_assistant::log::LogBridge bridge;  // NOLINT(misc-const-correctness)
        qFatal("fatal_routed");
    }

    TestLogging test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_logging.moc"
