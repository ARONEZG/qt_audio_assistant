#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
# ci-check.sh — локальный прогон всех обязательных quality gates
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

# ── Tool discovery ──────────────────────────────────────────

find_clang_format() {
    for candidate in clang-format-18 clang-format; do
        if command -v "$candidate" &>/dev/null; then
            echo "$candidate"
            return
        fi
    done
    echo ""
}

find_clang_tidy() {
    for candidate in clang-tidy-18 clang-tidy; do
        if command -v "$candidate" &>/dev/null; then
            echo "$candidate"
            return
        fi
    done
    echo ""
}

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

find_qmlformat() {
    for candidate in /usr/lib/qt6/bin/qmlformat /usr/lib/qt6/bin/qmlformat6 qmlformat; do
        if command -v "$candidate" &>/dev/null; then
            echo "$candidate"
            return
        fi
    done
    echo ""
}

find_qmllint() {
    for candidate in /usr/lib/qt6/bin/qmllint /usr/lib/qt6/bin/qmllint6 qmllint; do
        if command -v "$candidate" &>/dev/null; then
            echo "$candidate"
            return
        fi
    done
    echo ""
}

# ── Step functions ──────────────────────────────────────────

step_clang_format() {
    info "clang-format (dry-run, --Werror)"
    local cf
    cf=$(find_clang_format)
    if [ -z "$cf" ]; then
        warn "clang-format not found — skipping"
        return 0
    fi

    local src_files
    src_files=$(find "$PROJECT_ROOT/src" "$PROJECT_ROOT/tests" \
        -name '*.cpp' -o -name '*.h' -o -name '*.hpp' 2>/dev/null | sort)
    if [ -z "$src_files" ]; then
        warn "No C++ source files found"
        return 0
    fi

    if "$cf" --dry-run --Werror $src_files; then
        pass "clang-format OK"
    else
        fail "clang-format found issues"
        return 1
    fi
}

step_qmlformat() {
    info "qmlformat (dry-run)"
    local qf
    qf=$(find_qmlformat)
    if [ -z "$qf" ]; then
        warn "qmlformat not found — skipping"
        return 0
    fi

    local qml_files
    qml_files=$(find "$PROJECT_ROOT/qml" "$PROJECT_ROOT/tests" \
        -name '*.qml' 2>/dev/null | sort)
    if [ -z "$qml_files" ]; then
        warn "No QML files found"
        return 0
    fi

    local tmpfile
    tmpfile=$(mktemp)
    local ok=true
    for f in $qml_files; do
        if ! "$qf" "$f" 2>&1 > "$tmpfile"; then
            fail "qmlformat error on: $f"
            ok=false
            continue
        fi
        if ! diff -q "$f" "$tmpfile" > /dev/null 2>&1; then
            fail "qmlformat would modify: $f"
            ok=false
        fi
    done
    rm -f "$tmpfile"
    if $ok; then
        pass "qmlformat OK"
    else
        return 1
    fi
}

