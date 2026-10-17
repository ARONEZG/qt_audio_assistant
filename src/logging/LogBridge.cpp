#include "LogBridge.hpp"

#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdlib>

namespace audio_assistant::log {

namespace {

/// Forwards a single Qt log record to spdlog's default logger.
///
/// In Qt 6 QtMsgType has exactly five enumerators (QtDebugMsg, QtInfoMsg,
/// QtWarningMsg, QtCriticalMsg, QtFatalMsg); QtSystemMsg was removed in Qt 6
/// (deprecated since 5.4) and is not part of the enum. An if-else chain is used
/// instead of switch to avoid conflicting -Wswitch-default and
/// -Wcovered-switch-default diagnostics when all enumerators are covered, and it
/// keeps a defensive fallback to critical for any unexpected value.
void forwardToSpdlog(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    spdlog::level::level_enum level = spdlog::level::critical;
    if (type == QtDebugMsg) {
        level = spdlog::level::debug;
    } else if (type == QtInfoMsg) {
        level = spdlog::level::info;
    } else if (type == QtWarningMsg) {
        level = spdlog::level::warn;
    } else if (type == QtCriticalMsg) {
        level = spdlog::level::err;
    }

    const char* file = context.file != nullptr ? context.file : "";
    const int kLine = context.line;
    const char* func = context.function != nullptr ? context.function : "";

    spdlog::log(spdlog::source_loc{file, kLine, func}, level, "{}", message.toStdString());

    // Qt stops handling a fatal message once a custom handler is installed;
    // log it and terminate to preserve the default behaviour.
    if (type == QtFatalMsg) {
        std::abort();
    }
}

}  // anonymous namespace

LogBridge::LogBridge(spdlog::level::level_enum level)
    : previousHandler_{qInstallMessageHandler(forwardToSpdlog)}, previousLogger_{spdlog::default_logger()} {
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");

    auto logger = std::make_shared<spdlog::logger>("qt", consoleSink);
    logger->set_level(level);
    logger->flush_on(spdlog::level::warn);

    spdlog::set_default_logger(logger);
}

LogBridge::~LogBridge() {
    qInstallMessageHandler(previousHandler_);
    spdlog::set_default_logger(previousLogger_);
}

}  // namespace audio_assistant::log
