# Audio Assistant

Минимальный мультиплатформенный desktop-каркас приложения на C++20, Qt 6 и QML.

**Текущий статус:** собираемый GUI-каркас и доменные контракты захвата аудио.
Функции захвата, буферизации и транскрибации пока не реализованы.

## Требования

- Компилятор **Clang** 18+ (проверено с LLVM 18.1.3)
- **CMake** 3.26+
- **Qt 6.8.3** — минимальная и проверенная версия. Qt ставится per-user через
  `aqtinstall` в `~/Qt`; dev-пакеты дистрибутива (`qt6-base-dev`, `qt6-declarative-dev`)
  больше не используются: в Ubuntu 24.04 они дают Qt 6.4.2, чего недостаточно.
  - нужны архивы `qtbase` и `qtdeclarative` (в Qt 6 Qt Quick Controls 2 входит
    в `qtdeclarative`, отдельного архива `qtquickcontrols2` нет)
- **spdlog** 1.14+ — зависимость логирования, управляется через Conan
  (`spdlog/1.14.1` в `conanfile.py`). Системный `libspdlog-dev` в Ubuntu 24.04
  (spdlog 1.12) не подходит: CMake требует `find_package(spdlog 1.14 REQUIRED)`.
- **miniaudio** 0.11.25 — зависимость захвата аудио, управляется через Conan
  (`miniaudio/0.11.25` в `conanfile.py`; лицензия Unlicense/MIT-0). Header-only,
  без транзитивных зависимостей. Версия — единственный источник правды; решение
  и матрица покрытия — `docs/ADR-audio-capture.md`.
- **Conan** 2.x — обязателен: через него подтягиваются `spdlog`, **miniaudio**
  и **LLMQore v0.8.0** (MIT). LLMQore нет в ConanCenter, поэтому он собирается из
  in-repo рецепта `recipes/llmqore`, который скачивает исходники с `github.com`
  (доступ к сети нужен на этапе `conan install`, а не конфигурации CMake).

### Установка Qt 6.8.3 (aqtinstall, без sudo)

```bash
pipx install aqtinstall   # или: python3 -m pip install --user aqtinstall
aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O ~/Qt \
    --archives icu qtbase qtdeclarative qtsvg qttools qttranslations qtwayland
```

### Системные пакеты (Ubuntu / Debian)

```bash
sudo apt install clang cmake ninja-build git
```

## Сборка

Ninja — предпочтительный генератор (если установлен):

```bash
export CMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64
CC=clang-18 CXX=clang++-18 cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"
cmake --build build --parallel
```

Если Ninja недоступен, используйте генератор по умолчанию (Unix Makefiles):

```bash
CC=clang-18 CXX=clang++-18 cmake -S . -B build \
    -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"
cmake --build build --parallel
```

Путь к Qt задаётся только на этапе конфигурации через `CMAKE_PREFIX_PATH`;
в `CMakeLists.txt` он не зашит.

Проект требует Clang; конфигурация завершится ошибкой с другим компилятором.
Все зависимости (`spdlog`, `miniaudio`, LLMQore) поставляются через Conan (см. ниже).

## Захват аудио: зависимость и покрытие платформ

Для захвата аудио подключён **miniaudio** (ConanCenter, header-only). Логика захвата
пока не реализована — подготовлена только зависимость и smoke-тест компиляции/линковки.
Матрица покрытия (подробнее — `docs/ADR-audio-capture.md`):

| Платформа | Микрофон | Системный звук (loopback) |
|---|---|---|
| Windows | Да (WASAPI) | Да, нативно (`ma_device_type_loopback`, WASAPI) |
| Linux | Да (PulseAudio/PipeWire-Pulse, ALSA, JACK) | Да, через «monitor»-источник PulseAudio/PipeWire-Pulse |
| macOS | Да (CoreAudio) | Нет — gap (см. ниже) |

- **Linux, runtime-требование:** для системного звука нужен работающий звуковой сервер
  PulseAudio или PipeWire с PipeWire-Pulse и включённым «monitor»-источником на нужном
  устройстве вывода. Нативный loopback miniaudio для Linux-backend'ов не поддерживает.
- **macOS gap:** нативный захват системного звука miniaudio не покрывает. Будущее решение —
  отдельный platform-модуль на **ScreenCaptureKit** (macOS 13+) или **CATapDescription**
  (`AudioHardwareCreateProcessTap`, macOS 14.2+); до его появления macOS loopback
  отсутствует и должен явно показываться как недоступная функция.

## Сборка с Conan (рекомендуемый способ)

Conan 2.x управляет зависимостями: `spdlog/1.14.1` и `miniaudio/0.11.25` берутся
из ConanCenter, `llmqore/0.8.0` собирается из in-repo рецепта `recipes/llmqore`
(см. `conanfile.py`).
Qt ставится отдельно через `aqtinstall` и находится по `CMAKE_PREFIX_PATH`
(зависимость Qt через ConanCenter пока отключена). Требуется `conan` 2.x в `PATH`.

```bash
# Профиль зафиксирован в репозитории: profiles/clang18 (Linux x86_64, Clang 18, Debug)
conan export recipes/llmqore
conan install . -of build/conan --build=missing \
    -pr:b profiles/clang18 -pr profiles/clang18

CC=clang-18 CXX=clang++-18 cmake -S . -B build \
    -DCONAN_BUILD_DIR=build/conan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64 \
    -G Ninja
cmake --build build --parallel
```

Компилятор всё равно задаётся явно через `CC`/`CXX`: Conan toolchain подключается
через `include()` уже после `project()`, поэтому он не управляет выбором компилятора.
Пакеты, собираемые Conan (`llmqore`), компилируются Clang-ом — это задаёт conf
`tools.build:compiler_executables` в `profiles/clang18`.

