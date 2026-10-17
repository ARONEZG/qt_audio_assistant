#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
# ci-check.sh — локальный прогон всех обязательных quality gates
#
# Статический анализ вынесен в scripts/static-analysis.sh.
# См. docs/STATIC_ANALYSIS_TOOLS.md.
#
# Этот скрипт зеркалирует .github/workflows/ci.yml и позволяет
# агенту (и разработчику) запустить все проверки локально одной
# командой до отправки кода в CI.
#
# Использование:
#   scripts/ci-check.sh              # полный прогон
#   scripts/ci-check.sh --quick      # только формат + QML
#   scripts/ci-check.sh --build      # только сборка + тесты
#   scripts/ci-check.sh --analyze    # только статический анализ
#   scripts/ci-check.sh --sanitize   # только санитайзеры
#   scripts/ci-check.sh --coverage   # только покрытие
#   scripts/ci-check.sh --help       # справка
# ─────────────────────────────────────────────────────────────────

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

pass()  { echo -e "${GREEN}[PASS]${NC} $*"; }
fail()  { echo -e "${RED}[FAIL]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
info()  { echo -e "[INFO] $*"; }

# ── Tool discovery (build, sanitizer, coverage) ────────────

find_clang() {
    for candidate in clang-18 clang; do
        if command -v "$candidate" &>/dev/null; then
            echo "$candidate"
            return
        fi
    done
    echo ""
}

find_clangpp() {
    for candidate in clang++-18 clang++; do
        if command -v "$candidate" &>/dev/null; then
            echo "$candidate"
            return
        fi
    done
    echo ""
}

# ── Conan / spdlog dependency ───────────────────────────────

CONAN_BUILD_DIR="$PROJECT_ROOT/build/conan"

# spdlog is a managed Conan dependency (conanfile.py) and CMake pins
# find_package(spdlog 1.14 REQUIRED), so every CMake configure must be preceded
# by `conan install` and receive -DCONAN_BUILD_DIR. The build type is matched so
# Release links a Release-built spdlog, not the Debug one.
conan_install() {
    local build_type="$1"
    if ! command -v conan &>/dev/null; then
        fail "conan not found in PATH (required for the spdlog dependency)"
        return 1
    fi
    info "Conan install ($build_type)"
    conan install "$PROJECT_ROOT" -of "$CONAN_BUILD_DIR" --build=missing \
        -pr "$PROJECT_ROOT/profiles/clang18" -s build_type="$build_type" 2>&1 | tail -3 || {
        fail "conan install failed"
        return 1
    }
    pass "Conan install ($build_type) OK"
}

# ── Step functions ──────────────────────────────────────────

step_debug_build() {
    info "Debug build"
    local cc cpp
    cc=$(find_clang)
    cpp=$(find_clangpp)
    if [ -z "$cc" ] || [ -z "$cpp" ]; then
        fail "Clang compiler not found"
        return 1
    fi

    local build_dir="$PROJECT_ROOT/build-debug"
    mkdir -p "$build_dir"

    conan_install Debug || return 1

    CC="$cc" CXX="$cpp" cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DCONAN_BUILD_DIR="$CONAN_BUILD_DIR" 2>&1 | tail -5 || {
        fail "CMake configure (Debug) failed"
        return 1
    }

    cmake --build "$build_dir" -j"$(nproc)" 2>&1 | tail -5 || {
        fail "Debug build failed"
        return 1
    }
    pass "Debug build OK"
}

step_debug_tests() {
    info "Tests (Debug)"
    local build_dir="$PROJECT_ROOT/build-debug"
    if [ ! -f "$build_dir/CTestTestfile.cmake" ]; then
        warn "Debug build not found — running build first"
        step_debug_build || return 1
    fi
    if ctest --test-dir "$build_dir" --output-on-failure -j"$(nproc)"; then
        pass "Tests (Debug) OK"
    else
        fail "Tests (Debug) failed"
        return 1
    fi
}

step_release_build() {
    info "Release build"
    local cc cpp
    cc=$(find_clang)
    cpp=$(find_clangpp)
    if [ -z "$cc" ] || [ -z "$cpp" ]; then
        fail "Clang compiler not found"
        return 1
    fi

    local build_dir="$PROJECT_ROOT/build-release"
    mkdir -p "$build_dir"

    conan_install Release || return 1

    CC="$cc" CXX="$cpp" cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCONAN_BUILD_DIR="$CONAN_BUILD_DIR" 2>&1 | tail -5 || {
        fail "CMake configure (Release) failed"
        return 1
    }

    cmake --build "$build_dir" -j"$(nproc)" 2>&1 | tail -5 || {
        fail "Release build failed"
        return 1
    }
    pass "Release build OK"
}

step_release_tests() {
    info "Tests (Release)"
    local build_dir="$PROJECT_ROOT/build-release"
    if [ ! -f "$build_dir/CTestTestfile.cmake" ]; then
        warn "Release build not found — running build first"
        step_release_build || return 1
    fi
    if ctest --test-dir "$build_dir" --output-on-failure -j"$(nproc)"; then
        pass "Tests (Release) OK"
    else
        fail "Tests (Release) failed"
        return 1
    fi
}

step_asan_ubsan() {
    info "ASan + UBSan"
    local cc cpp
    cc=$(find_clang)
    cpp=$(find_clangpp)
    if [ -z "$cc" ] || [ -z "$cpp" ]; then
        fail "Clang compiler not found"
        return 1
    fi

    local build_dir="$PROJECT_ROOT/build-asan"
    mkdir -p "$build_dir"

    conan_install Debug || return 1

    info "Configuring ASan+UBSan build..."
    CC="$cc" CXX="$cpp" cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCONAN_BUILD_DIR="$CONAN_BUILD_DIR" \
        -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
        -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
        -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined" \
        2>&1 | tail -5 || {
        fail "ASan+UBSan configure failed"
        return 1
    }

    info "Building with sanitizers..."
    cmake --build "$build_dir" -j"$(nproc)" 2>&1 | tail -5 || {
        fail "ASan+UBSan build failed"
        return 1
    }

    info "Running tests under sanitizers..."
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
    UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
        ctest --test-dir "$build_dir" --output-on-failure -j"$(nproc)" || {
        fail "Sanitizers found issues"
        return 1
    }
    pass "ASan + UBSan OK"
}

