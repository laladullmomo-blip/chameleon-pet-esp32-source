#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR=""

usage() {
    cat <<'EOF'
用法：scripts/build.sh [--build-dir /tmp/round-pet-build]
编译 ESP32-S3 固件并输出至 dist/。默认使用新的 ASCII 临时构建目录。
可指定已有构建目录进行增量编译；目录必须为无空格的 ASCII 绝对路径。
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            BUILD_DIR="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) printf '未知参数：%s\n' "$1" >&2; usage; exit 2 ;;
    esac
done

if [[ -z "${IDF_PATH:-}" || ! -f "${IDF_PATH}/tools/idf.py" ]]; then
    LOCAL_IDF="${PROJECT_ROOT}/.tools/esp-idf"
    [[ -f "${LOCAL_IDF}/export.sh" ]] || {
        printf '请安装并激活 ESP-IDF 6.0.2，或准备项目 .tools/esp-idf。\n' >&2
        exit 1
    }
    export IDF_TOOLS_PATH="${PROJECT_ROOT}/.tools/espressif"
    for LOCAL_PY in "${IDF_TOOLS_PATH}"/python_env/idf*_env/bin/python; do
        if [[ -x "${LOCAL_PY}" ]]; then
            export PATH="$(dirname "${LOCAL_PY}"):${PATH}"
            break
        fi
    done
    # ESP-IDF's activation script manages its own optional shell variables.
    set +u
    . "${LOCAL_IDF}/export.sh"
    set -u
fi

if [[ -n "${IDF_PYTHON_ENV_PATH:-}" && -x "${IDF_PYTHON_ENV_PATH}/bin/python" ]]; then
    PET_PYTHON="${IDF_PYTHON_ENV_PATH}/bin/python"
else
    PET_PYTHON="${PET_PYTHON:-$(command -v python3)}"
fi
IDF_COMMAND=("${PET_PYTHON}" "${IDF_PATH}/tools/idf.py")

if [[ -z "${BUILD_DIR}" ]]; then
    BUILD_DIR="$(mktemp -d /tmp/round-pet-build.XXXXXX)"
fi
[[ "${BUILD_DIR}" =~ ^/[A-Za-z0-9_./-]+$ ]] || {
    printf '构建目录必须为无空格的 ASCII 绝对路径：%s\n' "${BUILD_DIR}" >&2
    exit 2
}
mkdir -p "${BUILD_DIR}"
BUILD_DIR="$(cd "${BUILD_DIR}" && pwd)"
printf '构建目录：%s\n' "${BUILD_DIR}"
cd "${PROJECT_ROOT}"
"${IDF_COMMAND[@]}" -B "${BUILD_DIR}" build
"${IDF_COMMAND[@]}" -B "${BUILD_DIR}" merge-bin -o merged.bin

DIST_DIR="${PROJECT_ROOT}/dist"
mkdir -p "${DIST_DIR}/bootloader" "${DIST_DIR}/partition_table"
cp "${BUILD_DIR}/round_pet.bin" "${DIST_DIR}/round_pet.bin"
cp "${BUILD_DIR}/round_pet.elf" "${DIST_DIR}/round_pet.elf"
cp "${BUILD_DIR}/bootloader/bootloader.bin" "${DIST_DIR}/bootloader/bootloader.bin"
cp "${BUILD_DIR}/partition_table/partition-table.bin" "${DIST_DIR}/partition_table/partition-table.bin"
cp "${BUILD_DIR}/merged.bin" "${DIST_DIR}/merged.bin"
cp "${BUILD_DIR}/flash_args" "${DIST_DIR}/flash_args"
cp "${BUILD_DIR}/flasher_args.json" "${DIST_DIR}/flasher_args.json"
"${PET_PYTHON}" - "${DIST_DIR}" "${BUILD_DIR}" <<'PY'
import hashlib
import json
import pathlib
import subprocess
import sys
from datetime import datetime, timezone

dist = pathlib.Path(sys.argv[1])
build = pathlib.Path(sys.argv[2])
description = json.loads((build / 'project_description.json').read_text())
files = sorted(dist.rglob('*.bin')) + [dist / 'round_pet.elf', dist / 'flash_args']
(dist / 'SHA256SUMS').write_text(''.join(
    f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(dist)}\n'
    for path in files
))
metadata = {
    'project': description.get('project_name', 'round_pet'),
    'target': description.get('target', 'esp32s3'),
    'idf_version': description.get('idf_ver') or description.get('git_revision'),
    'app_version': description.get('project_version'),
    'build_directory': str(build),
    'built_at_utc': datetime.now(timezone.utc).isoformat(),
    'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
    'source_dirty': bool(subprocess.check_output(['git', 'status', '--porcelain'], text=True).strip()),
    'hardware_execution': 'NOT RUN',
}
(dist / 'build-info.json').write_text(json.dumps(metadata, indent=2) + '\n')
PY
printf '\n固件已生成：%s\n' "${DIST_DIR}"
printf '按分区刷写请运行：scripts/flash.sh --port /dev/cu.usbmodemXXXX\n'
