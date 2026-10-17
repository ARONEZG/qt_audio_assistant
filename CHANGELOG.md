# Changelog

Все заметные изменения этого проекта документируются здесь.

Формат основан на принципах [Keep a Changelog](https://keepachangelog.com/ru/1.1.0/),
а версии следуют [Semantic Versioning](https://semver.org/lang/ru/).

## [Unreleased]

### Fixed

- Окно приложения больше не зависает невидимым: добавлено свойство `visible: true` в `ApplicationWindow` (`qml/Main.qml`). Ранее окно создавалось, но не отображалось.

### Added

- Логирование через spdlog: мост Qt → spdlog `LogBridge` (`src/logging/LogBridge.hpp`,
  `src/logging/LogBridge.cpp`). При создании устанавливается цветной консольный логгер
  и Qt-обработчик сообщений (`qInstallMessageHandler`), при разрушении — предыдущие
  обработчик и логгер восстанавливаются. `qDebug/qInfo/qWarning/qCritical` маппятся на
  уровни spdlog, `qFatal` логируется и завершает процесс. Подключено в `src/main.cpp`.
- Зависимость `spdlog/1.14.1` в `conanfile.py`; поиск и линковка `spdlog::spdlog`
  (`CMakeLists.txt`, `find_package(spdlog 1.14 REQUIRED)`). Conan — единственный источник
  версии: CI, `scripts/ci-check.sh` и `scripts/static-analysis.sh` выполняют `conan install`
  и передают `-DCONAN_BUILD_DIR`; системный `libspdlog-dev` не используется.
- Тесты моста логирования (`tests/tst_logging.cpp`): маршрутизация сообщений,
  фильтрация по уровню, восстановление Qt-обработчика и логгера spdlog, а также
  завершение процесса по `qFatal` (проверяется в дочернем процессе).
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
- Conan 2.x skeleton: `conanfile.py` и профиль `profiles/clang18` для будущего управления
  зависимостями. Зависимость Qt через ConanCenter пока отключена (`requires = ()`); на Linux
  Qt берётся из системы.
- Опциональная поддержка Conan в `CMakeLists.txt` через переменную `CONAN_BUILD_DIR`.

### Changed

- `AGENTS.md` реструктурирован: общие правила (~80 строк) в корне, специфичные
  для подсистем — во вложенных `src/AGENTS.md`, `qml/AGENTS.md`, `tests/AGENTS.md`
  (загружаются OpenCode V2 только при работе в соответствующей директории).
- Правила ветвления, changelog и code reuse вынесены в `docs/BRANCHING.md`,
  `docs/CHANGELOG_GUIDE.md`, `docs/CODE_REUSE_POLICY.md`.
- Обновлён `.gitignore`: исключены артефакты покрытия (`*.profraw`, `*.profdata`),
  кеш-директория `.cache/`.
- Обновлён `docs/STATIC_ANALYSIS_TOOLS.md`: состояние инструментов в окружении.
- Правила Git workflow: авто-push + auto-PR для значимых изменений, CI/CD gate для
  мержа, изоляция изменений (`AGENTS.md` §6, `docs/BRANCHING.md`).