step_coverage() {
    info "LLVM source-based coverage"
    local cc cpp
    cc=$(find_clang)
    cpp=$(find_clangpp)
    if [ -z "$cc" ] || [ -z "$cpp" ]; then
        fail "Clang compiler not found"
        return 1
    fi

    local build_dir="$PROJECT_ROOT/build-cov"
    mkdir -p "$build_dir"

    conan_install Debug || return 1

    info "Configuring coverage build..."
    CC="$cc" CXX="$cpp" cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCONAN_BUILD_DIR="$CONAN_BUILD_DIR" \
        -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
        -DCMAKE_C_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
        -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
        -DCMAKE_SHARED_LINKER_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
        2>&1 | tail -5 || {
        fail "Coverage configure failed"
        return 1
    }

    info "Building instrumented binaries..."
    cmake --build "$build_dir" -j"$(nproc)" 2>&1 | tail -5 || {
        fail "Coverage build failed"
        return 1
    }

    info "Running tests for profiling data..."
    LLVM_PROFILE_FILE="$build_dir/code-%p.profraw" \
        ctest --test-dir "$build_dir" --output-on-failure -j"$(nproc)" || {
        warn "Some tests failed (coverage data may be incomplete)"
    }

    info "Generating coverage report..."
    local profdata
    profdata=$(command -v llvm-profdata-18 || command -v llvm-profdata || echo "")
    local cov
    cov=$(command -v llvm-cov-18 || command -v llvm-cov || echo "")

    if [ -z "$profdata" ] || [ -z "$cov" ]; then
        warn "llvm-profdata/llvm-cov not found — skipping report"
        return 0
    fi

    cd "$build_dir"
    "$profdata" merge -sparse code-*.profraw -o code.profdata 2>/dev/null || {
        warn "No profile data to merge"
        return 0
    }

    "$cov" report \
        -instr-profile=code.profdata \
        -ignore-filename-regex='.*(moc_|_autogen|build|/usr/).*' \
        $(find . -type f -name '*.so' -o -type f -name 'audio-assistant' -o -type f -name 'tst_*' | grep -v autogen) 2>&1 || true

    pass "Coverage report generated"
}

# ── Main ────────────────────────────────────────────────────

show_help() {
    cat << EOF
ci-check.sh — local quality gates runner

Usage: scripts/ci-check.sh [OPTIONS]

Options:
  --all             Run all checks (default)
  --quick           Format + QML checks only (delegates to static-analysis.sh)
  --build           Debug build + tests only
  --analyze         Static analysis: clang-tidy + clazy (delegates to static-analysis.sh)
  --static-analysis Full static analysis (delegates to static-analysis.sh)
  --sanitize        ASan + UBSan sanitizers
  --coverage        LLVM source-based coverage
  --help            This help

The script fails with exit code 1 if any check fails.
EOF
}

main() {
    cd "$PROJECT_ROOT"

    local mode="${1:---all}"

    case "$mode" in
        --quick)
            "$SCRIPT_DIR/static-analysis.sh" --quick || exit 1
            ;;
        --build)
            step_debug_build || exit 1
            step_debug_tests || exit 1
            step_release_build || exit 1
            step_release_tests || exit 1
            ;;
        --analyze)
            "$SCRIPT_DIR/static-analysis.sh" --linters || exit 1
            ;;
        --static-analysis)
            "$SCRIPT_DIR/static-analysis.sh" || exit 1
            ;;
        --sanitize)
            step_asan_ubsan || exit 1
            ;;
        --coverage)
            step_coverage || exit 1
            ;;
        --help)
            show_help
            exit 0
            ;;
        --all)
            echo "=== Quality Gates: Full Run ==="
            echo ""

            local failed_steps=0

            echo "--- Phase 1: Format + QML ---"
            "$SCRIPT_DIR/static-analysis.sh" --quick || ((failed_steps++))

            echo ""
            echo "--- Phase 2: Debug Build + Tests ---"
            step_debug_build || ((failed_steps++))
            step_debug_tests || ((failed_steps++))

            echo ""
            echo "--- Phase 3: Release Build + Tests ---"
            step_release_build || ((failed_steps++))
            step_release_tests || ((failed_steps++))

            echo ""
            echo "--- Phase 4: Static Analysis ---"
            "$SCRIPT_DIR/static-analysis.sh" || ((failed_steps++))

            echo ""
            echo "--- Phase 5: Sanitizers ---"
            step_asan_ubsan || ((failed_steps++))

            echo ""
            echo "--- Phase 6: Coverage ---"
            step_coverage || ((failed_steps++))

            echo ""
            echo "======================================"
            if [ "$failed_steps" -eq 0 ]; then
                pass "All quality gates passed!"
                exit 0
            else
                fail "$failed_steps step(s) failed"
                exit 1
            fi
            ;;
        *)
            echo "Unknown option: $mode"
            show_help
            exit 1
            ;;
    esac
}

main "$@"
