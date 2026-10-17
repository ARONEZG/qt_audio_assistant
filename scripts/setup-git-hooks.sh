#!/usr/bin/env bash
# Включает общие git-hook'и репозитория (core.hooksPath = .githooks).
# Запускать один раз на клон: scripts/setup-git-hooks.sh
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
git -C "$repo_root" config core.hooksPath .githooks
echo "core.hooksPath = .githooks — git-hook'и включены."
