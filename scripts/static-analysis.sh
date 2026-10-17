#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
# static-analysis.sh — единый скрипт статического анализа
#
# Запускает инструменты статического анализа через один вызов.
# Модели (AI и разработчику) не нужно помнить отдельные команды.
#
# Быстрый прогон (по умолчанию) зеркалит CI-джобу «Static Analysis»:
# clang-format, qmlformat, qmllint, clang-tidy, clazy, cppcheck.
# Тяжёлые необязательные анализаторы (scan-build, iwyu, CodeChecker)
# вынесены в режим --deep: они не входят в обязательный CI-gate и
# дороги по времени.
#
# Анализу подлежит только собственный код репозитория (src/, tests/,
# qml/). Сторонние библиотеки (Conan-пакеты spdlog/LLMQore), moc/qml-
# генерация и Qt build helpers исключаются из анализа явно.
#
# Скорость:
#   * clang-tidy/clazy распараллелены по числу CPU (ANALYSIS_JOBS);
#   * анализируются только translation units (.cpp) — заголовки
#     проверяются через включающие их TU (HeaderFilterRegex уже
#     ограничивает диагностику src/tests), поэтому отдельный прогон
#     по .h только замедлял работу;
#   * CodeChecker получает отфильтрованную базу (только src/ + tests/).
#
# Использование:
#   scripts/static-analysis.sh                    # быстрый прогон (CI parity)
#   scripts/static-analysis.sh --deep             # быстрый + scan-build/iwyu/CodeChecker
#   scripts/static-analysis.sh --quick            # только формат + QML
#   scripts/static-analysis.sh --all              # то же, что без флагов
#   scripts/static-analysis.sh --format           # только clang-format + QML format
#   scripts/static-analysis.sh --linters          # clang-tidy + clazy
#   scripts/static-analysis.sh --cppcheck         # cppcheck (опционально)
#   scripts/static-analysis.sh --scan-build       # Clang Static Analyzer (опционально)
#   scripts/static-analysis.sh --iwyu             # Include What You Use (опционально)
#   scripts/static-analysis.sh --qmllint          # только QML линтинг
#   scripts/static-analysis.sh --codechecker      # CodeChecker (опционально)
#   scripts/static-analysis.sh --list-tools       # показать найденные инструменты
#   scripts/static-analysis.sh --help             # справка
#
# Скрипт завершается с exit-кодом >0, если хотя бы один
# обязательный шаг не прошёл. Опциональные шаги (cppcheck,
# scan-build, iwyu, CodeChecker) при ошибке только предупреждают.
# ─────────────────────────────────────────────────────────────────

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass()  { echo -e "${GREEN}[PASS]${NC} $*"; }
fail()  { echo -e "${RED}[FAIL]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
info()  { echo -e "[INFO] $*"; }

# ── Parallelism ─────────────────────────────────────────────
# Analyzers are single-threaded; one process per translation unit is the
# cheapest way to use the machine.
ANALYSIS_JOBS="${ANALYSIS_JOBS:-}"
analysis_jobs() {
    if [ -n "$ANALYSIS_JOBS" ]; then
        echo "$ANALYSIS_JOBS"
    elif command -v nproc &>/dev/null; then
        nproc
    elif command -v sysctl &>/dev/null; then
        sysctl -n hw.ncpu 2>/dev/null || echo 4
    else
        echo 4
    fi
}

# Runs a tool once per line of stdin, up to ANALYSIS_JOBS processes in
# parallel. The tool command is passed as arguments and each stdin line is
# appended as the final argument. Per-invocation output is buffered and only
# printed when that invocation fails, so parallel diagnostics stay readable.
# Returns non-zero if any invocation failed.
run_on_files() {
    local jobs
    jobs=$(analysis_jobs)
    xargs -P "$jobs" -I{} bash -c '
        output=$("$@" 2>&1) || { printf "%s\n" "$output"; exit 1; }
    ' _ "$@" {}
}

# ── Tool discovery ──────────────────────────────────────────

find_clang_format() {
    for candidate in clang-format-18 clang-format-19 clang-format; do
        if command -v "$candidate" &>/dev/null; then
            echo "$candidate"
            return
        fi
    done
    echo ""
}

find_clang_tidy() {
    for candidate in clang-tidy-18 clang-tidy-19 clang-tidy; do
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

find_clazy() {
    for candidate in clazy-standalone clazy; do
        if command -v "$candidate" &>/dev/null; then
            echo "$candidate"
            return
        fi
    done
    echo ""
}

find_cppcheck() {
    if command -v cppcheck &>/dev/null; then
        echo "cppcheck"
    else
        echo ""
    fi
}

find_scan_build() {
    for candidate in scan-build-18 scan-build-19 scan-build; do
        if command -v "$candidate" &>/dev/null; then
            echo "$candidate"
            return
        fi
    done
    echo ""
}

find_iwyu() {
    for candidate in iwyu include-what-you-use; do
        if command -v "$candidate" &>/dev/null; then
            echo "$candidate"
            return
        fi
    done
    echo ""
}

find_codechecker() {
    if command -v CodeChecker &>/dev/null; then
        echo "CodeChecker"
    else
        echo ""
    fi
}

find_debug_build_dir() {
    local dir="$PROJECT_ROOT/build-debug"
    if [ -f "$dir/compile_commands.json" ]; then
        echo "$dir"
        return
    fi
    dir="$PROJECT_ROOT/build"
    if [ -f "$dir/compile_commands.json" ]; then
        echo "$dir"
        return
    fi
    echo ""
}

# ── Helper: C++ / QML sources ───────────────────────────────

cpp_sources() {
    find "$PROJECT_ROOT/src" "$PROJECT_ROOT/tests" \
        -name '*.cpp' -o -name '*.h' -o -name '*.hpp' 2>/dev/null | sort
}

# Translation units only. Headers are parsed through the TU that includes
# them (HeaderFilterRegex already restricts findings to src/tests), so listing
# them separately would re-parse every header without a useful compile command.
cpp_impl_sources() {
    find "$PROJECT_ROOT/src" "$PROJECT_ROOT/tests" \
        -name '*.cpp' 2>/dev/null | sort
}

qml_sources() {
    find "$PROJECT_ROOT/qml" -name '*.qml' 2>/dev/null | sort
}

# Directories that are never project-owned: build trees, fetched dependencies
# and Qt/moc generation.
third_party_excludes() {
    printf '%s\n' \
        "$PROJECT_ROOT/build" \
        "$PROJECT_ROOT/build-debug" \
        "$PROJECT_ROOT/build-release" \
        "$PROJECT_ROOT/build-asan" \
        "$PROJECT_ROOT/build-cov" \
        "$PROJECT_ROOT/build-scan" \
        "$PROJECT_ROOT/.git"
}

# ── Filtered compilation database ───────────────────────────
# Produces a compile_commands.json limited to src/ + tests/. Analyzers that
# consume the whole database (CodeChecker) would otherwise walk every
# generated translation unit under _deps/ and .qt/.
project_compile_commands() {
    local build_dir="$1"
    local db="$build_dir/compile_commands.json"
    if [ ! -f "$db" ]; then
        return 1
    fi
    if ! command -v python3 &>/dev/null; then
        # No python3: fall back to the full database (still correct, just
        # wider than we would like).
        echo "$db"
        return 0
    fi
    local out="$build_dir/compile_commands.project.json"
    python3 - "$db" "$PROJECT_ROOT" "$out" <<'PY'
import json
import os
import sys

source_db, root, output = sys.argv[1], sys.argv[2], sys.argv[3]
roots = (os.path.join(root, "src") + os.sep, os.path.join(root, "tests") + os.sep)
with open(source_db, encoding="utf-8") as handle:
    entries = json.load(handle)
kept = [entry for entry in entries if entry.get("file", "").startswith(roots)]
with open(output, "w", encoding="utf-8") as handle:
    json.dump(kept, handle, indent=2)
PY
    echo "$out"
}

# ccache launcher flags, when available, so the helper rebuild triggered by a
# missing database is not paid in full on every run.
ccache_cmake_args() {
    if command -v ccache &>/dev/null; then
        printf '%s' "-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
    fi
}

# ── Step functions ──────────────────────────────────────────

step_clang_format() {
    info "1. clang-format (dry-run, --Werror)"
    local cf
    cf=$(find_clang_format)
    if [ -z "$cf" ]; then
        warn "clang-format not found — skipping"
        return 0
    fi

    local files
    files=$(cpp_sources)
    if [ -z "$files" ]; then
        warn "No C++ source files found"
        return 0
    fi

    if $cf --dry-run --Werror $files; then
        pass "clang-format OK"
    else
        fail "clang-format found issues"
        return 1
    fi
}

step_qmlformat() {
    info "2. qmlformat (dry-run)"
    local qf
    qf=$(find_qmlformat)
    if [ -z "$qf" ]; then
        warn "qmlformat not found — skipping"
        return 0
    fi

    local files
    files=$(qml_sources)
    if [ -z "$files" ]; then
        warn "No QML files found"
        return 0
    fi

    local tmpfile
    tmpfile=$(mktemp)
    local ok=true
    for f in $files; do
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
    info "3. qmllint"
    local ql
    ql=$(find_qmllint)
    if [ -z "$ql" ]; then
        warn "qmllint not found — skipping"
        return 0
    fi

    local files
    files=$(qml_sources)
    if [ -z "$files" ]; then
        warn "No QML files found"
        return 0
    fi

    if $ql $files; then
        pass "qmllint OK"
    else
        fail "qmllint found issues"
        return 1
    fi
}

ensure_compile_commands() {
    local dir
    dir=$(find_debug_build_dir)
    if [ -n "$dir" ]; then
        echo "$dir"
        return 0
    fi

    info "compile_commands.json not found — building debug first"
    local cc cpp
    cc="$(command -v clang-18 || command -v clang || echo "")"
    cpp="$(command -v clang++-18 || command -v clang++ || echo "")"
    if [ -z "$cc" ] || [ -z "$cpp" ]; then
        fail "Clang compiler not found for debug build"
        return 1
    fi

    local build_dir="$PROJECT_ROOT/build-debug"
    local conan_dir="$PROJECT_ROOT/build/conan"
    mkdir -p "$build_dir"

    if ! command -v conan &>/dev/null; then
        fail "conan not found in PATH (required for the spdlog/llmqore dependencies)"
        return 1
    fi
    info "Conan export recipes/llmqore"
    conan export "$PROJECT_ROOT/recipes/llmqore" 2>&1 | tail -2 || {
        fail "conan export (llmqore recipe) failed"
        return 1
    }
    info "Conan install (Debug)"
    conan install "$PROJECT_ROOT" -of "$conan_dir" --build=missing \
        -pr:b "$PROJECT_ROOT/profiles/clang18" -pr "$PROJECT_ROOT/profiles/clang18" \
        -s:b build_type=Debug -s build_type=Debug 2>&1 | tail -3 || {
        fail "conan install failed"
        return 1
    }

    local launcher
    launcher=$(ccache_cmake_args)
    # shellcheck disable=SC2086  # ccache flag list is intentionally word-split
    CC="$cc" CXX="$cpp" cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DCONAN_BUILD_DIR="$conan_dir" \
        $launcher 2>&1 | tail -3 || {
        fail "CMake configure (Debug) failed"
        return 1
    }
    cmake --build "$build_dir" -j"$(analysis_jobs)" 2>&1 | tail -3 || {
        fail "Debug build failed"
        return 1
    }
    echo "$build_dir"
}

# Resolves the compilation database to use for a project-only analyzer.
project_db() {
    local build_dir="$1"
    local db
    db=$(project_compile_commands "$build_dir") || db="$build_dir/compile_commands.json"
    if [ -z "$db" ]; then
        db="$build_dir/compile_commands.json"
    fi
    echo "$db"
}

step_clang_tidy() {
    info "4. clang-tidy (--warnings-as-errors='*')"
    local ct
    ct=$(find_clang_tidy)
    if [ -z "$ct" ]; then
        warn "clang-tidy not found — skipping"
        return 0
    fi

    local build_dir
    build_dir=$(ensure_compile_commands) || return 1

    local files
    files=$(cpp_impl_sources)
    if [ -z "$files" ]; then
        warn "No source files found"
        return 0
    fi

    local db
    db=$(project_db "$build_dir")

    info "   $(wc -l <<< "$files") translation unit(s), $(analysis_jobs) job(s)"

    if printf '%s\n' "$files" \
        | run_on_files "$ct" -p "$db" --warnings-as-errors='*'; then
        pass "clang-tidy OK"
    else
        fail "clang-tidy found issues"
        return 1
    fi
}

step_clazy() {
    info "5. clazy (level0,level1,level2)"
    local cl
    cl=$(find_clazy)
    if [ -z "$cl" ]; then
        warn "clazy-standalone not found — skipping"
        return 0
    fi

    local build_dir
    build_dir=$(ensure_compile_commands) || return 1

    local files
    files=$(cpp_impl_sources)
    if [ -z "$files" ]; then
        warn "No source files found"
        return 0
    fi

    local db
    db=$(project_db "$build_dir")

    if printf '%s\n' "$files" \
        | run_on_files "$cl" -checks=level0,level1,level2 -p "$db"; then
        pass "clazy OK"
    else
        fail "clazy found issues"
        return 1
    fi
}

step_cppcheck() {
    info "6. cppcheck (optional)"
    local cc
    cc=$(find_cppcheck)
    if [ -z "$cc" ]; then
        warn "cppcheck not found — skipping"
        return 0
    fi

    local excludes=""
    local dir
    while IFS= read -r dir; do
        excludes="$excludes -i $dir"
    done < <(third_party_excludes)

    # shellcheck disable=SC2086  # -i exclusion list is intentionally word-split
    # No -j: cppcheck disables the unusedFunction check when run in parallel,
    # which would silently drop coverage. It is fast enough single-threaded.
    cppcheck --enable=all --inconclusive --error-exitcode=1 \
        --suppress=missingIncludeSystem \
        -I "$PROJECT_ROOT/src" \
        $excludes \
        "$PROJECT_ROOT/src" "$PROJECT_ROOT/tests" 2>&1 || {
        warn "cppcheck found issues (optional, non-blocking)"
    }
    pass "cppcheck done"
}

step_scan_build() {
    info "7. Clang Static Analyzer / scan-build (optional)"
    local sb
    sb=$(find_scan_build)
    if [ -z "$sb" ]; then
        warn "scan-build not found — skipping"
        return 0
    fi

    local build_dir="$PROJECT_ROOT/build-scan"
    local conan_dir="$PROJECT_ROOT/build/conan"
    mkdir -p "$build_dir"

    local cc cpp
    cc="$(command -v clang-18 || command -v clang || echo "")"
    cpp="$(command -v clang++-18 || command -v clang++ || echo "")"
    if [ -z "$cc" ] || [ -z "$cpp" ]; then
        warn "Clang not found — skipping scan-build"
        return 0
    fi
    if ! command -v conan &>/dev/null; then
        warn "conan not found — skipping scan-build"
        return 0
    fi

    info "Conan export recipes/llmqore"
    conan export "$PROJECT_ROOT/recipes/llmqore" 2>&1 | tail -2 || {
        warn "scan-build: conan export failed — skipping"
        return 0
    }

    info "Conan install (Debug)"
    conan install "$PROJECT_ROOT" -of "$conan_dir" --build=missing \
        -pr:b "$PROJECT_ROOT/profiles/clang18" -pr "$PROJECT_ROOT/profiles/clang18" \
        -s:b build_type=Debug -s build_type=Debug 2>&1 | tail -3 || {
        warn "scan-build: conan install failed — skipping"
        return 0
    }

    local launcher
    launcher=$(ccache_cmake_args)
    # shellcheck disable=SC2086  # ccache flag list is intentionally word-split
    CC="$cc" CXX="$cpp" cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DCONAN_BUILD_DIR="$conan_dir" \
        $launcher 2>&1 | tail -3 || {
        warn "scan-build: CMake configure failed — skipping"
        return 0
    }

    # Third-party trees (_deps/) are never reported on; the report goes to a
    # scratch dir so a previous analysis is not reused.
    local report_dir
    report_dir=$(mktemp -d)
    if $sb --use-cc="$cc" --use-c++="$cpp" \
        --exclude "$build_dir/_deps" \
        -o "$report_dir" \
        cmake --build "$build_dir" -j"$(analysis_jobs)" 2>&1; then
        pass "scan-build OK"
    else
        warn "scan-build found issues (optional, non-blocking)"
    fi
    rm -rf "$report_dir"
}

step_iwyu() {
    info "8. Include What You Use (optional)"
    local iw
    iw=$(find_iwyu)
    if [ -z "$iw" ]; then
        warn "iwyu not found — skipping"
        return 0
    fi

    local build_dir
    build_dir=$(ensure_compile_commands) || return 1

    local files
    files=$(cpp_impl_sources)
    if [ -z "$files" ]; then
        warn "No source files found"
        return 0
    fi

    local ok=true
    for f in $files; do
        if ! $iw -p "$build_dir" "$f" 2>&1; then
            warn "iwyu suggests changes in: $f (optional)"
            ok=false
        fi
    done
    if $ok; then
        pass "iwyu OK"
    else
        warn "iwyu: suggestions above (non-blocking)"
    fi
}

step_codechecker() {
    info "9. CodeChecker (optional, aggregator)"
    local cc_checker
    cc_checker=$(find_codechecker)
    if [ -z "$cc_checker" ]; then
        warn "CodeChecker not found — skipping"
        return 0
    fi

    local build_dir
    build_dir=$(ensure_compile_commands) || return 1

    # Restrict the database to src/ + tests/ so CodeChecker never analyzes
    # third-party (LLMQore) or generated code.
    local db
    db=$(project_db "$build_dir")

    local results_dir
    results_dir=$(mktemp -d)

    if CodeChecker analyze "$db" \
        --analyzers clang-tidy cppcheck \
        --output "$results_dir" 2>&1; then
        pass "CodeChecker analyze OK"
    else
        warn "CodeChecker analyze found issues (optional)"
    fi

    if CodeChecker parse "$results_dir" 2>&1 | head -20; then
        pass "CodeChecker parse done"
    fi
    rm -rf "$results_dir"
}

# ── list-tools ──────────────────────────────────────────────

list_tools() {
    echo "=== Доступные инструменты статического анализа ==="
    echo ""

    local cf; cf=$(find_clang_format);        echo "  clang-format:     ${cf:-NOT FOUND}"
    local qt_qf; qt_qf=$(find_qmlformat);     echo "  qmlformat:        ${qt_qf:-NOT FOUND}"
    local qt_ql; qt_ql=$(find_qmllint);       echo "  qmllint:          ${qt_ql:-NOT FOUND}"
    local ct; ct=$(find_clang_tidy);          echo "  clang-tidy:       ${ct:-NOT FOUND}"
    local cl; cl=$(find_clazy);               echo "  clazy:            ${cl:-NOT FOUND}"
    local cc; cc=$(find_cppcheck);            echo "  cppcheck:         ${cc:-NOT FOUND}"
    local sb; sb=$(find_scan_build);          echo "  scan-build:       ${sb:-NOT FOUND}"
    local iw; iw=$(find_iwyu);               echo "  iwyu:             ${iw:-NOT FOUND}"
    local cchk; cchk=$(find_codechecker);    echo "  CodeChecker:      ${cchk:-NOT FOUND}"

    local db; db=$(find_debug_build_dir)
    if [ -n "$db" ]; then
        echo ""
        echo "  compile_commands.json: $db"
    else
        echo ""
        echo "  compile_commands.json: NOT FOUND (будет создан при --linters)"
    fi
    echo ""
    echo "  analysis jobs: $(analysis_jobs) (переопределить: ANALYSIS_JOBS=N)"
    echo ""
}

# ── Help ────────────────────────────────────────────────────

show_help() {
    cat << EOF
static-analysis.sh — единый скрипт статического анализа

Быстрый прогон (по умолчанию) зеркалит CI-джобу «Static Analysis» и
анализирует только собственный код (src/, tests/, qml/).

Использование:
  scripts/static-analysis.sh                    быстрый прогон (CI parity)
  scripts/static-analysis.sh --deep             быстрый + тяжёлые необязательные
  scripts/static-analysis.sh --quick            только формат + QML
  scripts/static-analysis.sh --all              то же, что без флагов
  scripts/static-analysis.sh --format           только clang-format + qmlformat
  scripts/static-analysis.sh --qmllint          только qmllint
  scripts/static-analysis.sh --linters          clang-tidy + clazy
  scripts/static-analysis.sh --cppcheck         cppcheck (опционально)
  scripts/static-analysis.sh --scan-build       Clang Static Analyzer (опционально)
  scripts/static-analysis.sh --iwyu             Include What You Use (опционально)
  scripts/static-analysis.sh --codechecker      CodeChecker (опционально)
  scripts/static-analysis.sh --list-tools       показать найденные инструменты
  scripts/static-analysis.sh --help             эта справка

Обязательные шаги (прерывают выполнение при ошибке):
  clang-format, qmlformat, qmllint, clang-tidy, clazy

Опциональные шаги (только предупреждают при ошибке):
  cppcheck — в быстром прогоне;
  scan-build, iwyu, CodeChecker — только в режиме --deep.

Переменные окружения:
  ANALYSIS_JOBS   число параллельных процессов анализа (по умолчанию nproc)
EOF
}

# ── Main ────────────────────────────────────────────────────

mandatory_checks() {
    local failed=0
    echo "--- Format checks ---"
    step_clang_format || failed=$((failed + 1))
    step_qmlformat || failed=$((failed + 1))
    step_qmllint || failed=$((failed + 1))
    echo ""

    echo "--- Linters ---"
    step_clang_tidy || failed=$((failed + 1))
    step_clazy || failed=$((failed + 1))
    echo ""

    echo "--- cppcheck ---"
    step_cppcheck || true
    echo ""

    return "$failed"
}

deep_checks() {
    echo "--- Deep analysis (optional, non-blocking) ---"
    step_scan_build || true
    step_iwyu || true
    step_codechecker || true
    echo ""
}

main() {
    cd "$PROJECT_ROOT"

    local mode="${1:---all}"

    case "$mode" in
        --format)
            step_clang_format || exit 1
            step_qmlformat || exit 1
            ;;
        --qmllint)
            step_qmllint || exit 1
            ;;
        --linters)
            step_clang_tidy || exit 1
            step_clazy || exit 1
            ;;
        --cppcheck)
            step_cppcheck
            ;;
        --scan-build)
            step_scan_build
            ;;
        --iwyu)
            step_iwyu
            ;;
        --codechecker)
            step_codechecker
            ;;
        --list-tools)
            list_tools
            exit 0
            ;;
        --help)
            show_help
            exit 0
            ;;
        --quick)
            echo "=== Static Analysis: Format + QML ==="
            echo ""
            local failed_q=0
            step_clang_format || failed_q=$((failed_q + 1))
            step_qmlformat || failed_q=$((failed_q + 1))
            step_qmllint || failed_q=$((failed_q + 1))
            echo ""
            if [ "$failed_q" -eq 0 ]; then
                pass "All format + QML checks passed!"
            else
                fail "$failed_q step(s) failed"
                exit 1
            fi
            ;;
        --deep)
            echo "=== Static Analysis: Fast + Deep ==="
            echo ""
            local failed_d=0
            mandatory_checks || failed_d=$?
            deep_checks
            echo "======================"
            if [ "$failed_d" -eq 0 ]; then
                pass "All mandatory static analysis checks passed!"
                exit 0
            else
                fail "$failed_d mandatory step(s) failed"
                exit 1
            fi
            ;;
        --all|--)
            echo "=== Static Analysis: Fast Run (CI parity) ==="
            echo ""

            local failed=0
            mandatory_checks || failed=$?

            echo "======================"
            if [ "$failed" -eq 0 ]; then
                pass "All mandatory static analysis checks passed!"
                exit 0
            else
                fail "$failed mandatory step(s) failed"
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
