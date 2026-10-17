#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
# static-analysis.sh — единый скрипт статического анализа
#
# Запускает все инструменты статического анализа, перечисленные
# в AGENTS.md §8, через один вызов. Модели (AI и разработчику)
# не нужно помнить отдельные команды каждого инструмента.
#
# Использование:
#   scripts/static-analysis.sh                    # полный прогон
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

# ── Helper: C++ sources ────────────────────────────────────

cpp_sources() {
    find "$PROJECT_ROOT/src" "$PROJECT_ROOT/tests" \
        -name '*.cpp' -o -name '*.h' -o -name '*.hpp' 2>/dev/null | sort
}

cpp_impl_sources() {
    find "$PROJECT_ROOT/src" "$PROJECT_ROOT/tests" \
        -name '*.cpp' 2>/dev/null | sort
}

qml_sources() {
    find "$PROJECT_ROOT/qml" -name '*.qml' 2>/dev/null | sort
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
    mkdir -p "$build_dir"
    CC="$cc" CXX="$cpp" cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -G Ninja -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -3 || {
        fail "CMake configure (Debug) failed"
        return 1
    }
    cmake --build "$build_dir" -j"$(nproc)" 2>&1 | tail -3 || {
        fail "Debug build failed"
        return 1
    }
    echo "$build_dir"
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
    files=$(cpp_sources)
    if [ -z "$files" ]; then
        warn "No source files found"
        return 0
    fi

    if $ct -p "$build_dir" --warnings-as-errors='*' $files; then
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

    if $cl \
        -checks=level0,level1,level2 \
        -p "$build_dir/compile_commands.json" \
        $files; then
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

    cppcheck --enable=all --inconclusive --error-exitcode=1 \
        --suppress=missingIncludeSystem \
        -I "$PROJECT_ROOT/src" \
        -i "$PROJECT_ROOT/build-debug" 2>/dev/null \
        -i "$PROJECT_ROOT/build-release" 2>/dev/null \
        -i "$PROJECT_ROOT/build-asan" 2>/dev/null \
        -i "$PROJECT_ROOT/build-cov" 2>/dev/null \
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
    mkdir -p "$build_dir"

    local cc cpp
    cc="$(command -v clang-18 || command -v clang || echo "")"
    cpp="$(command -v clang++-18 || command -v clang++ || echo "")"
    if [ -z "$cc" ] || [ -z "$cpp" ]; then
        warn "Clang not found — skipping scan-build"
        return 0
    fi

    CC="$cc" CXX="$cpp" cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -G Ninja -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -3 || {
        warn "scan-build: CMake configure failed — skipping"
        return 0
    }

    if $sb --use-cc="$cc" --use-c++="$cpp" \
        cmake --build "$build_dir" -j"$(nproc)" 2>&1; then
        pass "scan-build OK"
    else
        warn "scan-build found issues (optional, non-blocking)"
    fi
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
    files=$(cpp_sources)
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

    local results_dir
    results_dir=$(mktemp -d)

    if CodeChecker analyze "$build_dir/compile_commands.json" \
        --analyzers clang-tidy,cppcheck \
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
}

# ── Help ────────────────────────────────────────────────────

show_help() {
    cat << EOF
static-analysis.sh — единый скрипт статического анализа

Запускает все инструменты статического анализа из AGENTS.md §8
через один вызов. Модели (AI и разработчику) не нужно помнить
отдельные команды каждого инструмента.

Использование:
  scripts/static-analysis.sh                    полный прогон
  scripts/static-analysis.sh --quick            только формат + QML
  scripts/static-analysis.sh --all              то же, что без флагов
  scripts/static-analysis.sh --format           только clang-format + qmlformat
  scripts/static-analysis.sh --qmllint          только qmllint
  scripts/static-analysis.sh --linters          clang-tidy + clazy
  scripts/static-analysis.sh --cppchecker      cppcheck (опционально)
  scripts/static-analysis.sh --scan-build      Clang Static Analyzer (опционально)
  scripts/static-analysis.sh --iwyu             Include What You Use (опционально)
  scripts/static-analysis.sh --codechecker      CodeChecker (опционально)
  scripts/static-analysis.sh --list-tools       показать найденные инструменты
  scripts/static-analysis.sh --help             эта справка

Обязательные шаги (прерывают выполнение при ошибке):
  clang-format, qmlformat, qmllint, clang-tidy, clazy

Опциональные шаги (только предупреждают при ошибке):
  cppcheck, scan-build, iwyu, CodeChecker
EOF
}

# ── Main ────────────────────────────────────────────────────

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
            step_clang_format || ((failed_q++))
            step_qmlformat || ((failed_q++))
            step_qmllint || ((failed_q++))
            echo ""
            if [ "$failed_q" -eq 0 ]; then
                pass "All format + QML checks passed!"
            else
                fail "$failed_q step(s) failed"
                exit 1
            fi
            ;;
        --all|--)
            echo "=== Static Analysis: Full Run ==="
            echo ""

            local failed=0

            echo "--- Format checks ---"
            step_clang_format || ((failed++))
            step_qmlformat || ((failed++))
            step_qmllint || ((failed++))
            echo ""

            echo "--- Linters ---"
            step_clang_tidy || ((failed++))
            step_clazy || ((failed++))
            echo ""

            echo "--- Optional checks ---"
            step_cppcheck || true
            step_scan_build || true
            step_iwyu || true
            step_codechecker || true
            echo ""

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
