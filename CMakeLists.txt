cmake_minimum_required(VERSION 3.26)

project(audio-assistant
    VERSION 0.1.0
    DESCRIPTION "Audio Assistant — minimal build skeleton"
    LANGUAGES CXX
)

# ── Export compile_commands.json for static analysis ─────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# qt_add_qml_module / moc require AUTOMOC
set(CMAKE_AUTOMOC ON)

# ── Clang enforcement (strict profile per AGENTS.md) ────────
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "Strict profile requires Clang compiler. "
        "Found: ${CMAKE_CXX_COMPILER_ID} (${CMAKE_CXX_COMPILER}).\n"
        "Configure with:\n"
        "  CC=clang-18 CXX=clang++-18 cmake -S . -B build"
    )
endif()

# ── Qt 6 components ─────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
)

find_package(Qt6 REQUIRED COMPONENTS
    Test
)

# ── QML module ──────────────────────────────────────────────
# Put Main.qml at the root of the module resources (default behaviour
# would keep the qml/ subdirectory, resulting in qrc:/qt/qml/AudioAssistant/qml/Main.qml).
# The alias must be set BEFORE qt_add_qml_module() because the macro computes
# the resource deploy paths while it parses QML_FILES.
set_source_files_properties(qml/Main.qml
    PROPERTIES QT_RESOURCE_ALIAS "Main.qml"
)

qt_add_qml_module(audio-assistant-qml
    URI "AudioAssistant"
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/AudioAssistant
    QML_FILES
        qml/Main.qml
)

# ── Main executable ─────────────────────────────────────────
add_executable(audio-assistant
    src/main.cpp
)

set_target_properties(audio-assistant PROPERTIES
    CXX_EXTENSIONS OFF
)

target_compile_options(audio-assistant PRIVATE
    -std=c++20
    -Wall -Wextra -Wpedantic -Werror
    -Weverything
    # Qt and moc-generated code use C++98-incompatible constructs (nullptr,
    # constinit, etc.); these diagnostics are not applicable to C++20/Qt.
    -Wno-c++98-compat
    -Wno-c++98-compat-pedantic
    -Wno-c++20-compat
    -Wno-unknown-warning-option
    -Wconversion -Wsign-conversion -Wimplicit-fallthrough
    -Wcast-align -Wcast-qual -Wdouble-promotion -Wformat=2
    -Wnull-dereference -Woverloaded-virtual -Wshadow-all
    -Wswitch-enum -Wundef -Wunreachable-code -Wunused
)

target_link_libraries(audio-assistant PRIVATE
    audio-assistant-qml
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::QuickControls2
)

# ── Tests ───────────────────────────────────────────────────
enable_testing()

qt_add_executable(tst_main
    tests/tst_main.cpp
)

set_target_properties(tst_main PROPERTIES
    CXX_EXTENSIONS OFF
)

target_compile_options(tst_main PRIVATE
    -std=c++20
    -Wall -Wextra -Wpedantic -Werror
    -Weverything
    # Qt and moc-generated code use C++98-incompatible constructs (nullptr,
    # constinit, etc.); these diagnostics are not applicable to C++20/Qt.
    -Wno-c++98-compat
    -Wno-c++98-compat-pedantic
    -Wno-c++20-compat
    -Wno-unknown-warning-option
    -Wconversion -Wsign-conversion -Wimplicit-fallthrough
    -Wcast-align -Wcast-qual -Wdouble-promotion -Wformat=2
    -Wnull-dereference -Woverloaded-virtual -Wshadow-all
    -Wswitch-enum -Wundef -Wunreachable-code -Wunused
)

target_link_libraries(tst_main PRIVATE
    audio-assistant-qml
    Qt6::Gui
    Qt6::Qml
    Qt6::Test
)

add_test(NAME tst_main COMMAND tst_main)
set_tests_properties(tst_main PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
