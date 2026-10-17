#pragma once

#include <spdlog/common.h>
#include <spdlog/fwd.h>

#include <QMessageLogContext>
#include <QString>
#include <memory>

namespace audio_assistant::log {

/// Bridges Qt's logging into spdlog.
///
/// Construction installs a colored console logger as spdlog's default logger
/// and registers a Qt message handler that forwards Qt's debug/info/warning/
/// critical messages to it. Destruction restores the Qt message handler and the
/// spdlog default logger that were in effect before construction.
///
/// A bridge owns the single process-wide Qt message-handler slot, so it is
/// neither copyable nor movable.
class LogBridge {
public:
    /// Installs the bridge; messages below `level` are filtered out.
    explicit LogBridge(spdlog::level::level_enum level = spdlog::level::info);

    ~LogBridge();

    LogBridge(const LogBridge&) = delete;
    LogBridge& operator=(const LogBridge&) = delete;
    LogBridge(LogBridge&&) = delete;
    LogBridge& operator=(LogBridge&&) = delete;

private:
    QtMessageHandler previousHandler_{nullptr};
    std::shared_ptr<spdlog::logger> previousLogger_;
};

}  // namespace audio_assistant::log
