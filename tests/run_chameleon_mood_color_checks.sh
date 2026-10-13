#!/usr/bin/env bash
set -euo pipefail
ch_project=$(cd -- "$(dirname -- "$0")/.." && pwd)
ch_mood_build=$(mktemp -d "${TMPDIR:-/tmp}/chameleon-mood-checks.XXXXXX")
trap 'rm -rf "$ch_mood_build"' EXIT
if [[ ! -d "$ch_project/managed_components/lvgl__lvgl" ]]; then
  echo '请先准备项目已固定版本的官方 LVGL 依赖，再运行心情颜色检查。' >&2
  exit 1
fi
cat > "$ch_mood_build/esp_heap_caps.h" <<'HEADER'
#pragma once
#include <cstddef>
constexpr int MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
void *ch_mood_test_allocate(size_t, int);
void ch_mood_test_free(void *);
inline void *heap_caps_malloc(size_t bytes, int caps) { return ch_mood_test_allocate(bytes, caps); }
inline void heap_caps_free(void *memory) { ch_mood_test_free(memory); }
HEADER
ch_include=(-DLV_CONF_INCLUDE_SIMPLE -I"$ch_project/tests/chameleon_ui" -I"$ch_project/managed_components/lvgl__lvgl" -I"$ch_project/main")
cc -std=c11 -O1 "${ch_include[@]}" -c "$ch_project/main/chameleon_assets.c" -o "$ch_mood_build/assets.o"
c++ -std=c++17 -O1 -Wall -Wextra -Werror -pedantic -fsanitize=undefined -fno-sanitize-recover=all \
  -I"$ch_mood_build" "${ch_include[@]}" "$ch_project/main/chameleon_mood_color.cpp" \
  "$ch_project/tests/chameleon_mood_color_test.cpp" "$ch_mood_build/assets.o" -o "$ch_mood_build/mood_checks"
"$ch_mood_build/mood_checks"
