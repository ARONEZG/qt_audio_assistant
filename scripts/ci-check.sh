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
# Локальная скорость:
#   * все сборки идут через ccache (если он есть) — повторные прогоны
#     переиспользуют объектные файлы Qt/moc/LLMQore;
#   * `conan install` выполняется один раз на build_type за прогон;
#   * статический анализ (clang-tidy/clazy) распараллелен и ограничен
#     собственным кодом (без _deps/ и генерации).
#
# Использование:
#   scripts/ci-check.sh              # полный прогон (CI parity)
#   scripts/ci-check.sh --deep       # полный + тяжёлые анализаторы
#   scripts/ci-check.sh --fast       # быстрый: формат+QML, Debug+tests,
#                                    #   clang-tidy/clazy по изменённым файлам
#   scripts/ci-check.sh --quick      # только формат + QML
#   scripts/ci-check.sh --build      # только сборка + тесты
#   scripts/ci-check.sh --analyze    # только статический анализ
#   scripts/ci-check.sh --sanitize   # только санитайзеры
#   scripts/ci-check.sh --coverage   # только покрытие
#   scripts/ci-check.sh --jobs N     # число потоков (сборка и анализ)
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

# ── Parallelism ─────────────────────────────────────────────
# Compilation is CPU-bound, but the analyzers (clang-tidy/clazy) and the
# sanitizer/coverage builds are memory-hungry (~1–2 GiB per process). Blindly
# using `nproc` on a busy laptop makes the run swap and end up *slower* than a
# smaller number of jobs. BUILD_JOBS / ANALYSIS_JOBS / --jobs override.

BUILD_JOBS="${BUILD_JOBS:-}"

_cpu_count() {
    if command -v nproc &>/dev/null; then
        nproc
    elif command -v sysctl &>/dev/null; then
        sysctl -n hw.ncpu 2>/dev/null || echo 4
    else
        echo 4
    fi
}

_mem_available_gb() {
    # Linux only; returns 0 ("unknown") elsewhere, so callers fall back to the
    # CPU count. macOS/Windows do not expose /proc/meminfo and take that path.
    if [ -r /proc/meminfo ] && command -v awk &>/dev/null; then
        awk '/^MemAvailable:/ {printf "%.0f", $2/1048576; exit}' /proc/meminfo 2>/dev/null
    else
        echo 0
    fi
}

# Memory-aware cap for memory-hungry tools: ~2 GiB per job, never more than the
# CPU count. Returns nproc when available memory cannot be read.
mem_aware_jobs() {
    local cpu mem jobs
    cpu=$(_cpu_count)
    mem=$(_mem_available_gb)
    if [ "$mem" -gt 0 ]; then
        jobs=$(( mem / 2 ))
        [ "$jobs" -lt 1 ] && jobs=1
        [ "$jobs" -gt "$cpu" ] && jobs=$cpu
        echo "$jobs"
    else
        echo "$cpu"
    fi
}

build_jobs() {
    if [ -n "$BUILD_JOBS" ]; then
        echo "$BUILD_JOBS"
    else
        _cpu_count
    fi
}

# ── ccache (local speed-up) ─────────────────────────────────
# ccache is transparent and already used by CI. Enabling it locally makes
# repeated runs (and the four build variants) reuse objects instead of
# recompiling Qt/moc/LLMQore from scratch every time.
setup_ccache() {
    if ! command -v ccache &>/dev/null; then
        return 0
    fi
    # Reuse across worktrees/absolute paths and avoid the default 5 GiB
    # cache thrashing on a project with four build trees.
    export CCACHE_BASEDIR="${CCACHE_BASEDIR:-$PROJECT_ROOT}"
    export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-20G}"
}

ccache_cmake_args() {
    if command -v ccache &>/dev/null; then
        printf '%s\n' \
            -DCMAKE_C_COMPILER_LAUNCHER=ccache \
            -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    fi
}

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

# ── Conan dependencies (spdlog, miniaudio, sqlite3, llmqore) ─

CONAN_BUILD_DIR="$PROJECT_ROOT/build/conan"
CONAN_LLMQORE_RECIPE="$PROJECT_ROOT/recipes/llmqore"
_CONAN_INSTALLED=""

