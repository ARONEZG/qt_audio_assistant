# Разработка в VS Code

Готовая конфигурация рабочего пространства лежит в `.vscode/` и коммитится в репозиторий.
Она покрывает четыре сценария: **сборка**, **запуск**, **отладка**, **clang-format и
clang-tidy**. Конфигурация сборки не дублируется — единственный источник истины остаётся
`CMakePresets.json` (`audio-assistant-debug`, `audio-assistant-release`).

## Предварительные требования

| Что нужно | Проверка | Примечание |
|---|---|---|
| Clang 18 | `clang-18 --version` | единственный допустимый компилятор (см. `AGENTS.md`) |
| Qt 6.8.3 | `ls ~/Qt/6.8.3/gcc_64` | `aqtinstall`, путь подхватывается `CMakeLists.txt` |
| Conan 2 | `conan --version` | ставится per-user, обычно `~/.local/bin/conan` |
| Нина/CMake | `ninja --version`, `cmake --version` | CMake ≥ 3.26 |
| GDB | `gdb --version` | отладчик для `cppdbg` |
| Расширения | — | `.vscode/extensions.json`, панель Extensions |

Расширения: **CMake Tools**, **clangd**, **C/C++** (только как отладчик `cppdbg`),
**Qt C++ / Qt QML**, **Conan**. IntelliSense расширения C/C++ отключён
(`C_Cpp.intelliSenseEngine: disabled`), навигацию и диагностику даёт clangd — две модели
кода одновременно конфликтуют.

## Быстрый старт

1. Открыть каталог репозитория (или worktree) в VS Code.
2. Один раз выбрать configure-пресет: палитра команд → **CMake: Select Configure Preset** →
   `audio-assistant-debug`. Выбор запоминается для рабочего пространства; при включённом
   `cmake.configureOnOpen` конфигурация запускается сразу.
3. `F5` (или **Run and Debug** → `Debug: audio-assistant (Debug, GUI)`) — сборка и запуск
   под отладчиком.

При первой конфигурации `CMakeLists.txt` сам выполняет `conan install` (включая экспорт
рецепта `recipes/llmqore` из репозитория), поэтому отдельный шаг «поставить зависимости»
не нужен.

## Задачи (`Terminal → Run Task`)

| Задача | Что делает |
|---|---|
| `cmake: configure (debug)` / `(release)` | `cmake --preset …`; при необходимости сам поднимает Conan |
| `cmake: build (debug)` / `(release)` | `cmake --build --preset …` (Debug — задача сборки по умолчанию, `Ctrl+Shift+B`) |
| `app: run (debug)` | запуск GUI без отладчика |
| `test: ctest (debug)` | `ctest --preset audio-assistant-debug` (Qt Test, `QT_QPA_PLATFORM=offscreen`) |
| `clang-format: current file` | `clang-format -i` для открытого файла |
| `clang-format: all project sources` | `clang-format -i` по `src/` и `tests/` |
| `clang-format: check (repo script)` | `scripts/static-analysis.sh --format` (`--dry-run --Werror` + `qmlformat`) |
| `clang-tidy: current file` | `clang-tidy` по открытому файлу с базой `build/debug/compile_commands.json` |
| `clang-tidy: all project sources (repo script)` | `scripts/static-analysis.sh --linters` (`clang-tidy` + `clazy`) |
| `quality: full gate (ci-check.sh)` | полный локальный quality gate: формат + QML, Debug/Release сборка и тесты, статический анализ, санитары, покрытие |

`clang-format` на сохранении включён только для C/C++ и выполняется через clangd с
проектным `.clang-format`. QML не форматируется на сохранении: канонический форматтер —
`qmlformat` (задача `clang-format: check (repo script)`), потому что `qmlls` даёт другое
форматирование и ломал бы проверку.

## Отладка

- `Debug: audio-assistant (Debug, GUI)` — приложение с реальным GUI.
- `Debug: Qt test (Debug, offscreen)` — выбранный Qt Test (`tst_main`, `tst_message`,
  `tst_answer`, `tst_chat`, `tst_logging`) с `QT_QPA_PLATFORM=offscreen`. Аргументы
  `args` можно дополнить именем тест-функции, например `test_sendsMessage`.
- `Debug: selected CMake target (Debug)` — цель, выбранная командой **CMake: Set Debug Target**.

Всем конфигурациям предшествует `preLaunchTask` `cmake: build (debug)`, поэтому бинарник
всегда свежий. Бинарники берутся из каталога пресета `build/debug` (не `build-debug`,
который создаёт `scripts/ci-check.sh`).

## Частые проблемы

- **`conan` не найден при конфигурации.** VS Code, запущенный из графической сессии, может
  не иметь `~/.local/bin` в `PATH`. На Linux/macOS настройки `cmake.configureEnvironment`
  и `env` задач добавляют этот каталог сами; если Conan установлен в другое место —
  поправить `.vscode/settings.json` и `.vscode/tasks.json` (`PATH`). На Windows `PATH`
  задач не переопределяется: `conan.exe` должен быть виден процессу VS Code (запустите
  `code` из терминала либо добавьте каталог Conan в системный `PATH`).
- **clangd не видит код (`'QtCore/…' file not found`, нет навигации).** Обычно нет
  `build/debug/compile_commands.json` — выполните задачу `cmake: configure (debug)`, затем
  команду **clangd: Restart language server**.
- **Устаревший `compile_commands.json`.** Базу всегда создаёт конфигурация Debug: после
  смены пресета/переконфигурации перезапустить clangd.
- **Другая версия Clang.** Поправить `--query-driver` в `.vscode/settings.json`
  (`/usr/bin/clang++-18` в Ubuntu/Debian; в других дистрибутивах, например
  `/usr/lib/llvm-18/bin/clang++`).
- **Проект не собирается (`Ninja`/пресет).** Точные команды те же, что и в задачах:
  `cmake --preset audio-assistant-debug && cmake --build --preset audio-assistant-debug`.

## Статус платформ

- **Linux** — проверено (сборка, запуск, отладка, clang-format, clang-tidy).
- **Windows / macOS** — конфигурации добавлены (Qt-пути, `MIMode`, `.exe`, bundle), но не
  проверялись: правки в этих блоках требуют проверки на соответствующей платформе, как и
  любой другой код проекта (см. `AGENTS.md`, раздел «Кроссплатформенность»).
