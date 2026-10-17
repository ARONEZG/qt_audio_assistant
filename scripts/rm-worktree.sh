#!/usr/bin/env bash
# Удаляет git-worktree по имени ветки. Каталог только; ветка не удаляется.
#
# Использование:
#   scripts/rm-worktree.sh <feature|fix|chore>/<name> [--force]
#
# Без --force git откажется удалять worktree с незакоммиченными изменениями.
set -euo pipefail

usage() {
  echo "usage: $(basename "$0") <feature|fix|chore>/<name> [--force]" >&2
  exit 2
}

[ "$#" -ge 1 ] || usage
branch="$1"
force="${2:-}"

if [ -n "$force" ] && [ "$force" != "--force" ]; then
  echo "error: неизвестный аргумент '$force'." >&2
  usage
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

common_dir="$(git -C "$repo_root" rev-parse --path-format=absolute --git-common-dir)"
main_root="$(dirname "$common_dir")"
worktrees_root="$(dirname "$main_root")/$(basename "$main_root")-worktrees"

name="${branch//\//-}"
target="$worktrees_root/$name"

if [ ! -d "$target" ]; then
  echo "error: worktree не найден: $target" >&2
  exit 1
fi

if [ "$force" = "--force" ]; then
  git -C "$main_root" worktree remove --force "$target"
else
  git -C "$main_root" worktree remove "$target"
fi

echo "worktree удалён: $target"
echo "(локальная ветка '$branch' сохранена)"
