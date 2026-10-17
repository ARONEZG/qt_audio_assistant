#!/usr/bin/env bash
# Создаёт изолированный git-worktree от актуальной origin/main.
#
# Использование:
#   scripts/new-worktree.sh <feature|fix|chore>/<name>
#
# Каталог: <сосед репозитория>/<repo>-worktrees/<ветка, где '/' -> '-'>
set -euo pipefail

usage() {
  echo "usage: $(basename "$0") <feature|fix|chore>/<name>" >&2
  exit 2
}

[ "$#" -eq 1 ] || usage
branch="$1"

case "$branch" in
  feature/*|fix/*|chore/*) ;;
  *)
    echo "error: ветка должна начинаться с feature/, fix/ или chore/." >&2
    usage
    ;;
esac

if [[ "$branch" == */ ]]; then
  echo "error: имя ветки не должно заканчиваться на '/'." >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

# Корень основного чекаута (общий для всех worktree).
common_dir="$(git -C "$repo_root" rev-parse --path-format=absolute --git-common-dir)"
main_root="$(dirname "$common_dir")"
worktrees_root="$(dirname "$main_root")/$(basename "$main_root")-worktrees"

name="${branch//\//-}"
target="$worktrees_root/$name"

if git -C "$repo_root" show-ref --verify --quiet "refs/heads/$branch"; then
  echo "error: ветка '$branch' уже существует." >&2
  exit 1
fi
if [ -e "$target" ]; then
  echo "error: каталог '$target' уже существует." >&2
  exit 1
fi

mkdir -p "$worktrees_root"
git -C "$repo_root" fetch origin --quiet
git -C "$repo_root" worktree add -b "$branch" "$target" origin/main

if [ -x "$target/scripts/setup-git-hooks.sh" ]; then
  (cd "$target" && scripts/setup-git-hooks.sh)
fi

echo
echo "worktree: $target"
echo "ветка:    $branch (от origin/main)"
echo "далее:    cd \"$target\""
