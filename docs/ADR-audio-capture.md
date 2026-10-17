# ADR-0001: Зависимость для захвата аудио (микрофон + системный звук)

- **Статус:** принято (adopt)
- **Дата проверки фактов:** 2026-10-18
- **Связанные документы:** `docs/CODE_REUSE_POLICY.md`, `docs/REUSE_CANDIDATES.md`,
  `AUDIO_ASSISTANT_BUILD_MVP.md`, `conanfile.py`
- **Область:** только выбор и подготовка зависимости. Логика захвата здесь не реализуется.

## Контекст

Цель — захват аудио минимум из двух источников: **микрофон** и **системный звук
(loopback)**. Задача этого этапа — зафиксировать зависимость и способ её поставки
(строго через Conan), чтобы последующая реализация захвата строилась на уже
проверенной основе.

Собственная реализация потребовала бы отдельного кода под WASAPI (Windows), CoreAudio
(macOS) и PulseAudio/ALSA (Linux) плюс условную компиляцию в platform-модуле. Это
существенно дороже адаптера к готовой кроссплатформенной библиотеке, а также
противоречит `docs/CODE_REUSE_POLICY.md`.

## Решение

Использовать **miniaudio** как managed-зависимость Conan:
`miniaudio/0.11.25` из ConanCenter.

## Карточка кандидата (по `docs/CODE_REUSE_POLICY.md`)

### Решаемая задача и альтернатива собственной реализации

- Даёт: единый C-API для захвата/воспроизведения аудио, энумерацию устройств,
  callback-модель, backend'ы WASAPI/WinMM/DSound (Windows), CoreAudio (macOS),
  PulseAudio/ALSA/JACK (Linux), AAudio/OpenSL (Android).
- Альтернатива — своя реализация трёх платформенных backend'ов: отклонена как
  неоправданно дорогая для MVP (см. «Отказ от альтернатив» в конце).

### Точный источник, версия и дата проверки

- URL upstream: <https://github.com/mackron/miniaudio>
- URL пакета: <https://conan.io/center/recipes/miniaudio> (ConanCenter)
- Версия: **0.11.25** — самая свежая в ConanCenter на дату проверки
  (`conan list "miniaudio/*" -r=conancenter` показал 14 версий, максимум — `0.11.25`).
- Пин тега upstream: `0.11.25`; в заголовке `miniaudio - v0.11.25 - 2026-03-04`.
- Рецепт ConanCenter: revision `bb742f61dfe4946a1c8635c3b364bf31`;
  source tarball `https://github.com/mackron/miniaudio/archive/0.11.25.tar.gz`,
  sha256 `b900edcffe979816e2560a0580b9b1216d674b4f17fbadeca8f777a7f8ab0274`
  (из `recipes/miniaudio/all/conandata.yml`).
- Дата проверки: 2026-10-18.

### Лицензия и совместимость

- Лицензия пакета: **Unlicense / MIT-0** (двойное лицензирование, выбор за
  потребителем; так же указано в рецепте ConanCenter). Обе — permissive,
  совместимы с проприетарным и permissive лицензированием продукта.
- Attribution не требуется ни одной из лицензий; файл `LICENSE` поставляется в
  пакете (`<pkg>/licenses/LICENSE`).

### Поддержка платформ и Qt

- miniaudio не зависит от Qt (чистый C, header-only), поэтому совместим с любой
  версией Qt; целевая версия Qt проекта (6.8.3) здесь не ограничивает.
- Linux: поддержка есть (PulseAudio/PipeWire-Pulse, ALSA, JACK).
- Windows: поддержка есть (WASAPI, WinMM, DSound).
- macOS: поддержка есть (CoreAudio) для микрофона.
- Android: backend'ы есть (AAudio/OpenSL), но вне текущего скоупа проекта
  и не проверялись.
- **Проверено фактически только на Linux/Clang 18** (`conan install` +
  Debug/Release сборка и smoke-тест); Windows/macOS — заявленная поддержка
  upstream, не подтверждена в этом репозитории.

### Транзитивные зависимости, размер и влияние на сборку

- Транзитивные зависимости: **нет**. В Conan-графе miniaudio — лист.
- header-only (`header_only=True` по умолчанию в рецепте): один заголовок
  `miniaudio.h` (~95 864 строки, ~4.5 МБ) + опциональные extras под `include/extras`.
  Компилируется амальгамацией: implementation встраивается в TU с
  `#define MINIAUDIO_IMPLEMENTATION`.
- `package_id()` очищается при `header_only` → **единый package-id**
  (`da39a3ee5e6b4b0d3255bfef95601890afd80709`), т.е. бинарно-независимый пакет,
  скачивается без локальной сборки.
