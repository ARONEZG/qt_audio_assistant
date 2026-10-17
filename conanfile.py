"""Conan recipe for the Audio Assistant project.

Qt 6.8.3 is the primary dependency target, but Conan does not provide it here:
the Conan requirement stays disabled (see ``requires`` below) because
ConanCenter lacks pre-built Qt binaries for Linux/Clang. On Linux, Qt is
instead installed per-user with aqtinstall into ``~/Qt/6.8.3/gcc_64`` and
located by CMake via ``-DCMAKE_PREFIX_PATH``.

Usage (from the project root, Linux with Clang 18 and Ninja):

    # 0. One-time Qt install (no sudo, per-user, pinned version).
    #    In Qt 6 Qt Quick Controls 2 is part of the qtdeclarative archive,
    #    so there is no separate qtquickcontrols2 module/archive:
    pipx install aqtinstall
    aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O ~/Qt \
        --archives icu qtbase qtdeclarative qtsvg qttools qttranslations qtwayland

    # 1. Install build dependencies using the in-repo profile
    #    (profiles/clang18; currently only build-time tooling, no packages)
    conan install . -of build/conan --build=missing -pr profiles/clang18

    # 2. Configure CMake with the Conan-generated toolchain.
    #    The toolchain is included after project(), so the compiler must be
    #    selected explicitly (the profile's settings are not enough).
    CC=clang-18 CXX=clang++-18 cmake -S . -B build \
        -DCONAN_BUILD_DIR=build/conan \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64 \
        -G Ninja

    # 3. Build and test
    cmake --build build
    ctest --test-dir build --output-on-failure

    # Without Conan (aqtinstall Qt):
    CC=clang-18 CXX=clang++-18 cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64 -G Ninja
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
    # On Linux Qt 6.8.3 is installed per-user with aqtinstall into
    # ~/Qt/6.8.3/gcc_64 (see the module docstring) and located by CMake
    # through -DCMAKE_PREFIX_PATH; the system packages are no longer used.
    # Uncomment the line below once pre-built binaries for your platform
    # are available in ConanCenter, or on Windows/macOS:
    #
    #   requires = "qt/6.8.3"
    #
    # Qt 6.8.3 options for reference:
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
    # Currently no managed runtime dependencies; Conan only generates the
    # CMake toolchain/build files (build-time tooling).
    requires = ()

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self)
        tc.generate()
