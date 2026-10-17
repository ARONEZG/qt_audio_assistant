# Правила C++ кода в src/

Действуют, когда агент читает или изменяет файлы в каталоге `src/`.
Правила из корневого `AGENTS.md` остаются обязательными; этот файл дополняет их.

## Zero-warning policy

Предупреждение = ошибка. Для Clang обязательно:

```text
-Wall -Wextra -Wpedantic -Werror
-Weverything
-Wconversion -Wsign-conversion -Wimplicit-fallthrough
-Wcast-align -Wcast-qual -Wdouble-promotion -Wformat=2
-Wnull-dereference -Woverloaded-virtual -Wshadow-all
-Wswitch-enum -Wundef -Wunreachable-code -Wunused
```

`-Wno-*` разрешены только: минимально на цель, с комментарием причины, не скрывая
диагностику в собственном коде. Перечислять в отчёте. Запрещены: `-w`, массовые
`-Wno-error=*`, понижение уровня предупреждений, пометка своего кода как system headers.
Внешние/Qt-заголовки — через `SYSTEM` include directories.

## STL-контейнеры (запрет Qt-контейнеров)

Строго запрещены: `QList`, `QVector`, `QMap`, `QHash`, `QSet`, `QQueue`, `QStack`, `QPair`,
`QStringList`, `QVariantList`, `QVariantMap`, `QVariantHash` и aliases.

Разрешены: `std::vector` (по умолчанию), `std::array`, `std::deque`, `std::list`,
`std::map`, `std::unordered_map`, `std::set`, `std::unordered_set`, `std::queue`,
`std::stack`, `std::pair`, `std::span`, `std::ranges`.

Qt value types разрешены: `QString`, `QByteArray`, `QUrl`, `QVariant`, `QJsonValue`,
`QObject`, model/view, signal/slot API.

При неизбежном Qt-контейнере от API:
- изолировать в тонком adapter;
- немедленно преобразовать в STL;
- не хранить в полях и не возвращать из project-owned контрактов;
- покрыть тестом преобразования.

## Форматирование и именование

- `clang-format --dry-run --Werror`, корневой `.clang-format` без переопределений.
- `PascalCase` — типы, `camelCase` — функции/локальные переменные,
  `kPascalCase` — константы, `camelCase_` — приватные поля.
- Без венгерской нотации.

## Статический анализ

Запускать `scripts/static-analysis.sh` после каждого изменения C++.
Обязательны: clang-format, clang-tidy, clazy (level0,level1,level2 для нового кода).
Состав инструментов и команды: `docs/STATIC_ANALYSIS_TOOLS.md`.
