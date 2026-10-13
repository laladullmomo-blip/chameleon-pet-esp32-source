#!/usr/bin/env bash
set -euo pipefail
pet_project=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$pet_project"
pet_cmake=$(command -v cmake || true)
if [[ -z "$pet_cmake" ]]; then
  for pet_candidate in "$pet_project"/.tools/espressif/tools/cmake/*/CMake.app/Contents/bin/cmake; do
    if [[ -x "$pet_candidate" ]]; then pet_cmake="$pet_candidate"; break; fi
  done
fi
if [[ -z "$pet_cmake" || ! -d managed_components/lvgl__lvgl ]]; then
  echo '请先运行 scripts/build.sh 下载官方依赖与编译工具，或先激活 ESP-IDF。' >&2
  exit 1
fi
pet_ui_build=$(mktemp -d /tmp/round-pet-ui.XXXXXX)
trap 'rm -rf "$pet_ui_build"' EXIT
"$pet_cmake" -S tests/ui -B "$pet_ui_build" -DCMAKE_BUILD_TYPE=Release
"$pet_cmake" --build "$pet_ui_build" -j 8
"$pet_ui_build/ui_checks" "$pet_project/artifacts/ui"
