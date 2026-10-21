# ── Logging library ─────────────────────────────────────────
# The spdlog-backed Qt message-handler bridge (LogBridge). A static library so
# the application and tst_logging compile the bridge once and share the same
# implementation instead of listing the .cpp in both targets.
#
# spdlog and Qt6::Core are PUBLIC: LogBridge.hpp exposes spdlog::level::level_enum,
# QMessageLogContext and QString in its constructor signature, so every consumer
# of the header needs those include paths and the spdlog symbols at link time.
add_library(audio-assistant-logging STATIC
    LogBridge.cpp
)

set_target_properties(audio-assistant-logging PROPERTIES
    CXX_EXTENSIONS OFF
)

# Consumers include the header as "logging/LogBridge.hpp".
target_include_directories(audio-assistant-logging PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(audio-assistant-logging
    PUBLIC Qt6::Core spdlog::spdlog
    PRIVATE audio-assistant-strict
)