## Тесты

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

## Запуск

```bash
./build/audio-assistant
```

### Сетевой провайдер (OpenAI-совместимый)

Помимо offline-заглушки приложение предлагает сетевой провайдер, работающий с любым
OpenAI-совместимым chat-completions endpoint. Endpoint и модель можно задать в панели
«Provider settings» в окне приложения и сохранить; они пишутся в
`provider.env` в каталоге пользовательской конфигурации (`QStandardPaths::AppConfigLocation`).
Их также можно передать через окружение — переменные окружения имеют приоритет над
сохранённым файлом:

```bash
POLZA_BASE_URL="https://<host>/api/v1" \
POLZA_API_KEY="<secret>" \
POLZA_MODEL="<model>" \
./build/audio-assistant
```

`POLZA_API_KEY` — секрет: он не хранится в репозитории, не пишется в конфиг-файл и не
попадает в логи; в UI он показан маскированным полем и живёт только в памяти сессии
(поле write-only — значение ключа наружу в QML не отдаётся, ключ передаётся в C++
только по нажатию «Save»). Сохранение endpoint/model не требует ключа: ключ можно
добавить позже (в панели или через окружение); пустое поле ключа сохраняет уже
заданный ключ. Если ключ или endpoint не заданы, приложение всё равно запускается, но
при отправке сообщения провайдер честно сообщает об ошибке (`kAuthentication`/`kUnknown`)
вместо падения. Пустые или некорректные значения endpoint/model отклоняются при
сохранении с понятным сообщением.

## Модули

- `audio-assistant-domain` (`src/domain/`) — домен LLM-чата: сообщения, провайдеры, чаты.
- `audio-assistant-audio` (`src/audio/`) — аудиоподсистема. Её доменные контракты
  захвата не зависят от Qt и платформенного кода:
  - `AudioFormat` — неизменяемый тип формата PCM (частота дискретизации, число каналов,
    формат выборки); инварианты (частота > 0, каналы > 0) проверяются при создании;
  - `AudioSource` — ролевой интерфейс источника аудио (`start`/`stop`/`format`);
  - `AudioSink` — порт, в который источник пишет декодированный PCM
    (`write`, non-blocking: без аллокаций и блокировок в real-time колбэке).
  Канонический формат захвата — **16 кГц, mono, f32** (`[-1, 1]`). Реализация захвата
  (miniaudio) и кольцевой буфер добавляются поверх этих контрактов отдельными задачами.

## Сборка через CMake-пресеты

`CMakePresets.json` в корне содержит пресеты `audio-assistant-debug` и
`audio-assistant-release`: они задают Ninja, Clang 18 и каталог сборки `build/<тип>`,
а `conan install` выполняется автоматически при первой конфигурации (если `conan`
есть в `PATH`).

```bash
cmake --preset audio-assistant-debug
cmake --build --preset audio-assistant-debug
ctest --preset audio-assistant-debug --output-on-failure
```

## Разработка из VS Code

Готовая конфигурация рабочего пространства — в `.vscode/`: сборка по пресетам, запуск,
отладка (`cppdbg` + gdb/lldb), `clang-format` и `clang-tidy` (по базе
`build/debug/compile_commands.json`). Расширения перечислены в `.vscode/extensions.json`.
Пошаговая инструкция и разбор частых проблем — `docs/VSCODE_SETUP.md`.

## Проверенные окружения

- **Linux (Ubuntu 24.04, x86_64)**: проверено полностью — Debug/Release, ASan+UBSan,
  clang-format, clang-tidy, Qt Test. Qt 6.8.3 установлен через `aqtinstall`
  в `~/Qt/6.8.3/gcc_64`; LLMQore v0.8.0 подключается через Conan
  (`recipes/llmqore`) и используется сетевым провайдером
  `OpenAiCompatibleProvider` (`LLMQore::OpenAIClient` за доменным `Provider`).
  `miniaudio/0.11.25` подключается через ConanCenter: собирается один TU с
  `MINIAUDIO_IMPLEMENTATION` (библиотека `audio-assistant-audio`), заголовок
  включён как SYSTEM; API захвата не используется, добавлен только smoke-тест
  линковки (`tst_audio_smoke`).
  Отдельной графической сессии
  во время проверки не было; GUI запускался только с `QT_QPA_PLATFORM=offscreen`.
  Сборка через Conan проверена только на Linux/Ninja; конфигурация с другими генераторами
  (Visual Studio, Xcode) обрабатывается defensively, но не проверялась.
- **Windows**: не проверено.
- **macOS**: не проверено.
- **Android**: не входит в текущий этап.

## Не реализовано

- AppController и архитектура сервисов
- Захват микрофона и системного аудио (контракты `AudioSource`/`AudioSink` и
  зависимость `miniaudio` подключены, логика захвата не реализована)
- Кольцевой аудиобуфер
- Транскрибация
- Интеграция с LLM / сетевые запросы
- Горячие клавиши
- Выбор сервисов
- Многопоточность
- Функции сверх минимального QML-отображения

## План следующих этапов

1. Добавить `AppController`, свойства Qt и связь C++ с QML
2. Интерфейсы транскрибации и LLM (без сетевых реализаций)
3. `AudioBuffer` + модульные тесты
4. Захват микрофона через `miniaudio` за узким интерфейсом
5. Системный звук (loopback): Windows — нативно (WASAPI), Linux — monitor-источник
   PulseAudio/PipeWire-Pulse
6. macOS loopback: отдельный platform-модуль (ScreenCaptureKit 13+ / CATap 14.2+)
7. Обработка последних 30 секунд аудио
8. Реальные сервисы и обработка ошибок
9. Глобальная горячая клавиша
10. CI-матрица на Linux, Windows, macOS
