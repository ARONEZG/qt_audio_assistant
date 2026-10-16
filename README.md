# Audio Assistant

Минимальный мультиплатформенный desktop-каркас приложения на C++20, Qt 6 и QML.

**Текущий статус:** только собираемый GUI-каркас. Функции пока не реализованы.

## Требования

- Компилятор **Clang** 18+ (проверено с LLVM 18.1.3)
- **CMake** 3.26+
- **Qt 6** (проверено с Qt 6.4.2):
  - dev-пакеты: `qt6-base-dev`, `qt6-declarative-dev`
  - QML-модули: `qml6-module-qtquick`, `qml6-module-qtquick-controls`,
    `qml6-module-qtquick-layouts`, `qml6-module-qtquick-templates`,
    `qml6-module-qtquick-window`
- **Git** 2.x (опционально)

### Ubuntu / Debian

```bash
sudo apt install clang cmake ninja-build git \
    qt6-base-dev qt6-declarative-dev \
    qml6-module-qtquick qml6-module-qtquick-controls \
    qml6-module-qtquick-layouts qml6-module-qtquick-templates \
    qml6-module-qtquick-window
```

## Сборка

Ninja — предпочтительный генератор (если установлен):

```bash
CC=clang-18 CXX=clang++-18 cmake -S . -B build -G Ninja
cmake --build build --parallel
```

Если Ninja недоступен, используйте генератор по умолчанию (Unix Makefiles):

```bash
CC=clang-18 CXX=clang++-18 cmake -S . -B build
cmake --build build --parallel
```

Проект требует Clang; конфигурация завершится ошибкой с другим компилятором.

## Тесты

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

## Запуск

```bash
./build/audio-assistant
```

## Проверенные окружения

- **Linux (Ubuntu 24.04, x86_64)**: проверено полностью — Debug/Release, ASan+UBSan,
  clang-format, clang-tidy, Qt Test. Отдельной графической сессии во время проверки не было;
  GUI запускался только с `QT_QPA_PLATFORM=offscreen`.
- **Windows**: не проверено.
- **macOS**: не проверено.
- **Android**: не входит в текущий этап.

## Не реализовано

- AppController и архитектура сервисов
- Захват микрофона и системного аудио
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
4. Qt Multimedia + аудиозахват
5. Обработка последних 30 секунд аудио
6. Реальные сервисы и обработка ошибок
7. Системный аудиовыход + глобальная горячая клавиша
8. CI-матрица на Linux, Windows, macOS
