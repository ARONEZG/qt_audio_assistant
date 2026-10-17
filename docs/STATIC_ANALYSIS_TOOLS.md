# Инструменты статического и динамического анализа

Единый справочник инструментов качества для проекта. Обязательность и порядок применения заданы в
`AGENTS.md`, раздел «Статический и динамический анализ». Этот файл содержит назначение, формат
подключения и типовые команды установки/запуска. Ничего не ставить автоматически без запроса.

## Обязательные

- **clang** — компилятор и фронтенд для всех анализаторов. `clang`, `clang++`.
- **clang-format** — форматирование. Формат `--dry-run --Werror` обязателен.
- **clang-tidy** — базовая проверка (`bugprone`, `clang-analyzer`, `modernize`, `performance`,
  `readability`, `cppcoreguidelines`, `concurrency`, `portability`); источник `.clang-tidy`.
- **clazy** — Qt-специфичный анализатор на базе Clang. Обязателен для кода, работающего с Qt
  (QObject, signals/slots, QML view models). Уровни `level0,level1` всегда, `level2` для
  новых/изменённых translation units. См. <https://github.com/KDE/clazy>.
- **CTest + Qt Test** — запуск тестов.
- **Clang Sanitizers** — ASan + UBSan в отдельной Debug-сборке.
- **LLVM coverage** — `llvm-profdata`, `llvm-cov`, `-fprofile-instr-generate -fcoverage-mapping`.

## Требуемые для Qt/QML

- **qmlformat** — форматирование QML. Обязан проходить в check-режиме.
- **qmllint** — линтинг QML. Обязан быть без предупреждений.
- **qmltyperegistrar** — проверка регистрации типов QML.

## Дополнительные (запускать при доступности)

- **cppcheck** — независимая диагностика UB и опасных конструкций.
  `cppcheck --enable=all --inconclusive --error-exitcode=1`.
- **scan-build / Clang Static Analyzer** — path-sensitive анализ. Пересекается с
  `clang-analyzer-*` в clang-tidy, но добавляет каналы cross-translation-unit.
- **Include What You Use (iwyu)** — лишние/недостающие include.
- **Facebook Infer** — lifetime/race; подключать как review-layer, а не блокирующий gate, пока не
  стабилизирован по шуму.
- **ThreadSanitizer (TSan)** — для многопоточных модулей, отдельная сборка (не с ASan).

## Агрегатор

- **CodeChecker** (<https://github.com/Ericsson/codechecker>) — единый прогон clang static analyzer,
  clang-tidy, cppcheck, GCC analyzer и Infer поверх `compile_commands.json`; хранение дефектов,
  diff между прогонами, web viewer. Не заменяет отдельные шаги clazy и строгой сборки.

## Примерные команды (ориентир, проверить под текущую ОС)

```bash
# clang-format
clang-format --dry-run --Werror <changed files>

# clang-tidy через compile_commands.json
clang-tidy -p build <changed files> --warnings-as-errors='*'

# clazy standalone (без пересборки)
clazy-standalone -checks=level0,level1 -p build/compile_commands.json <changed files>

# cppcheck
cppcheck --enable=all --inconclusive --error-exitcode=1 \
  --suppress=missingIncludeSystem -I src -i build src tests

# scan-build (Clang Static Analyzer)
scan-build cmake --build build

# CodeChecker (log требует повторной сборки)
CodeChecker log -b "cmake --build build" -o compile_commands.json
CodeChecker analyze build/compile_commands.json -o ./cc-results
CodeChecker parse ./cc-results -e html -o ./cc-html
```

## Установка (примеры, без автоустановки)

```bash
# Ubuntu/Debian (clang/clang-tidy/clang-format/cppcheck)
sudo apt-get install clang clang-tidy clang-format cppcheck llvm

# clazy — собрать из исходников под установленный clang/llvm
# (Linux/Windows/macOS; тестировался на Linux/macOS/Windows+MSVC)

# CodeChecker
pip3 install codechecker          # или snap:  sudo snap install codechecker --classic

# iwyu
sudo apt-get install iwyu
```

## Состояние инструментов в текущем окружении

На 2026-10-16 доступны:

| Инструмент         | Версия          | Источник       | Статус      |
|---------------------|-----------------|----------------|-------------|
| clang-18            | 18.1.3          | apt (Ubuntu)   | OK          |
| clang-format-18     | 18.1.3          | apt (Ubuntu)   | OK          |
| clang-tidy-18       | 18.1.3          | apt (Ubuntu)   | OK          |
| ninja-build         | 1.11.1          | apt (Ubuntu)   | OK          |
| cppcheck            | 2.13.0          | apt (Ubuntu)   | OK          |
| iwyu                | 0.21 (clang 17) | apt (Ubuntu)   | OK          |
| clazy               | 1.11-4ubuntu2   | apt (Ubuntu)   | OK          |
| scan-build-18       | 18.1.3          | apt (Ubuntu)   | OK          |
| codechecker         | 6.29.1          | pipx           | OK          |
| pre-commit          | 4.7.0           | pipx           | OK          |
| qmlformat (Qt6)     | —               | apt (qt6-tools)     | OK |
| qmllint (Qt6)       | —               | apt (qt6-tools)     | OK |
| qmltyperegistrar    | —               | apt (qt6-tools)     | OK |
| llvm-profdata-18    | 18.1.3          | apt (Ubuntu)   | OK          |
| llvm-cov-18         | 18.1.3          | apt (Ubuntu)   | OK          |

Недостающие: `qmlformat` доступен как `/usr/lib/qt6/bin/qmlformat`,
но в `PATH` по умолчанию не входит. Локальный скрипт `scripts/ci-check.sh`
разрешает его по полному пути.
