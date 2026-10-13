#!/bin/sh
set -eu
ch_project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ch_alarm_build=$(mktemp -d "${TMPDIR:-/tmp}/chameleon-alarm-checks.XXXXXX")
trap 'rm -rf "$ch_alarm_build"' EXIT HUP INT TERM
c++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -I"$ch_project/main" "$ch_project/main/chameleon_alarm.cpp" \
  "$ch_project/tests/chameleon_alarm_test.cpp" -o "$ch_alarm_build/chameleon_alarm_test"
"$ch_alarm_build/chameleon_alarm_test"
