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
# Qt 6.4.2 is the single source of truth for the Qt version; it must stay in
# sync with `requires = "qt/6.4.2"` in conanfile.py.
find_package(Qt6 6.4.2 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
)

find_package(Qt6 REQUIRED COMPONENTS
    Test
)

# ── Strict warning profile (zero-warning policy, AGENTS.md §3) ──
# Один INTERFACE-таргет вместо дублирования флагов в каждой цели.
add_library(audio-assistant-strict-warnings INTERFACE)
target_compile_options(audio-assistant-strict-warnings INTERFACE
    -std=c++20
    -Wall -Wextra -Wpedantic -Werror
    -Weverything
    # Qt и moc-генерированный код используют конструкции, несовместимые с
    # C++98 (nullptr, constinit и т. п.); эти диагностики неприменимы к C++20/Qt.
    -Wno-c++98-compat
    -Wno-c++98-compat-pedantic
    -Wno-c++20-compat
    -Wno-unknown-warning-option
    # Clang 18: экспериментальная -Wunsafe-buffer-usage из -Weverything срабатывает
    # на moc-сгенерированном коде (reinterpret_cast над void** для аргументов
    # слотов), который создаёт moc и нельзя пометить как системный. Собственный
    # код проекта буферных конструкций не содержит; диагностика остаётся в силе
    # для clang-tidy (bugprone-* / clang-analyzer-*) на наших TU.
    -Wno-unsafe-buffer-usage
    # -Wpadded из -Weverything срабатывает на выравнивающих вставках в структурах,
    # генерируемых moc для метаданных Q_TEST (qt_meta_stringdata_*: массив
    # QByteArrayData + строковые данные). Это упаковка, специфичная для
    # сгенерированного Qt-кода, а не собственного; padding собственных структур
    # остаётся в зоне ответственности clang-tidy (clang-analyzer/performance).
    -Wno-padded
    -Wconversion -Wsign-conversion -Wimplicit-fallthrough
    -Wcast-align -Wcast-qual -Wdouble-promotion -Wformat=2
    -Wnull-dereference -Woverloaded-virtual -Wshadow-all
    -Wswitch-enum -Wundef -Wunreachable-code -Wunused
)

# ── AppController library ───────────────────────────────────
add_library(appcontroller STATIC
    src/AppController.cpp
)

set_target_properties(appcontroller PROPERTIES
    CXX_EXTENSIONS OFF
    AUTOMOC ON
)

target_compile_options(appcontroller PRIVATE
    $<TARGET_PROPERTY:audio-assistant-strict-warnings,INTERFACE_COMPILE_OPTIONS>
)

target_include_directories(appcontroller PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(appcontroller PUBLIC
    Qt6::Core
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
    $<TARGET_PROPERTY:audio-assistant-strict-warnings,INTERFACE_COMPILE_OPTIONS>
)

target_link_libraries(audio-assistant PRIVATE
    appcontroller
    audio-assistant-qml
    Qt6::Gui
    Qt6::Qml
    Qt6::QuickControls2
)

# ── Tests ───────────────────────────────────────────────────
enable_testing()

function(audio_assistant_add_test target sources)
    qt_add_executable(${target} ${sources})

    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
    )

    target_compile_options(${target} PRIVATE
        $<TARGET_PROPERTY:audio-assistant-strict-warnings,INTERFACE_COMPILE_OPTIONS>
    )

    target_link_libraries(${target} PRIVATE
        ${ARGN}
        Qt6::Test
    )

    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

audio_assistant_add_test(tst_main
    tests/tst_main.cpp
    audio-assistant-qml
    Qt6::Gui
    Qt6::Qml
)

audio_assistant_add_test(tst_appcontroller
    tests/tst_appcontroller.cpp
    appcontroller
)
