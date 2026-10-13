#!/usr/bin/env bash
set -euo pipefail
ch_project=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ch_project"
ch_cmake=$(command -v cmake || true)
if [[ -z "$ch_cmake" ]]; then
  for ch_candidate in "$ch_project"/.tools/espressif/tools/cmake/*/CMake.app/Contents/bin/cmake; do
    if [[ -x "$ch_candidate" ]]; then ch_cmake="$ch_candidate"; break; fi
  done
fi
if [[ -z "$ch_cmake" || ! -d managed_components/lvgl__lvgl ]]; then
  echo '请先完成官方 ESP-IDF 依赖准备，再运行原生 LVGL 检查。' >&2
  exit 1
fi
ch_ui_build="${TMPDIR:-/tmp}/chameleon-native-ui-checks"
rm -rf "$ch_ui_build"
trap 'rm -rf "$ch_ui_build"' EXIT
"$ch_cmake" -S tests/chameleon_ui -B "$ch_ui_build" -DCMAKE_BUILD_TYPE=Release
"$ch_cmake" --build "$ch_ui_build" -j 8
for ch_mode in standby standby-menu weather-city flow sick hungry sleep manualscene keys cleanup daily buttons-sound migration migration-v2 invalidsave pet pet-partial pet-te-buffer pet-heart-baby pet-heart-teen pet-heart-adult sick-stage-baby sick-stage-teen sick-stage-adult growth-teen growth-adult growth-paused fresh-baby round-baby round-teen round-adult capture-baby capture-teen capture-adult growth-dark growth-feeding growth-sleep colors-baby colors-teen colors-adult poop-perch power-touch queued-touch world-first-frame care-info rain-shelter migration-v3 migration-v4 games-baby games-teen games-adult game-clock bond-ui encounter-dawn encounter-night encounter-rainbow encounter-defer anniversary-21 anniversary-30 anniversary-sleep anniversary-dark anniversary-feeding anniversary-game anniversary-backlog alarm-config alarm-dark-muted alarm-controls alarm-snooze alarm-timeout alarm-invalid alarm-game audio-config audio-invalid audio-save-failure forest-wishes forest-autonomy forest-workshop forest-calendar forest-invalid forest-save-failure forest-game forest-scenes forest-install; do
  "$ch_ui_build/chameleon_ui_checks" "$ch_project/artifacts/chameleon-ui" "$ch_mode"
done
