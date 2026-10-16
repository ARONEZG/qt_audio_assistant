"""Conan recipe for the Audio Assistant project.

Qt 6.4.2 is the primary dependency.

On Linux, Qt is used from the system (apt/libqt6*-dev) because ConanCenter
currently lacks pre-built binaries for Linux/Clang. When pre-built binaries
become available or for Windows/macOS builds, uncomment `requires`.

Usage:

    # Install dependencies (currently only build-time tools)
    conan install . -of build/conan --build=missing -pr clang18

    # Configure CMake with Conan toolchain
    cmake -S . -B build \
        -DCONAN_BUILD_DIR=build/conan \
        -DCMAKE_BUILD_TYPE=Debug \
        -G Ninja

    # Build
    cmake --build build

    # Without Conan (system Qt):
    CC=clang-18 CXX=clang++-18 cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -G Ninja
    cmake --build build
"""

from conan import ConanFile
from conan.tools.cmake import cmake_layout, CMakeDeps, CMakeToolchain


class AudioAssistantConan(ConanFile):
    name = "audio-assistant"
    version = "0.1.0"

    description = "Audio Assistant — MVP build skeleton"
    topics = "audio", "assistant", "qt"
    url = "https://github.com/stc-arduino/audio-assistant"
    license = "Proprietary"

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
    requires = ()

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self)
        tc.generate()