- Системные линковки, объявленные рецептом:
  - Linux/FreeBSD: `m`, `pthread`; дополнительно Linux: `dl`
    (используются при runtime-линковке backend'ов).
  - macOS: frameworks `CoreFoundation`, `CoreAudio`, `AudioUnit`
    и define `MA_NO_RUNTIME_LINKING=1`.
  - Windows: системные библиотеки рецептом не объявлены (используется
    runtime-линковка Win32 API).
- Влияние на сборку: увеличивает время компиляции одного TU с
  `MINIAUDIO_IMPLEMENTATION` (амальгамация ~95k строк); на время инкрементальной
  сборки остальных TU не влияет. Требует включения заголовка как **SYSTEM**,
  иначе чужой код уронит сборку под `-Weverything -Werror`.

### Тесты, CI, активность сопровождения

- Upstream: активный единственный мейнтейнер (mackron), регулярные релизы,
  собственный каталог `tests/` и примеры. Пакет в ConanCenter обновляется.
- Conan-пакет header-only: собственной сборки/тестов у пакета нет, CI-проверка
  сводится к упаковке заголовка. Дополнительного suit'а тестов пакет не даёт —
  проверка интеграции обеспечивается собственным smoke-тестом репозитория.
- Открытых критических issues, блокирующих использование, при обзоре не выявлено.

### Соответствие Clang/C++20, `-Werror`, санитайзерам

- Заголовок — C; собирается Clang 18. Под строгим профилем проекта
  (`-Weverything -Werror`) включается как SYSTEM-заголовок, а TU с
  implementation компилируется с ослабленными диагностиками только для чужого кода
  (см. шаги 3–4 серии).
- Код покрывается ASan/UBSan в составе наших тестовых целей; заголовок не
  отключает санитайзеры.

### Стоимость адаптера, обновлений и удаления

- Адаптер: тонкий обёрточный модуль за узким интерфейсом (шаг 3 серии);
  API захвата пока не пишется.
- Обновления: смена версии — правка одной строки `requires` в `conanfile.py`
  (Conan — единственный источник версии).
- Удаление: удалить `requires`, `find_package`, линковку и модуль-обёртку.

### Итог

**Adopt** — подключить через ConanCenter (`miniaudio/0.11.25`).

## Матрица покрытия «микрофон / loopback» по платформам

| Платформа | Микрофон | Системный звук (loopback) |
|---|---|---|
| **Windows** | Да — WASAPI (fallback WinMM/DSound) | Да, **нативно**: `ma_device_type_loopback` (только WASAPI; `ma_is_loopback_supported` возвращает `MA_TRUE` для WASAPI) |
| **Linux** | Да — PulseAudio/PipeWire-Pulse, ALSA, JACK | Да, **через «monitor»-источник** PulseAudio/PipeWire-Pulse как обычное capture-устройство. Нативный loopback-режим miniaudio для PulseAudio/ALSA/JACK не поддерживает |
| **macOS** | Да — CoreAudio | **Нет (gap)** — `ma_is_loopback_supported(coreaudio) == MA_FALSE` |
| **Android** | Backend'ы AAudio/OpenSL присутствуют; вне текущего скоупа | Вне скоупа |

Обоснование: в `miniaudio.h` функция `ma_is_loopback_supported()` возвращает
`MA_TRUE` только для `ma_backend_wasapi`; для всех остальных backend'ов (включая
`pulseaudio`, `alsa`, `coreaudio`) — `MA_FALSE`. Поэтому loopback на Linux берётся
не нативным режимом библиотеки, а через monitor-источник PulseAudio/PipeWire-Pulse
(это обычное устройство захвата с точки зрения miniaudio), а на macOS требуется
отдельное платформенное решение.

### macOS loopback gap и план его закрытия

Нативный захват системного звука на macOS miniaudio не покрывает. Будущее решение —
отдельный platform-specific модуль (по правилам `AGENTS.md` §3 — за узким
интерфейсом, отдельные файлы/цели, выбор через CMake), реализующий:

- **ScreenCaptureKit** (macOS 13+) — захват системного аудио вместе с видео/только аудио;
- **CATapDescription / AudioHardwareCreateProcessTap** (macOS 14.2+) — tap на выбранные
  процессы/устройство вывода, более точное и лёгкое решение.

До реализации macOS loopback остаётся **неподтверждённой/отсутствующей**
функциональностью и должна явно указываться как gap в документации и UI.

## Отказ от альтернатив (по одной строке)

- **Qt Multimedia**: даёт только захват микрофона (без loopback) и ставится не через
  Conan, а вместе с Qt — нарушает требование «все зависимости строго через Conan».
- **portaudio / rtaudio**: кроссплатформенный захват микрофона, но без режима
  loopback — не закрывают системный звук.
- **libpulse**: покрывает только Linux (плюс требует согласия на PipeWire-Pulse) —
  не кроссплатформенное решение.

## Последствия

- Плюсы: один header-only пакет с permissive-лицензией закрывает микрофон на трёх
  платформах и loopback на Windows/Linux; нет транзитивных зависимостей; Conan —
  единственный источник версии.
- Минусы/риски: время компиляции TU с `MINIAUDIO_IMPLEMENTATION`; macOS loopback
  требует отдельного platform-модуля; Windows/macOS-сборки пока не подтверждены
  в этом репозитории.
