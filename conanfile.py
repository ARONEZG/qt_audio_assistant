"""Conan recipe for the Audio Assistant project.

The Qt 6.4.2 requirement is currently disabled (see ``requires`` below): on
Linux, Qt is taken from the system (apt/libqt6*-dev) because ConanCenter lacks
pre-built binaries for Linux/Clang. Uncomment it once pre-built binaries are
available, or for Windows/macOS builds.

spdlog is the managed runtime dependency (logging).

Usage (from the project root, Linux with Clang 18 and Ninja):

    # 1. Install dependencies using the in-repo profile
    #    (profiles/clang18; spdlog is fetched from ConanCenter)
    conan install . -of build/conan --build=missing -pr profiles/clang18

    # 2. Configure CMake with the Conan-generated toolchain.
    #    The toolchain is included after project(), so the compiler must be
    #    selected explicitly (the profile's settings are not enough).
    CC=clang-18 CXX=clang++-18 cmake -S . -B build \
        -DCONAN_BUILD_DIR=build/conan \
        -DCMAKE_BUILD_TYPE=Debug \
        -G Ninja

    # 3. Build and test
    cmake --build build
    ctest --test-dir build --output-on-failure

    # Without Conan (system Qt):
    CC=clang-18 CXX=clang++-18 cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -G Ninja
    cmake --build build
"""

from conan import ConanFile
from conan.tools.cmake import cmake_layout, CMakeDeps, CMakeToolchain


class AudioAssistantConan(ConanFile):
    name = "audio-assistant"
    # Must stay in sync with project(VERSION ...) in CMakeLists.txt.
    version = "0.1.0"

    description = "Audio Assistant — MVP build skeleton"
    topics = "audio", "assistant", "qt"
    url = "https://github.com/ARONEZG/qt_audio_assistant"
    # License is deliberately not declared: there is no LICENSE file yet.
    # Set this once licensing is decided.
    # license = "Proprietary"

    settings = "os", "arch", "compiler", "build_type"

    # ── Qt dependency ───────────────────────────────────────────────
    # On Linux Qt is provided by the system (apt/libqt6*-dev packages).
    # Uncomment the line below once pre-built binaries for your platform
    # are available in ConanCenter, or on Windows/macOS:
    #
    #   requires = "qt/6.4.2"
    #
    # Qt 6.4.2 options for reference:
    # default_options = {
    #     "qt/*:shared": False,
    #     "qt/*:opengl": "desktop",
    #     "qt/*:gui": True,
    #     "qt/*:qtdeclarative": True,
    #     "qt/*:qtquickcontrols2": True,
    #     "qt/*:openssl": True,
    #     "qt/*:with_pcre2": True,
    #     "qt/*:with_icu": True,
    #     "qt/*:with_harfbuzz": True,
    #     "qt/*:with_fontconfig": True,
    #     "qt/*:with_freetype": True,
    #     "qt/*:with_doubleconversion": True,
    #     "qt/*:with_dbus": True,
    # }
    # Managed runtime dependency: spdlog (logging). Qt is taken from the system
    # on Linux (see note above); add "qt/6.4.2" here for Windows/macOS builds.
    requires = "spdlog/1.14.1"

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self)
        tc.generate()
