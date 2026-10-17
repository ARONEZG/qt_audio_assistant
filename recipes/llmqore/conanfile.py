"""Conan recipe for LLMQore — the Qt/C++ LLM client library.

LLMQore is not published on ConanCenter and its upstream repository ships no
Conan recipe, so this in-repository recipe is the single source of truth for
the dependency. It pins the upstream git tag, builds the library and exposes
the upstream ``LLMQore::LLMQore`` CMake target to the consumer.

Qt discovery: ConanCenter has no pre-built Qt binaries for Linux/Clang, so the
project consumes Qt from the per-user aqtinstall tree (see ``conanfile.py`` in
the repository root) instead of through Conan. This recipe therefore resolves
the Qt prefix from the ``QT_PREFIX`` environment variable and otherwise probes
the same default locations as the project's ``CMakeLists.txt`` (the aqtinstall
layout ``$HOME/Qt/<version>/gcc_64`` and ``/opt/Qt/<version>/gcc_64``).

The project's build scripts run ``conan export`` for this recipe before every
``conan install`` (see ``scripts/ci-check.sh`` and ``.github/workflows/ci.yml``);
the binary is then produced by ``--build=missing`` for the requested
``build_type``. Manual equivalent:

    conan export recipes/llmqore
    conan install . -of build/conan --build=missing \
        -pr:b profiles/clang18 -pr profiles/clang18
"""

import os

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.scm import Git

# Pinned upstream source: the repository is the single source of truth for the
# version consumed by the project (previously a FetchContent GIT_TAG).
_LLMQORE_GIT_URL = "https://github.com/Palm1r/llmqore.git"
_LLMQORE_GIT_TAG = "v0.8.0"

# Keep in sync with the Qt version searched for in the project's CMakeLists.txt.
_DEFAULT_QT_VERSION = "6.8.3"


def _resolve_qt_prefix():
    """Returns the Qt prefix used to build LLMQore, or None if not found.

    ``QT_PREFIX`` wins so a machine with Qt in a non-default location can still
    build the package; the fallbacks mirror the probe in ``CMakeLists.txt``.
    """
    from_env = os.environ.get("QT_PREFIX")
    if from_env:
        return from_env

    candidates = (
        os.path.join(os.path.expanduser("~"), "Qt", _DEFAULT_QT_VERSION, "gcc_64"),
        os.path.join("/opt/Qt", _DEFAULT_QT_VERSION, "gcc_64"),
    )
    for candidate in candidates:
        if os.path.isdir(candidate):
            return candidate
    return None


class LLMQoreConan(ConanFile):
    name = "llmqore"
    version = "0.8.0"
    description = "Qt/C++ library for LLM provider integration"
    topics = ("llm", "qt", "client")
    url = "https://github.com/Palm1r/llmqore"
    license = "MIT"

    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

    def layout(self):
        cmake_layout(self)

    def source(self):
        git = Git(self)
        git.clone(
            url=_LLMQORE_GIT_URL,
            target=".",
            args=["--depth", "1", "--branch", _LLMQORE_GIT_TAG],
        )

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["BUILD_SHARED_LIBS"] = self.options.shared
        # Only the library is needed by the consumer; the upstream tests,
        # examples and MCP bridge are development targets.
        tc.variables["LLMQORE_BUILD_TESTS"] = False
        tc.variables["LLMQORE_BUILD_EXAMPLES"] = False
        tc.variables["LLMQORE_BUILD_MCP_BRIDGE"] = False
        tc.variables["LLMQORE_INSTALL"] = True

        qt_prefix = _resolve_qt_prefix()
        if qt_prefix:
            tc.cache_variables["CMAKE_PREFIX_PATH"] = qt_prefix
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "LLMQore")
        self.cpp_info.set_property("cmake_target_name", "LLMQore::LLMQore")
        self.cpp_info.libs = ["LLMQore"]
        if not self.options.shared:
            # Upstream defines this PUBLICly for static builds so headers export
            # symbols correctly; the ConanDeps target must carry it too.
            self.cpp_info.defines = ["LLMQORE_STATIC_LIB"]
