"""Conan recipe for onnxruntime — the ONNX inference runtime.

The upstream ONNX Runtime is published on ConanCenter as
``onnxruntime/1.24.4``, but its 1.24.4 sources do not compile with the
project's strict Clang 18 toolchain: ``onnxruntime/core/common/cpuid_info.cc``
uses the GCC-specific ``__builtin_cpu_supports("waitpkg")``, which Clang
rejects with ``error: invalid cpu feature string for builtin``. The fix was
merged upstream after the release (microsoft/onnxruntime#27669), so there is no
fixed 1.24.x in ConanCenter.

This in-repository recipe therefore pins the exact same upstream source
tarball that the ConanCenter recipe uses (same version and same sha256 —
conandata.yml below is copied from ConanCenter) and applies one minimal
vendor patch: the upstream TPAUSE check is replaced with the compiler-portable
CPUID-bit check, exactly as in PR #27669. Everything else (dependencies,
CMake wiring, package layout, exported ``onnxruntime::onnxruntime`` target)
is identical to the ConanCenter recipe, so switching back to the upstream
recipe is a one-line change once a fixed version lands in ConanCenter.

The recipe must be exported into the local cache before ``conan install`` so it
shadows the ConanCenter recipe of the same reference (the project's build
scripts already run ``conan export recipes/llmqore`` before every install;
``recipes/onnxruntime`` is exported the same way):

    conan export recipes/onnxruntime
    conan install . -of build/conan --build=missing \
        -pr:b profiles/clang18 -pr profiles/clang18

The library is built from source with the project's Clang profile (the
pre-built Linux binaries in ConanCenter are GCC 13 / libstdc++11, which are
ABI-compatible with Clang's libstdc++ build, but consuming them would require
the whole dependency graph to be resolved with GCC settings; building from
source with the Clang profile keeps the "Clang is the only compiler" rule of
AGENTS.md intact). The build is CPU-only (no CUDA, no XNNPACK).
"""

import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.apple import is_apple_os
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import apply_conandata_patches, copy, export_conandata_patches, get, replace_in_file, rmdir

# Keep in sync with `requires` in the repository's conanfile.py and with the
# upstream version the ConanCenter recipe pins.
_ONNXRUNTIME_VERSION = "1.24.4"

required_conan_version = ">=2"


