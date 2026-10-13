#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${PROJECT_ROOT}"
if ! ./scripts/preview.sh; then
    printf '\n启动失败，请查看上面的原因。按回车关闭窗口。\n'
    read -r PET_PREVIEW_REPLY
    exit 1
fi
