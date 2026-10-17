# Изоляция через git worktree

Значимая работа ведётся в отдельном **git-worktree** — втором рабочем каталоге того же
репозитория с отдельной веткой. Так основной чекаут остаётся стабильным: его можно
собирать и запускать (например, на `main`), пока агент работает в своём worktree.

## Раскладка

```text
<родитель>/
├── qt_audio_assistent/                    # основной чекаут — держим на main
└── qt_audio_assistent-worktrees/
    ├── feature-app-controller/            # задача 1
    └── fix-window-visibility/             # задача 2
```

- Общий `.git` — один репозиторий: история и ветки общие.
- Отдельные рабочие каталоги и **отдельные build-каталоги** (`build-debug`, `build-release`, …).
- Один worktree — одна ветка (git не даст вычекнуть одну ветку дважды).

## Создание и удаление

```sh
scripts/new-worktree.sh feature/my-feature   # от актуальной origin/main
scripts/rm-worktree.sh  feature/my-feature   # каталог; ветка остаётся
```

`new-worktree.sh` делает `git fetch`, создаёт worktree в
`../qt_audio_assistent-worktrees/<ветка>` и включает git-hook'и внутри него.

## Рабочий цикл

1. Основной чекаут — на `main`; обновляй `git -C <main> pull --ff-only`.
2. Под каждую значимую задачу — свой worktree от `origin/main`.
3. В worktree обычный цикл: тест → минимальное изменение → `scripts/ci-check.sh` →
   commit → push → PR.
4. После мержа PR worktree удаляется.

## Подводные камни

- **Ресурсы.** Два `cmake --build -j$(nproc)` одновременно загрузят CPU; собирайте по
  очереди или уменьшите `-j`.
- **Первый build.** В новом worktree сборка с нуля — CMake-кэш не переносится между worktree.
- **Хуки.** `core.hooksPath=.githooks` общий для репозитория, а относительный путь
  резолвится от корня каждого worktree, поэтому `.githooks/` должен присутствовать в ветке.
  Активация — `scripts/setup-git-hooks.sh` (вызывается `new-worktree.sh`).
- **Не переключай ветку** в основном чекауте на ветку, уже открытую в worktree.