class OnnxRuntimeConan(ConanFile):
    name = "onnxruntime"
    version = _ONNXRUNTIME_VERSION
    description = ("ONNX Runtime: cross-platform, high performance ML inferencing "
                   "and training accelerator (in-repo recipe with Clang fix)")
    url = "https://github.com/microsoft/onnxruntime"
    license = "MIT"
    homepage = "https://onnxruntime.ai"
    topics = ("deep-learning", "onnx", "neural-networks", "machine-learning",
              "ai-framework", "hardware-acceleration")

    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "with_xnnpack": [True, False],
        "with_cuda": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "with_xnnpack": False,
        "with_cuda": False,
    }
    short_paths = True

    def export_sources(self):
        export_conandata_patches(self)
        copy(self, "cmake/*", src=self.recipe_folder, dst=self.export_sources_folder)

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self, src_folder="src")

    def requirements(self):
        # Identical to the ConanCenter recipe for 1.24.4 (conandata.yml and the
        # dependency set are copied from it), so the graphs stay interchangeable.
        self.requires("onnx/[>=1.20.1 <1.21]")
        self.requires("abseil/[>=20240116.1 <=20260107.1]")
        self.requires("protobuf/[>=3.21.12 <7]")
        self.requires("date/[>=3.0.1 <3.1]")
        self.requires("re2/[>=20231101]")
        self.requires("flatbuffers/23.5.26")
        self.requires("boost/[>=1.83.0 <1.90.0]", headers=True, libs=False)  # mp11, header only
        self.requires("safeint/3.0.28")
        self.requires("nlohmann_json/[>=3.11.3 <3.12]")
        self.requires("eigen/[>=5.0.1 <6]")
        self.requires("ms-gsl/4.0.0")
        if self.settings.os != "Windows":
            self.requires("nsync/1.26.0")
        else:
            self.requires("wil/1.0.240803.1")
        if self.options.with_xnnpack:
            self.requires("xnnpack/[>=cci.20241203]")
            self.requires("pthreadpool/cci.20231129")
        if self.options.with_cuda:
            self.requires("cutlass/3.5.0")
        self.requires("cpuinfo/[>=cci.20250110]")

    def validate(self):
        check_min_cppstd(self, 17)
        onnx = self.dependencies["onnx"]
        if not onnx.options.disable_static_registration:
            raise ConanInvalidConfiguration(
                f"{self.ref} requires onnx compiled with `-o onnx:disable_static_registration=True`."
            )
        if onnx.options.get_safe("shared"):
            raise ConanInvalidConfiguration(
                "There are link errors using 'onnx/*:shared=True',"
                " use '-o onnx/*:shared=False' instead."
            )

    def validate_build(self):
        if self.settings.os == "Windows" and self.dependencies["abseil"].options.shared:
            raise ConanInvalidConfiguration("Using abseil shared on Windows leads to link errors.")

    def build_requirements(self):
        self.tool_requires("cmake/[>=3.28]")

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)
        self._patch_sources()

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["FETCHCONTENT_FULLY_DISCONNECTED"] = True
        tc.variables["onnxruntime_BUILD_SHARED_LIB"] = self.options.shared
        tc.variables["onnxruntime_USE_FULL_PROTOBUF"] = not self.dependencies["protobuf"].options.lite
        tc.variables["onnxruntime_USE_XNNPACK"] = self.options.with_xnnpack
        tc.variables["onnxruntime_USE_CUDA"] = self.options.with_cuda
        tc.variables["onnxruntime_BUILD_UNIT_TESTS"] = False
        tc.variables["onnxruntime_DISABLE_CONTRIB_OPS"] = False
        tc.variables["onnxruntime_USE_FLASH_ATTENTION"] = False
        tc.variables["onnxruntime_DISABLE_RTTI"] = False
        tc.variables["onnxruntime_DISABLE_EXCEPTIONS"] = False
        tc.variables["onnxruntime_ARMNN_RELU_USE_CPU"] = False
        tc.variables["onnxruntime_ARMNN_BN_USE_CPU"] = False
        tc.variables["onnxruntime_ENABLE_CPU_FP16_OPS"] = False
        tc.variables["onnxruntime_ENABLE_EAGER_MODE"] = False
        tc.variables["onnxruntime_ENABLE_LAZY_TENSOR"] = False
        tc.variables["onnxruntime_ENABLE_CUDA_EP_INTERNAL_TESTS"] = False
        tc.variables["onnxruntime_USE_NEURAL_SPEED"] = False
        tc.variables["onnxruntime_USE_MEMORY_EFFICIENT_ATTENTION"] = False

        # Disable a warning that gets converted to an error
        tc.preprocessor_definitions["_SILENCE_ALL_CXX23_DEPRECATION_WARNINGS"] = "1"
        tc.generate()

        deps = CMakeDeps(self)
        deps.set_property("boost::headers", "cmake_target_name", "Boost::mp11")
        deps.set_property("flatbuffers", "cmake_target_name", "flatbuffers::flatbuffers")
        deps.generate()

    def _patch_sources(self):
        apply_conandata_patches(self)
        # Copy of the ConanCenter-provided deps file (find_package for every
        # Conan requirement); kept here because the upstream repo only ships the
        # FetchContent version.
        copy(self, "onnxruntime_external_deps.cmake",
             src=os.path.join(self.export_sources_folder, "cmake"),
             dst=os.path.join(self.source_folder, "cmake", "external"))
        replace_in_file(self, os.path.join(self.source_folder, "cmake", "CMakeLists.txt"),
                        "if (Git_FOUND)", "if (FALSE)")
        # Clang 18 fix (upstream PR #27669, merged after v1.24.4): replace the
        # GCC-specific __builtin_cpu_supports("waitpkg") with the portable
        # CPUID-bit check for TPAUSE detection.
        replace_in_file(
            self,
            os.path.join(self.source_folder, "onnxruntime", "core", "common", "cpuid_info.cc"),
            (
                "        if (check_intel.is_intel) {\n"
                "#ifdef __linux__\n"
                "#if !defined(__ANDROID__)\n"
                "          has_tpause_ = __builtin_cpu_supports(\"waitpkg\") != 0;\n"
                "#endif\n"
                "#else\n"
                "          has_tpause_ = (data[2] & (1 << 5)) != 0;\n"
                "#endif\n"
                "        }"
            ),
            (
                "        if (check_intel.is_intel) {\n"
                "          has_tpause_ = (data[2] & (1 << 5)) != 0;\n"
                "        }"
            ),
        )

    def build(self):
        cmake = CMake(self)
        cmake.configure(build_script_folder="cmake", cli_args=["--compile-no-warning-as-error"])
        cmake.build()

    def package(self):
        copy(self, pattern="LICENSE", dst=os.path.join(self.package_folder, "licenses"), src=self.source_folder)
        cmake = CMake(self)
        cmake.install()
        rmdir(self, os.path.join(self.package_folder, "lib", "pkgconfig"))
        rmdir(self, os.path.join(self.package_folder, "lib", "cmake"))

    def package_info(self):
        if self.options.shared:
            self.cpp_info.libs = ["onnxruntime"]
        else:
            # order is important
            # https://github.com/microsoft/onnxruntime/blob/v1.23.2/cmake/onnxruntime.cmake#L240
            onnxruntime_libs = [
                "session",
                *(["providers_xnnpack"] if self.options.with_xnnpack else []),
                "optimizer",
                "providers",
                "lora",
                "framework",
                "graph",
                "util",
                "mlas",
                "common",
                "flatbuffers",
            ]
            self.cpp_info.libs = [f"onnxruntime_{lib}" for lib in onnxruntime_libs]

        self.cpp_info.includedirs.append("include/onnxruntime")
        if not self.options.shared:
            self.cpp_info.includedirs.append("include/onnxruntime/core/session")

        if self.settings.os in ["Linux", "Android", "FreeBSD", "SunOS", "AIX"]:
            self.cpp_info.system_libs.append("m")
        if self.settings.os in ["Linux", "FreeBSD", "SunOS", "AIX"]:
            self.cpp_info.system_libs.append("pthread")
        if is_apple_os(self):
            self.cpp_info.frameworks.append("Foundation")
        if self.settings.os == "Windows":
            self.cpp_info.system_libs.append("shlwapi")

        self.cpp_info.set_property("cmake_file_name", "onnxruntime")
        self.cpp_info.set_property("cmake_target_name", "onnxruntime::onnxruntime")
        self.cpp_info.set_property("pkg_config_name", "onnxruntime")
