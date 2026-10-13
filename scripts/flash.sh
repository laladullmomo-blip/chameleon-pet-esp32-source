#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT=""
BAUD="460800"
LIST_PORTS=false

usage() {
    cat <<'EOF'
用法：scripts/flash.sh --list-ports
      scripts/flash.sh --port /dev/cu.usbmodemXXXX [--baud 460800]
使用 dist/flash_args 按分区刷写；保留 NVS 宠物存档，不执行全片擦除。
串口必须明确指定。刷写结束后退出，不自动打开串口监视器。
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            PORT="$2"; shift 2 ;;
        --baud)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            BAUD="$2"; shift 2 ;;
        --list-ports) LIST_PORTS=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) printf '未知参数：%s\n' "$1" >&2; usage; exit 2 ;;
    esac
done
[[ "${BAUD}" =~ ^[0-9]+$ ]] || { printf '波特率必须为整数。\n' >&2; exit 2; }
if ! ${LIST_PORTS} && [[ -z "${PORT}" ]]; then
    usage
    exit 2
fi

if [[ -n "${IDF_PYTHON_ENV_PATH:-}" && -x "${IDF_PYTHON_ENV_PATH}/bin/python" ]]; then
    PET_PYTHON="${IDF_PYTHON_ENV_PATH}/bin/python"
elif [[ -z "${PET_PYTHON:-}" ]]; then
    for LOCAL_PY in "${PROJECT_ROOT}"/.tools/espressif/python_env/idf*_env/bin/python; do
        if [[ -x "${LOCAL_PY}" ]]; then PET_PYTHON="${LOCAL_PY}"; break; fi
    done
    PET_PYTHON="${PET_PYTHON:-$(command -v python3)}"
fi

if ${LIST_PORTS}; then
    "${PET_PYTHON}" -m serial.tools.list_ports -v
    exit 0
fi

"${PET_PYTHON}" - "${PORT}" <<'PY'
import sys
from serial.tools import list_ports

port = sys.argv[1]
devices = {item.device for item in list_ports.comports()}
if port not in devices:
    raise SystemExit(f'未发现指定串口：{port}。请先运行 scripts/flash.sh --list-ports。')
PY
[[ -f "${PROJECT_ROOT}/dist/flash_args" ]] || {
    printf '未找到 dist/flash_args，请先运行 scripts/build.sh。\n' >&2
    exit 1
}
"${PET_PYTHON}" "${PROJECT_ROOT}/scripts/verify_flash_inputs.py" "${PROJECT_ROOT}/dist"
cd "${PROJECT_ROOT}/dist"
"${PET_PYTHON}" -m esptool --chip esp32s3 --port "${PORT}" --baud "${BAUD}" \
    --before default-reset --after hard-reset write-flash @flash_args