# spdlog, miniaudio, sqlite3 and LLMQore are managed Conan dependencies
# (conanfile.py) and CMake pins find_package(spdlog 1.14 REQUIRED) /
# find_package(SQLite3 REQUIRED) / find_package(LLMQore REQUIRED), so every
# CMake configure must be preceded by `conan install` and receive
# -DCONAN_BUILD_DIR. spdlog, miniaudio and sqlite3 come from ConanCenter;
# llmqore is built from the in-repo recipe, which has to be exported into the
# cache first.
#
# The in-repo profile is passed for both the build and host contexts so no
# machine-global default profile is required; the build type is matched so
# Release links Release-built packages, not the Debug ones.
#
# The same build_type is requested several times per full run (Debug for the
# debug, ASan and coverage trees); it is installed once per build_type.
conan_install() {
    local build_type="$1"
    case " $_CONAN_INSTALLED " in
        *" $build_type "*)
            info "Conan install ($build_type) — уже выполнен"
            return 0
            ;;
    esac
    if ! command -v conan &>/dev/null; then
        fail "conan not found in PATH (required for the spdlog/miniaudio/sqlite3/llmqore dependencies)"
        return 1
    fi
    info "Conan export recipes/llmqore"
    conan export "$CONAN_LLMQORE_RECIPE" 2>&1 | tail -2 || {
        fail "conan export (llmqore recipe) failed"
        return 1
    }
    info "Conan install ($build_type)"
    conan install "$PROJECT_ROOT" -of "$CONAN_BUILD_DIR" --build=missing \
        -pr:b "$PROJECT_ROOT/profiles/clang18" -pr "$PROJECT_ROOT/profiles/clang18" \
        -s:b build_type="$build_type" -s build_type="$build_type" 2>&1 | tail -3 || {
        fail "conan install failed"
        return 1
    }
    _CONAN_INSTALLED="$_CONAN_INSTALLED $build_type"
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

    local launcher
    launcher=$(ccache_cmake_args)
    # shellcheck disable=SC2086  # ccache flag list is intentionally word-split
    CC="$cc" CXX="$cpp" cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DCONAN_BUILD_DIR="$CONAN_BUILD_DIR" \
        $launcher 2>&1 | tail -5 || {
        fail "CMake configure (Debug) failed"
        return 1
    }

    cmake --build "$build_dir" -j"$(build_jobs)" 2>&1 | tail -5 || {
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
    if ctest --test-dir "$build_dir" --output-on-failure -j"$(build_jobs)"; then
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

    local launcher
    launcher=$(ccache_cmake_args)
    # shellcheck disable=SC2086  # ccache flag list is intentionally word-split
    CC="$cc" CXX="$cpp" cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCONAN_BUILD_DIR="$CONAN_BUILD_DIR" \
        $launcher 2>&1 | tail -5 || {
        fail "CMake configure (Release) failed"
        return 1
    }

    cmake --build "$build_dir" -j"$(build_jobs)" 2>&1 | tail -5 || {
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
    if ctest --test-dir "$build_dir" --output-on-failure -j"$(build_jobs)"; then
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
    local launcher
    launcher=$(ccache_cmake_args)
    # shellcheck disable=SC2086  # ccache flag list is intentionally word-split
    CC="$cc" CXX="$cpp" cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCONAN_BUILD_DIR="$CONAN_BUILD_DIR" \
        -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
        -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
        -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined" \
        $launcher \
        2>&1 | tail -5 || {
        fail "ASan+UBSan configure failed"
        return 1
    }

    info "Building with sanitizers..."
    cmake --build "$build_dir" -j"$(build_jobs)" 2>&1 | tail -5 || {
        fail "ASan+UBSan build failed"
        return 1
    }

    info "Running tests under sanitizers..."
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
    UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
        ctest --test-dir "$build_dir" --output-on-failure -j"$(build_jobs)" || {
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
    local launcher
    launcher=$(ccache_cmake_args)
    # shellcheck disable=SC2086  # ccache flag list is intentionally word-split
    CC="$cc" CXX="$cpp" cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCONAN_BUILD_DIR="$CONAN_BUILD_DIR" \
        -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
        -DCMAKE_C_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
        -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
        -DCMAKE_SHARED_LINKER_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
        $launcher \
        2>&1 | tail -5 || {
        fail "Coverage configure failed"
        return 1
    }

    info "Building instrumented binaries..."
    cmake --build "$build_dir" -j"$(build_jobs)" 2>&1 | tail -5 || {
        fail "Coverage build failed"
        return 1
    }

    info "Running tests for profiling data..."
    # Drop stale profiles from earlier runs: merging profiles built against
    # different binaries can make llvm-cov stall.
    rm -f "$build_dir"/code-*.profraw
    LLVM_PROFILE_FILE="$build_dir/code-%p.profraw" \
        ctest --test-dir "$build_dir" --output-on-failure -j"$(build_jobs)" || {
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

    # DEBUGINFOD_URLS (set by default on e.g. Ubuntu) makes llvm-profdata and
    # llvm-cov block on network lookups for system debug info. Project objects
    # ship their own DWARF, so the lookups only cost minutes of wall-clock —
    # disable debuginfod for a deterministic, offline report.
    local no_debuginfod=(env -u DEBUGINFOD_URLS)

    cd "$build_dir"
    "${no_debuginfod[@]}" "$profdata" merge -sparse code-*.profraw -o code.profdata 2>/dev/null || {
        warn "No profile data to merge"
        return 0
    }

    # Report coverage for project-owned binaries only. Conan-provided
    # third-party code (spdlog, LLMQore) lives outside the build tree, and Qt
    # build helpers under .qt/ are not instrumented for coverage and must never
    # be scanned.
    local objects
    objects=$(find . -type d \( -name '_deps' -o -name '.qt' -o -name '*_autogen' \) -prune -o \
        -type f \( -name 'audio-assistant' -o -name 'tst_*' -o -name '*.so' \) -print)

    "${no_debuginfod[@]}" "$cov" report \
        -instr-profile=code.profdata \
        -ignore-filename-regex='.*(moc_|_autogen|_deps|build|/usr/|\.qt/).*' \
        $objects 2>&1 || true

    pass "Coverage report generated"
}

# ── Changed-file detection (for --fast) ─────────────────────
# C++ files changed relative to origin/main (committed and uncommitted). The
# --fast path runs clang-tidy/clazy on these instead of the whole tree.
changed_cpp_files() {
    {
        git diff --name-only --diff-filter=ACMR origin/main...HEAD -- src tests 2>/dev/null
        git diff --name-only --diff-filter=ACMR -- src tests 2>/dev/null
    } | grep -E '\.(cpp|h|hpp)$' | sort -u || true
}

# ── Parallel phase wrappers ─────────────────────────────────
# run_all launches these concurrently after the Debug build: each writes its own
# build tree, and static analysis only needs Debug's compile_commands.json.

_ANALYSIS_MODE="--all"

phase_release()  { step_release_build && step_release_tests; }
phase_sanitize() { step_asan_ubsan; }
phase_coverage() { step_coverage; }
phase_analysis() { "$SCRIPT_DIR/static-analysis.sh" "$_ANALYSIS_MODE"; }

# Runs the four independent phases concurrently under a shared job budget.
# Output is buffered per phase and printed once all phases finish, so parallel
# diagnostics stay readable. Returns non-zero if any phase failed.
run_parallel() {
    local share="$1"
    local analysis_share="$2"
    export BUILD_JOBS="$share"
    export ANALYSIS_JOBS="$analysis_share"

    local status_dir
    status_dir=$(mktemp -d)

    local -a phases=(release sanitize coverage analysis)
    local i=0
    local p
    for p in "${phases[@]}"; do
        i=$((i + 1))
        (
            # errexit is off here on purpose: a failing phase must not abort the
            # subshell before its exit status is captured.
            set +e
            "phase_$p" > "$status_dir/$i.log" 2>&1
            rc=$?
            printf '%s\n' "$rc" > "$status_dir/$i.status"
        ) &
    done
    wait || true

    local any_failed=0
    i=0
    for p in "${phases[@]}"; do
        i=$((i + 1))
        echo ""
        echo "---- phase: $p ----"
        sed 's/^/  /' "$status_dir/$i.log" 2>/dev/null || true
        if [ "$(cat "$status_dir/$i.status" 2>/dev/null || echo 1)" != "0" ]; then
            fail "phase '$p' FAILED"
            any_failed=1
        else
            pass "phase '$p' OK"
        fi
    done
    rm -rf "$status_dir"
    return "$any_failed"
}

# ── Main ────────────────────────────────────────────────────

show_help() {
    cat << EOF
ci-check.sh — local quality gates runner

Usage: scripts/ci-check.sh [OPTIONS]

Options:
  --all             Run all checks (default, CI parity)
  --deep            Run all checks + heavy optional analyzers
                    (scan-build, iwyu, CodeChecker)
  --fast            Fast incremental check: format + QML, Debug build + tests,
                    then clang-tidy/clazy on changed files only. Release,
                    sanitizers and coverage are skipped — run --all before push.
  --quick           Format + QML checks only (delegates to static-analysis.sh)
  --build           Debug + Release build and tests
  --analyze         Static analysis: clang-tidy + clazy (delegates to static-analysis.sh)
  --static-analysis Full static analysis (delegates to static-analysis.sh)
  --deep-static-analysis
                    Static analysis + heavy optional analyzers
  --sanitize        ASan + UBSan sanitizers
  --coverage        LLVM source-based coverage
  --jobs N, -j N    Parallel jobs for builds, tests and analyzers
  --help            This help

Environment:
  BUILD_JOBS        parallel build/test jobs (default: nproc)
  ANALYSIS_JOBS     parallel analyzer processes (default: memory-aware)
  CCACHE_MAXSIZE    ccache limit (default locally: 20G)

The script fails with exit code 1 if any check fails.
EOF
}

run_all() {
    local analysis_mode="$1"
    local label="$2"
    _ANALYSIS_MODE="$analysis_mode"
    echo "=== Quality Gates: $label ==="
    echo ""

    local failed_steps=0

    echo "--- Phase 1: Format + QML ---"
    "$SCRIPT_DIR/static-analysis.sh" --quick || failed_steps=$((failed_steps + 1))

    echo ""
    echo "--- Phase 2: Debug Build + Tests ---"
    step_debug_build || failed_steps=$((failed_steps + 1))
    step_debug_tests || failed_steps=$((failed_steps + 1))

    # Install the Release deps up front: the parallel phases would otherwise
    # race on the shared build/conan output dir (Debug was already installed by
    # the Debug build). _CONAN_INSTALLED is exported so the forked subshells
    # inherit it and skip their own `conan install`.
    conan_install Release || failed_steps=$((failed_steps + 1))
    export _CONAN_INSTALLED

    # The three remaining builds + static analysis are independent of each
    # other (Debug's compile_commands.json already exists). Run them
    # concurrently, splitting the job budget evenly so the machine is not
    # oversubscribed. The analysis phase is the most memory-hungry, so its
    # budget is additionally capped by the memory-aware limit (mem_aware_jobs
    # is a standalone cap; take the smaller of the two).
    echo ""
    echo "--- Phase 3: Release + Sanitizers + Coverage + Analysis (parallel) ---"
    local share
    share=$(( $(build_jobs) / 4 ))
    [ "$share" -lt 1 ] && share=1
    local analysis_share
    analysis_share=$(mem_aware_jobs)
    [ "$analysis_share" -gt "$share" ] && analysis_share=$share
    [ "$analysis_share" -lt 1 ] && analysis_share=1
    run_parallel "$share" "$analysis_share" || failed_steps=$((failed_steps + 1))

    echo ""
    echo "======================================"
    if [ "$failed_steps" -eq 0 ]; then
        pass "All quality gates passed!"
        exit 0
    else
        fail "$failed_steps step(s) failed"
        exit 1
    fi
}

fast_run() {
    echo "=== Fast checks (incremental) ==="
    echo ""

    local failed=0

    echo "--- Format + QML ---"
    "$SCRIPT_DIR/static-analysis.sh" --quick || failed=$((failed + 1))

    echo ""
    echo "--- Debug build + tests ---"
    step_debug_build || failed=$((failed + 1))
    step_debug_tests || failed=$((failed + 1))

    echo ""
    echo "--- Linters (changed files) ---"
    local files
    if git rev-parse --verify --quiet origin/main >/dev/null 2>&1; then
        files=$(changed_cpp_files)
        if [ -z "$files" ]; then
            info "No changed C++ files — skipping clang-tidy/clazy"
        else
            info "   $(wc -l <<< "$files") changed file(s)"
            ANALYSIS_FILES="$files" "$SCRIPT_DIR/static-analysis.sh" --linters || failed=$((failed + 1))
        fi
    else
        warn "origin/main not found — linting all translation units (no change base)"
        "$SCRIPT_DIR/static-analysis.sh" --linters || failed=$((failed + 1))
    fi

    echo ""
    echo "======================================"
    if [ "$failed" -eq 0 ]; then
        pass "Fast checks passed (run --all before push)"
        exit 0
    else
        fail "$failed step(s) failed"
        exit 1
    fi
}

main() {
    cd "$PROJECT_ROOT"
    setup_ccache

    local mode="--all"
    local jobs=""

    while [ "$#" -gt 0 ]; do
        case "$1" in
            --jobs|-j)
                if [ "$#" -lt 2 ]; then
                    echo "Option $1 requires a value" >&2
                    exit 2
                fi
                jobs="$2"
                shift 2
                ;;
            --jobs=*)
                jobs="${1#*=}"
                shift
                ;;
            --fast|--all|--deep|--quick|--build|--analyze|--static-analysis|--deep-static-analysis|--sanitize|--coverage|--help)
                mode="$1"
                shift
                ;;
            *)
                echo "Unknown option: $1" >&2
                show_help
                exit 1
                ;;
        esac
    done

    if [ -n "$jobs" ]; then
        export BUILD_JOBS="$jobs"
        export ANALYSIS_JOBS="$jobs"
    fi

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
        --deep-static-analysis)
            "$SCRIPT_DIR/static-analysis.sh" --deep || exit 1
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
        --fast)
            fast_run
            ;;
        --deep)
            run_all --deep "Full Run + Deep Analysis"
            ;;
        --all)
            run_all --all "Full Run"
            ;;
    esac
}

main "$@"
