# Changelog

Все заметные изменения этого проекта документируются здесь.

Формат основан на принципах [Keep a Changelog](https://keepachangelog.com/ru/1.1.0/),
а версии следуют [Semantic Versioning](https://semver.org/lang/ru/).

## [Unreleased]

### Fixed

- Окно приложения больше не зависает невидимым: добавлено свойство `visible: true` в `ApplicationWindow` (`qml/Main.qml`). Ранее окно создавалось, но не отображалось.
- Локальная генерация coverage-отчёта больше не зависает на сетевых запросах:
  `llvm-profdata` и `llvm-cov` запускаются с отключённым debuginfod
  (`env -u DEBUGINFOD_URLS`). При выставленном `DEBUGINFOD_URLS` (по умолчанию в части
  окружений Ubuntu) `llvm-cov` блокировался на загрузке отладочной информации системных
  библиотек; собственные объекты проекта несут DWARF сами.
- `CodeChecker analyze` в `scripts/static-analysis.sh` получал недопустимый аргумент
  `--analyzers clang-tidy,cppcheck` (анализаторы передаются через пробел).

### Added

- Домен LLM-чата (первый C++-инкремент, `src/domain/`): неизменяемое значение
  `Message` (`MessageRole`), типизированная ошибка `ProviderError`, объект одного
  потокового ответа `Answer` (накопление текста, `complete`/`fail`/`cancel`,
  запросы `isCompleted`/`isFailed`/`isCancelled`, сигналы
  `chunkReceived`/`completed`/`failed`/`cancelled`), role-интерфейс `Provider`
  (`id`/`displayName`/`respond`) и `Chat` (провайдер через DI, append-only история
  сообщений, «живой» ответ в `streamingText`, команды `send`/`cancel`, типизированный
  сигнал `failed(ProviderError, QString)` и сигнал `cancelled()`).
- Владение потоковым `Answer` разделено явно: им владеет `Provider` (parent), а
  `Chat` только наблюдает и очищает свой указатель по сигналу `destroyed()` (и
  отключается в деструкторе), поэтому провайдер, удаливший `Answer`, не оставляет
  висячий указатель — `Chat` сбрасывает состояние стриминга и остаётся
  работоспособным.
- Статическая библиотека `audio-assistant-domain` и модульные тесты
  `tst_message`, `tst_answer`, `tst_chat` с тестовым двойником `FakeProvider`
  (Qt Test + CTest, `QT_QPA_PLATFORM=offscreen`, детерминированно, без сети).
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
- Короткий блок критических правил для AI-агента (`docs/AGENTS_CRITICAL.md`),
  подключаемый через `opencode.json` (`instructions`), чтобы правила изоляции и
  доставки (ветка → push → PR) не терялись в длинных сессиях.
- Git-hook'и `.githooks/pre-commit` и `.githooks/pre-push` + `scripts/setup-git-hooks.sh`:
  блокируют прямые коммиты и пуши в `main`/`master`, принуждая к изоляции в ветках;
  `.githooks/pre-commit` также делегирует quality-проверки в pre-commit framework.
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
- Зависимость **LLMQore v0.8.0** (MIT) подключается через CMake `FetchContent` с
  зафиксированным тегом и линкуется в исполняемый файл `audio-assistant`. API библиотеки
  пока не используется.

### Changed

- Локальный прогон quality gates (`scripts/ci-check.sh`, `scripts/static-analysis.sh`)
  стал значительно быстрее:
  - `clang-tidy` и `clazy` распараллелены по числу CPU (`ANALYSIS_JOBS`) и анализируют
    только собственные translation units (`src/`, `tests/`); заголовки проверяются через
    включающие их TU, а сторонний код (LLMQore в `_deps/`) и moc/qml-генерация исключены;
  - тяжёлые необязательные анализаторы (`scan-build`, `iwyu`, `CodeChecker`), которых нет
    в обязательном CI-gate, вынесены в режим `--deep`; `--all` теперь зеркалит CI
    (clang-format, qmlformat, qmllint, clang-tidy, clazy, cppcheck);
  - локальные сборки Debug/Release/ASan/coverage идут через `ccache` — повторные прогоны
    переиспользуют объектные файлы Qt/moc/LLMQore;
  - `conan install` выполняется один раз на `build_type` за прогон;
  - `CodeChecker` получает отфильтрованную базу компиляции (только `src/` + `tests/`).
- Профиль строгих предупреждений Clang вынесен в общую INTERFACE-цель
  `audio-assistant-strict`; `CMakeLists.txt` собирает домен отдельной целью
  `audio-assistant-domain` и линкует её в приложение и тесты (target-based).
  Подавления `-Wno-padded` и `-Wno-unsafe-buffer-usage` вынесены в отдельную
  INTERFACE-цель `audio-assistant-qt-warnings`, которую линкуют только домен и его
  тесты (Qt value-типы дают tail-padding, а moc-диспетчеры индексируют массив
  аргументов сигнала); остальные цели сохраняют полный `-Weverything`-профиль.
- `tests/.clang-tidy` наследует корневой конфиг и смягчает только порог
  `readability-function-size`: макросы Qt Test (`QCOMPARE`/`QVERIFY`) раздувают
  счётчик statements, из-за чего обычные тестовые функции ложно не проходили.
- Стадия Coverage в `scripts/ci-check.sh` отчитывается только по собственному коду
  проекта: сторонний код, подтянутый в `_deps/` (LLMQore), и сгенерированные
  Qt-файлы (`_autogen`, `.qt`, каталоги сборки) исключаются из обхода и из отчёта;
  устаревшие профили перед прогоном удаляются, чтобы `llvm-cov` не зависал.
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
- Git-hook'и доработаны: устранён конфликт с pre-commit framework (quality-проверки
  делегируются из `.githooks/pre-commit`, `pre-commit install` в репозитории не
  используется) и блокируется root-коммит в unborn `main` (`git symbolic-ref`).
- Значимая работа ведётся в отдельных git-worktree (`scripts/new-worktree.sh`,
  `scripts/rm-worktree.sh`, `docs/WORKTRES.md`): основной чекаут остаётся стабильным,
  его можно собирать и запускать параллельно с работой агента.
- Минимальная версия Qt повышена с 6.4.2 до **6.8.3**. Qt ставится per-user через
  `aqtinstall` в `~/Qt/6.8.3/gcc_64` и находится через `CMAKE_PREFIX_PATH`; system-пакеты
  дистрибутива (`qt6-base-dev`, `qt6-declarative-dev`) больше не используются.
- CI: все стадии устанавливают Qt 6.8.3 через `aqtinstall` и передают `CMAKE_PREFIX_PATH`;
  состав стадий не изменился.
- `README.md` и `conanfile.py`: инструкции по установке и сборке приведены к новому
  способу получения Qt.
