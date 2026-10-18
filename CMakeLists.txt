cmake_minimum_required(VERSION 3.26)

project(audio-assistant
    VERSION 0.1.0
    DESCRIPTION "Audio Assistant — minimal build skeleton"
    LANGUAGES CXX
)

# ── Qt discovery for IDE presets ────────────────────────────
# Conan-generated CMakePresets.json is used from VS Code's CMake Tools,
# but Conan does not provide Qt on Linux (aqtinstall installs it into
# ~/Qt). Presets cannot pass CMAKE_PREFIX_PATH portably, so the default
# Qt 6.8.3 location is probed here and prepended — the Conan toolchain
# paths keep priority for Conan packages (spdlog), while find_package(Qt6)
# resolves to the aqtinstall prefix. -DCMAKE_PREFIX_PATH still wins for
# other entries; a duplicate Qt entry is harmless.
set(_qt_prefix_candidates
    "$ENV{HOME}/Qt/6.8.3/gcc_64"
    "/opt/Qt/6.8.3/gcc_64"
)
foreach(_qt_candidate IN LISTS _qt_prefix_candidates)
    if(IS_DIRECTORY "${_qt_candidate}")
        list(PREPEND CMAKE_PREFIX_PATH "${_qt_candidate}")
        message(STATUS "Qt prefix: ${_qt_candidate}")
        break()
    endif()
endforeach()

# ── Conan dependency support ────────────────────────────────
# If CONAN_BUILD_DIR is set, load the Conan-generated toolchain
# and prepend its module/prefix paths so find_package(spdlog …) works.
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
# If no toolchain is present, `conan install` is run automatically for the
# current build type, so IDE builds (VS Code CMake Tools "Build" button)
# work on a fresh clone without a manual Conan step. CI and
# scripts/ci-check.sh run `conan install` themselves first, so this branch
# is a no-op for them.
#
# Usage (from the project root):
#   conan install . -of build/conan --build=missing -pr profiles/clang18
#   cmake -S . -B build -DCONAN_BUILD_DIR=build/conan -DCMAKE_BUILD_TYPE=Debug -G Ninja
function(_audio_assistant_find_conan_toolchain conan_dir result_var)
    set(_candidates)
    if(CMAKE_BUILD_TYPE)
        list(APPEND _candidates
            "${conan_dir}/build/${CMAKE_BUILD_TYPE}/generators/conan_toolchain.cmake")
    endif()
    # Multi-config layout (no configuration subdirectory).
    list(APPEND _candidates
        "${conan_dir}/build/generators/conan_toolchain.cmake")
    # Fallback to the toolchain of a previously installed build_type —
    # the Conan install may have been run with a different CMAKE_BUILD_TYPE.
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND _candidates
            "${conan_dir}/build/Debug/generators/conan_toolchain.cmake")
    endif()
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        list(APPEND _candidates
            "${conan_dir}/build/Release/generators/conan_toolchain.cmake")
    endif()
    list(APPEND _candidates "${conan_dir}/conan_toolchain.cmake")

    set(_found)
    foreach(_candidate IN LISTS _candidates)
        if(EXISTS "${_candidate}")
            set(_found "${_candidate}")
            break()
        endif()
    endforeach()
    set(${result_var} "${_found}" PARENT_SCOPE)
endfunction()