step_qmllint() {
    info "qmllint"
    local ql
    ql=$(find_qmllint)
    if [ -z "$ql" ]; then
        warn "qmllint not found — skipping"
        return 0
    fi

    local qml_files
    qml_files=$(find "$PROJECT_ROOT/qml" -name '*.qml' 2>/dev/null | sort)
    if [ -z "$qml_files" ]; then
        warn "No QML files found"
        return 0
    fi

    if "$ql" $qml_files; then
        pass "qmllint OK"
    else
        fail "qmllint found issues"
        return 1
    fi
}

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

    CC="$cc" CXX="$cpp" cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -G Ninja -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -5 || {
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

    CC="$cc" CXX="$cpp" cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -G Ninja -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -5 || {
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

step_clang_tidy() {
    info "clang-tidy"
    local ct
    ct=$(find_clang_tidy)
    if [ -z "$ct" ]; then
        warn "clang-tidy not found — skipping"
        return 0
    fi

    local build_dir="$PROJECT_ROOT/build-debug"
    if [ ! -f "$build_dir/compile_commands.json" ]; then
        warn "compile_commands.json not found — run debug build first"
        step_debug_build || return 1
    fi

    local src_files
    src_files=$(find "$PROJECT_ROOT/src" "$PROJECT_ROOT/tests" \
        -name '*.cpp' -o -name '*.h' -o -name '*.hpp' 2>/dev/null | sort)
    if [ -z "$src_files" ]; then
        warn "No source files found"
        return 0
    fi

    if "$ct" -p "$build_dir" --warnings-as-errors='*' $src_files; then
        pass "clang-tidy OK"
    else
        fail "clang-tidy found issues"
        return 1
    fi
}

step_clazy() {
    info "clazy (level0,level1,level2)"
    if ! command -v clazy-standalone &>/dev/null; then
        warn "clazy-standalone not found — skipping"
        return 0
    fi

    local build_dir="$PROJECT_ROOT/build-debug"
    if [ ! -f "$build_dir/compile_commands.json" ]; then
        warn "compile_commands.json not found — run debug build first"
        step_debug_build || return 1
    fi

    local src_files
    src_files=$(find "$PROJECT_ROOT/src" "$PROJECT_ROOT/tests" \
        -name '*.cpp' 2>/dev/null | sort)
    if [ -z "$src_files" ]; then
        warn "No source files found"
        return 0
    fi

    if clazy-standalone \
        -checks=level0,level1,level2 \
        -p "$build_dir/compile_commands.json" \
        $src_files; then
        pass "clazy OK"
    else
        fail "clazy found issues"
        return 1
    fi
}

step_cppcheck() {
    info "cppcheck (optional)"
    if ! command -v cppcheck &>/dev/null; then
        warn "cppcheck not found — skipping"
        return 0
    fi

    cppcheck --enable=all --inconclusive --error-exitcode=1 \
        --suppress=missingIncludeSystem \
        -I "$PROJECT_ROOT/src" \
        -i "$PROJECT_ROOT/build-debug" \
        -i "$PROJECT_ROOT/build-release" \
        -i "$PROJECT_ROOT/build-asan" \
        "$PROJECT_ROOT/src" "$PROJECT_ROOT/tests" 2>&1 || {
        warn "cppcheck found issues (optional, non-blocking)"
    }
    pass "cppcheck done"
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

    info "Configuring ASan+UBSan build..."
    CC="$cc" CXX="$cpp" cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
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

    info "Configuring coverage build..."
    CC="$cc" CXX="$cpp" cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
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
        $(find . -name '*.so' -o -name 'audio-assistant' -o -name 'tst_*' | grep -v autogen) 2>&1 || true

    pass "Coverage report generated"
}

# ── Main ────────────────────────────────────────────────────

show_help() {
    cat << EOF
ci-check.sh — local quality gates runner

Usage: scripts/ci-check.sh [OPTIONS]

Options:
  -- all        Run all checks (default)
  --quick       Format + QML checks only
  --build       Debug build + tests only
  --analyze     Static analysis (clng-tidy, lazy, cppcheck)
  --sanitize    ASan + UBan sanitizers
  --coverage    LLVM source-based coverage
  --help        This help

The script fails with exit code 1 if any check fails.
EOF
}

main() {
    cd "$PROJECT_ROOT"

    local mode="${1:---all}"

    case "$mode" in
        --quick)
            step_clang_format || exit 1
            step_qmlformat || exit 1
            step_qmllint || exit 1
            ;;
        --build)
            step_debug_build || exit 1
            step_debug_tests || exit 1
            step_release_build || exit 1
            step_release_tests || exit 1
            ;;
        --analyze)
            step_clang_tidy || exit 1
            step_clazy || exit 1
            step_cppcheck || exit 1
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
            step_clang_format || ((failed_steps++))
            step_qmlformat || ((failed_steps++))
            step_qmllint || ((failed_steps++))

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
            step_clang_tidy || ((failed_steps++))
            step_clazy || ((failed_steps++))
            step_cppcheck || ((failed_steps++))

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
