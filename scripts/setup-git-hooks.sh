#!/usr/bin/env bash
# Включает общие git-hook'и репозитория (core.hooksPath = .githooks).
# Запускать один раз на клон: scripts/setup-git-hooks.sh
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
git -C "$repo_root" config core.hooksPath .githooks
echo "core.hooksPath = .githooks — git-hook'и включены."

# Предупреждение: артефакт `pre-commit install` игнорируется из-за core.hooksPath.
# Проверяем дефолтный каталог хуков: `git rev-parse --git-path` учитывает
# core.hooksPath и вернул бы наш же .githooks/pre-commit.
common_dir="$(git -C "$repo_root" rev-parse --path-format=absolute --git-common-dir)"
hooks_pre_commit="$common_dir/hooks/pre-commit"
if [ -e "$hooks_pre_commit" ]; then
  echo "warning: найден $hooks_pre_commit (артефакт 'pre-commit install')." >&2
  echo "         Он игнорируется: core.hooksPath указывает на .githooks/," >&2
  echo "         quality-проверки делегируются из .githooks/pre-commit." >&2
fi
