cmake_minimum_required(VERSION 3.26)

project(audio-assistant
    VERSION 0.1.0
    DESCRIPTION "Audio Assistant — minimal build skeleton"
    LANGUAGES CXX
)

# ── Conan dependency support ────────────────────────────────
# If CONAN_BUILD_DIR is set, load the Conan-generated toolchain
# and prepend its module/prefix paths so find_package(Qt6 …) works.
#
# Conan 2's cmake_layout() places the toolchain at a generator-
# specific path. For single-config generators (Ninja, Makefiles) it is:
#   <CONAN_BUILD_DIR>/build/<CMAKE_BUILD_TYPE>/generators/conan_toolchain.cmake
# For multi-config generators (Visual Studio, Xcode, Ninja Multi-Config) it is:
#   <CONAN_BUILD_DIR>/build/generators/conan_toolchain.cmake
# (no configuration subdirectory).
#
# This script probes those locations plus the flat fallback
#   <CONAN_BUILD_DIR>/conan_toolchain.cmake
#
# Only Linux/Ninja has been verified (AGENTS.md §2.1); multi-config
# paths are probed based on the documented cmake_layout() behaviour.
#
# Usage (from the project root):
#   conan install . -of build/conan --build=missing -pr profiles/clang18
#   cmake -S . -B build -DCONAN_BUILD_DIR=build/conan -DCMAKE_BUILD_TYPE=Debug -G Ninja
if(DEFINED CONAN_BUILD_DIR)
    set(_conan_dir "${CONAN_BUILD_DIR}")

    # Candidate toolchain paths, most specific first.
    set(_conan_toolchain_candidates)
    if(CMAKE_BUILD_TYPE)
        list(APPEND _conan_toolchain_candidates
            "${_conan_dir}/build/${CMAKE_BUILD_TYPE}/generators/conan_toolchain.cmake")
    endif()
    # Multi-config layout (no configuration subdirectory).
    list(APPEND _conan_toolchain_candidates
        "${_conan_dir}/build/generators/conan_toolchain.cmake")
    # Fallback to the toolchain of a previously installed build_type —
    # the Conan install may have been run with a different CMAKE_BUILD_TYPE.
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND _conan_toolchain_candidates
            "${_conan_dir}/build/Debug/generators/conan_toolchain.cmake")
    endif()
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        list(APPEND _conan_toolchain_candidates
            "${_conan_dir}/build/Release/generators/conan_toolchain.cmake")
    endif()
    list(APPEND _conan_toolchain_candidates "${_conan_dir}/conan_toolchain.cmake")

    set(_conan_toolchain)
    foreach(_candidate IN LISTS _conan_toolchain_candidates)
        if(EXISTS "${_candidate}")
            set(_conan_toolchain "${_candidate}")
            break()
        endif()
    endforeach()

    if(NOT _conan_toolchain)
        message(FATAL_ERROR
            "Conan toolchain not found.\n"
            "Searched:\n"
            "  ${_conan_toolchain_candidates}\n"
            "Run:\n"
            "  conan install . -of ${_conan_dir} --build=missing -pr profiles/clang18\n"
        )
    endif()
    message(STATUS "Loading Conan toolchain: ${_conan_toolchain}")

    # Prepend generators directory so CMakeDeps-generated files are found
    get_filename_component(_conan_gen "${_conan_toolchain}" DIRECTORY)
    list(PREPEND CMAKE_MODULE_PATH "${_conan_gen}")
    list(PREPEND CMAKE_PREFIX_PATH "${_conan_gen}")

    include("${_conan_toolchain}")
endif()

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
# Qt 6.8.3 is the single source of truth for the Qt version. It is provided by
# aqtinstall (see conanfile.py), not by the system packages; point CMake at it
# with -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64 (the path is deliberately not
# hardcoded here).
find_package(Qt6 6.8.3 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
)

find_package(Qt6 REQUIRED COMPONENTS
    Test
)

# ── spdlog logging ───────────────────────────────────────────
# spdlog is a managed Conan dependency (spdlog/1.14.1 in conanfile.py); the exact
# version comes from the Conan toolchain loaded above. The minimum is pinned here
# so that configuring against an older system spdlog fails immediately instead of
# silently building with a different version.
find_package(spdlog 1.14 REQUIRED)

# ── LLMQore (LLM client library) ────────────────────────────
# Pinned to the released tag v0.8.0 (MIT). Fetched as a SYSTEM subproject so
# its headers are consumed as system headers and its own warning settings do
# not leak into project targets. Only build-time auxiliary targets are forced
# off; the library itself is linked PRIVATE into the executable.
include(FetchContent)
set(LLMQORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLMQORE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLMQORE_BUILD_MCP_BRIDGE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(LLMQore
    SYSTEM
    GIT_REPOSITORY https://github.com/Palm1r/llmqore.git
    GIT_TAG v0.8.0
)
FetchContent_MakeAvailable(LLMQore)

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
    src/logging/LogBridge.cpp
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
    spdlog::spdlog
    # Linked to prove the FetchContent integration builds and links end-to-end.
    # No LLMQore header is included yet; usage code arrives with the feature task.
    LLMQore::LLMQore
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

# ── Logging tests ───────────────────────────────────────────
qt_add_executable(tst_logging
    tests/tst_logging.cpp
    src/logging/LogBridge.cpp
)

set_target_properties(tst_logging PROPERTIES
    CXX_EXTENSIONS OFF
)

target_compile_options(tst_logging PRIVATE
    -std=c++20
    -Wall -Wextra -Wpedantic -Werror
    -Weverything
    -Wno-c++98-compat
    -Wno-c++98-compat-pedantic
    -Wno-c++20-compat
    -Wno-unknown-warning-option
    -Wno-padded  # moc-generated meta code may have padding on structs
    -Wconversion -Wsign-conversion -Wimplicit-fallthrough
    -Wcast-align -Wcast-qual -Wdouble-promotion -Wformat=2
    -Wnull-dereference -Woverloaded-virtual -Wshadow-all
    -Wswitch-enum -Wundef -Wunreachable-code -Wunused
)

target_include_directories(tst_logging PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(tst_logging PRIVATE
    Qt6::Core
    Qt6::Test
    spdlog::spdlog
)

add_test(NAME tst_logging COMMAND tst_logging)
set_tests_properties(tst_logging PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