if(DEFINED CONAN_BUILD_DIR)
    set(_conan_dir "${CONAN_BUILD_DIR}")

    _audio_assistant_find_conan_toolchain("${_conan_dir}" _conan_toolchain)

    if(NOT _conan_toolchain)
        # The managed dependencies (spdlog, miniaudio, llmqore) are not installed
        # for this build type yet. Install them automatically so one-click IDE
        # builds work.
        find_program(_conan_exe conan)
        if(NOT _conan_exe)
            message(FATAL_ERROR
                "Conan toolchain not found and 'conan' is not in PATH.\n"
                "Searched under: ${_conan_dir}\n"
                "Install Conan and run:\n"
                "  conan install . -of ${_conan_dir} --build=missing -pr profiles/clang18\n"
            )
        endif()

        set(_conan_build_type "${CMAKE_BUILD_TYPE}")
        if(NOT _conan_build_type)
            set(_conan_build_type "Debug")
        endif()

        # The in-repo LLMQore recipe must be in the Conan cache before the
        # package can be resolved/built; --build=missing below then produces it.
        message(STATUS "Conan export recipes/llmqore (automatic)")
        execute_process(
            COMMAND "${_conan_exe}" export "${CMAKE_CURRENT_SOURCE_DIR}/recipes/llmqore"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE _conan_export_result
            OUTPUT_VARIABLE _conan_export_out
            ERROR_VARIABLE _conan_export_err
        )
        if(NOT _conan_export_result EQUAL 0)
            message(FATAL_ERROR
                "Automatic 'conan export recipes/llmqore' failed.\n"
                "${_conan_export_out}\n${_conan_export_err}"
            )
        endif()

        message(STATUS "Conan install (automatic, build_type=${_conan_build_type})")
        execute_process(
            COMMAND "${_conan_exe}" install .
                -of "${_conan_dir}"
                --build=missing
                -pr:b "${CMAKE_CURRENT_SOURCE_DIR}/profiles/clang18"
                -pr "${CMAKE_CURRENT_SOURCE_DIR}/profiles/clang18"
                -s:b build_type=${_conan_build_type}
                -s build_type=${_conan_build_type}
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE _conan_install_result
            OUTPUT_VARIABLE _conan_install_out
            ERROR_VARIABLE _conan_install_err
        )
        if(NOT _conan_install_result EQUAL 0)
            message(FATAL_ERROR
                "Automatic 'conan install' failed.\n"
                "${_conan_install_out}\n${_conan_install_err}"
            )
        endif()

        _audio_assistant_find_conan_toolchain("${_conan_dir}" _conan_toolchain)
    endif()

    if(NOT _conan_toolchain)
        message(FATAL_ERROR
            "Conan toolchain not found.\n"
            "Searched under: ${_conan_dir}\n"
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
# Network and Concurrent are required by LLMQore (Qt Networking / CONCURRENT).
find_package(Qt6 6.8.3 REQUIRED COMPONENTS
    Core
    Gui
    Network
    Concurrent
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

# ── Strict warning profile (shared by all project-owned targets) ──
# The zero-warning profile from src/AGENTS.md lives in one INTERFACE target so it
# is defined once and applied per target instead of being copy-pasted.
add_library(audio-assistant-strict INTERFACE)

target_compile_options(audio-assistant-strict INTERFACE
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

# Qt/QObject-specific suppressions, scoped to the targets that actually pull in
# Qt value types and moc-generated signal dispatchers (the domain library and
# its tests) instead of being applied to every project target.
add_library(audio-assistant-qt-warnings INTERFACE)

target_compile_options(audio-assistant-qt-warnings INTERFACE
    # Qt value types are pointer-aligned, so domain value objects carry internal
    # tail padding; the struct layout is not a public ABI contract here.
    -Wno-padded
    # moc-generated dispatchers index the signal-argument array directly; this
    # pattern lives only in generated code and cannot be rewritten by hand.
    -Wno-unsafe-buffer-usage
    # Qt generates resource initializers and QML cache code with static
    # constructors/destructors (qrc init, qmlcachegen output); -Weverything
    # flags those generated constructs, which cannot be rewritten by hand.
    -Wno-global-constructors
    -Wno-exit-time-destructors
    # Generated registration/resource entry points are free functions without a
    # header prototype, and the QML compiler emits empty statements.
    -Wno-missing-prototypes
    -Wno-extra-semi-stmt
)

# ── LLMQore (LLM client library) ────────────────────────────
# LLMQore is a managed Conan dependency (llmqore/0.8.0, recipe in
# recipes/llmqore) — the single source of truth for its version. Conan fetches,
# builds and packages it; the CMakeDeps-generated config exposes the upstream
# LLMQore::LLMQore target in the Conan generators directory. The library is
# linked PRIVATE into the executable.
find_package(LLMQore REQUIRED)

# ── miniaudio (audio capture) ───────────────────────────────
# miniaudio is a managed Conan dependency (miniaudio/0.11.25 in conanfile.py) —
# the single source of truth for its version. It is a third-party, header-only
# C library (miniaudio.h) that provides microphone capture on Windows/Linux/
# macOS and native loopback capture of the system output on Windows; on Linux
# the system output is captured through a PulseAudio/PipeWire-Pulse monitor
# source. macOS loopback is a documented gap (docs/ADR-audio-capture.md).
find_package(miniaudio REQUIRED)

# miniaudio.h is third-party code. CMakeDeps exports its include directory via
# INTERFACE_INCLUDE_DIRECTORIES (a plain -I); repeating the same paths in
# INTERFACE_SYSTEM_INCLUDE_DIRECTORIES makes CMake emit them as -isystem so the
# project's -Weverything -Werror profile does not flag the vendored header.
set_target_properties(miniaudio::miniaudio PROPERTIES
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${miniaudio_INCLUDE_DIRS}"
)

# ── Audio library ───────────────────────────────────────────
# Thin wrapper that owns the miniaudio implementation translation unit, so the
# third-party amalgamation is compiled once and the future platform-specific
# capture code has a single home behind a narrow interface. It contains no
# capture logic yet — only the implementation compile unit.
add_library(audio-assistant-audio STATIC
    src/audio/MiniaudioImplementation.cpp
)

set_target_properties(audio-assistant-audio PROPERTIES
    CXX_EXTENSIONS OFF
)

target_link_libraries(audio-assistant-audio
    PRIVATE audio-assistant-strict miniaudio::miniaudio
)

# ── Domain library ──────────────────────────────────────────
# Chat domain (immutable values, provider role interface, collections). Built as
# a static library so the executable and every test target link the same
# implementation, and used as the backing target of the AudioAssistant QML
# module below (its types are the QML view models).
add_library(audio-assistant-domain STATIC
    src/domain/Answer.cpp
    src/domain/Chat.cpp
    src/domain/Chats.cpp
    src/domain/Message.cpp
    src/domain/NewChat.cpp
    src/domain/Provider.cpp
    src/domain/ProviderOffer.cpp
    src/domain/Providers.cpp
)

set_target_properties(audio-assistant-domain PROPERTIES
    CXX_EXTENSIONS OFF
)

# Consumers include domain headers as "domain/<Header>.h".
target_include_directories(audio-assistant-domain PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

# qmltyperegistrar generates `#if __has_include(<Header.h>)` for the types it
# registers (it only keeps the header's file name), so the headers must also be
# reachable by their bare name while the module's registration code compiles.
target_include_directories(audio-assistant-domain PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/domain
)

# Qt6::Qml is public because the domain types carry the QML registration macros
# (QML_ELEMENT) in their headers; every consumer of the headers needs the Qml
# include path. The types are the view models, so there is no separate adapter.
target_link_libraries(audio-assistant-domain
    PUBLIC Qt6::Core Qt6::Qml
    PRIVATE audio-assistant-strict audio-assistant-qt-warnings
)

# ── QML module ──────────────────────────────────────────────
# The domain library is the backing target of the AudioAssistant QML module: the
# C++ types it exposes (Chat, Chats, Providers, Message) are the view models, so
# a view binds to them directly and no adapter library is needed. QML files are
# placed at the module root; a resource alias per file keeps them out of a qml/
# subdirectory (the default would deploy them under /qt/qml/AudioAssistant/qml/).
set(_audio_assistant_qml_files
    qml/Main.qml
    qml/Theme.qml
    qml/MessageDelegate.qml
    qml/Composer.qml
    qml/ConversationView.qml
    qml/ChatPanel.qml
    qml/ProviderSelector.qml
    qml/ChatGrid.qml
)

foreach(_qml_file IN LISTS _audio_assistant_qml_files)
    get_filename_component(_qml_name "${_qml_file}" NAME)
    set_source_files_properties("${_qml_file}"
        PROPERTIES QT_RESOURCE_ALIAS "${_qml_name}"
    )
endforeach()

# Theme.qml is a `pragma Singleton`; qt_add_qml_module needs the file property
# (the pragma alone is not enough) so the generated qmldir marks it a singleton.
set_source_files_properties(qml/Theme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(audio-assistant-domain
    URI "AudioAssistant"
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/AudioAssistant
    QML_FILES ${_audio_assistant_qml_files}
)

# ── Main executable ─────────────────────────────────────────
add_executable(audio-assistant
    src/app/OpenAiCompatibleProvider.cpp
    src/app/PlaceholderProvider.cpp
    src/main.cpp
    src/logging/LogBridge.cpp
)

set_target_properties(audio-assistant PROPERTIES
    CXX_EXTENSIONS OFF
)

target_link_libraries(audio-assistant PRIVATE
    audio-assistant-strict
    audio-assistant-qt-warnings
    audio-assistant-audio
    audio-assistant-domain
    # For a static QML module the generated plugin must be linked into the
    # executable, otherwise the engine cannot register the module's C++ types.
    audio-assistant-domainplugin
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::QuickControls2
    spdlog::spdlog
    LLMQore::LLMQore
    # LLMQore is a static library that links Qt Network publicly and Qt
    # Concurrent privately. Qt is not consumed through Conan on Linux (see
    # conanfile.py), so the ConanDeps target cannot carry those link
    # dependencies and they are attached to the consumer explicitly.
    Qt6::Network
    Qt6::Concurrent
)

# ── Tests ───────────────────────────────────────────────────
enable_testing()

# Registers a Qt Test target against the domain library and wires it into CTest.
# GUI/QML tests use their own target below (they need the QML module).
function(audio_assistant_add_domain_test target)
    qt_add_executable(${target} ${ARGN})

    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
    )

    target_link_libraries(${target} PRIVATE
        audio-assistant-strict
        audio-assistant-qt-warnings
        audio-assistant-domain
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

audio_assistant_add_domain_test(tst_message
    tests/tst_message.cpp
)
audio_assistant_add_domain_test(tst_answer
    tests/tst_answer.cpp
)
audio_assistant_add_domain_test(tst_chat
    tests/tst_chat.cpp
    tests/FakeProvider.cpp
    tests/FakeProvider.h
)
audio_assistant_add_domain_test(tst_providers
    tests/tst_providers.cpp
    tests/FakeProvider.cpp
    tests/FakeProvider.h
)
audio_assistant_add_domain_test(tst_chats
    tests/tst_chats.cpp
    tests/FakeProvider.cpp
    tests/FakeProvider.h
)
audio_assistant_add_domain_test(tst_newchat
    tests/tst_newchat.cpp
    tests/FakeProvider.cpp
    tests/FakeProvider.h
)
audio_assistant_add_domain_test(tst_placeholderprovider
    tests/tst_placeholderprovider.cpp
    src/app/PlaceholderProvider.cpp
)

# ── Network provider test ───────────────────────────────────
# Exercises the OpenAI-compatible adapter through a local mock HTTP server
# (tests/OpenAiMockServer) instead of the real network: streaming SSE, HTTP
# errors, malformed frames, an interrupted stream, cancellation and timeout.
# It links LLMQore (the client the adapter wraps) and therefore Qt Network.
qt_add_executable(tst_openai_provider
    tests/tst_openai_provider.cpp
    tests/OpenAiMockServer.cpp
    tests/OpenAiMockServer.h
    src/app/OpenAiCompatibleProvider.cpp
)

set_target_properties(tst_openai_provider PROPERTIES
    CXX_EXTENSIONS OFF
)

target_link_libraries(tst_openai_provider PRIVATE
    audio-assistant-strict
    audio-assistant-qt-warnings
    audio-assistant-domain
    Qt6::Core
    Qt6::Network
    Qt6::Concurrent
    Qt6::Test
    LLMQore::LLMQore
)

add_test(NAME tst_openai_provider COMMAND tst_openai_provider)
set_tests_properties(tst_openai_provider PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# ── Audio smoke test ────────────────────────────────────────
# Proves the miniaudio dependency configures, compiles and links (see the test
# file header for the exact scope) without touching a real audio device.
qt_add_executable(tst_audio_smoke
    tests/tst_audio_smoke.cpp
)

set_target_properties(tst_audio_smoke PROPERTIES
    CXX_EXTENSIONS OFF
)

# miniaudio::miniaudio is linked explicitly so the test's own <miniaudio.h>
# include is resolved as SYSTEM, exactly like the project targets; the
# implementation symbols come from audio-assistant-audio.
target_link_libraries(tst_audio_smoke PRIVATE
    audio-assistant-strict
    audio-assistant-audio
    miniaudio::miniaudio
    Qt6::Core
    Qt6::Test
)

add_test(NAME tst_audio_smoke COMMAND tst_audio_smoke)
set_tests_properties(tst_audio_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# GUI/QML tests: they load the real QML module offscreen and drive it with a
# fake provider, so they link the module (and its generated plugin) plus Quick.
function(audio_assistant_add_qml_test target)
    qt_add_executable(${target} ${ARGN})

    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
    )

    target_link_libraries(${target} PRIVATE
        audio-assistant-strict
        audio-assistant-qt-warnings
        audio-assistant-domain
        audio-assistant-domainplugin
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Test
    )

    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

audio_assistant_add_qml_test(tst_main
    tests/tst_main.cpp
    tests/FakeProvider.cpp
    tests/FakeProvider.h
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
