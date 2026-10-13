#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ -n "${PET_PREVIEW_PYTHON:-}" ]]; then
    [[ -x "${PET_PREVIEW_PYTHON}" ]] || {
        printf '指定的 Python 不可执行：%s\n' "${PET_PREVIEW_PYTHON}" >&2
        exit 1
    }
else
    PET_PREVIEW_PYTHON="$(command -v python3 || true)"
    if [[ -z "${PET_PREVIEW_PYTHON}" ]]; then
        PET_BUNDLED_PYTHON="${HOME}/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3"
        if [[ -x "${PET_BUNDLED_PYTHON}" ]]; then
            PET_PREVIEW_PYTHON="${PET_BUNDLED_PYTHON}"
        else
            for PET_LOCAL_PYTHON in "${PROJECT_ROOT}"/.tools/espressif/python_env/idf*_env/bin/python; do
                if [[ -x "${PET_LOCAL_PYTHON}" ]]; then
                    PET_PREVIEW_PYTHON="${PET_LOCAL_PYTHON}"
                    break
                fi
            done
        fi
    fi
fi

[[ -n "${PET_PREVIEW_PYTHON}" ]] || {
    printf '没有找到 Python 3。可设置 PET_PREVIEW_PYTHON 为现有 Python 3 的完整路径。\n' >&2
    exit 1
}

exec "${PET_PREVIEW_PYTHON}" "${PROJECT_ROOT}/scripts/preview_server.py" "$@"
