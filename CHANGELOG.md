# Changelog

Все заметные изменения этого проекта документируются здесь.

Формат основан на принципах [Keep a Changelog](https://keepachangelog.com/ru/1.1.0/),
а версии следуют [Semantic Versioning](https://semver.org/lang/ru/).

## [Unreleased]

### Added

- CI pipeline: GitHub Actions workflow (`.github/workflows/ci.yml`), локальный скрипт
  `scripts/ci-check.sh`, pre-commit hooks (`.pre-commit-config.yaml`), конфигурация `act`
  для локального прогона GitHub Actions.
- Установлены недостающие инструменты: cppcheck, iwyu, clazy, codechecker, qmlformat,
  qmllint, qmltyperegistrar, pre-commit.
- Проектные инструкции для AI-агента разработки (`AGENTS.md`) и каталог кандидатов на
  переиспользование (`docs/REUSE_CANDIDATES.md`).
- Конфигурации качества кода: `.clang-format`, `.clang-tidy`.
- Минимальный собираемый GUI-каркас: `CMakeLists.txt`, `src/main.cpp`, `qml/Main.qml`.
- QML-модуль `AudioAssistant` со встроенными ресурсами (qt_add_qml_module).
- Qt Test smoke-тест загрузки корневого QML-компонента (`tests/tst_main.cpp`).
- Интеграция с CTest, ASan+UBSan, llvm-cov, clang-tidy.
- Документация сборки и запуска (`README.md`).
- Файл `.gitignore` для build-каталогов, IDE и OS-артефактов.

### Changed

- Обновлён `.gitignore`: исключены артефакты покрытия (`*.profraw`, `*.profdata`),
  кеш-директория `.cache/`.
- Обновлён `docs/STATIC_ANALYSIS_TOOLS.md`: состояние инструментов в окружении.
