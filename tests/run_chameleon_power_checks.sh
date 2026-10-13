#!/bin/sh
set -eu
ch_project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ch_power_build=$(mktemp -d "${TMPDIR:-/tmp}/chameleon-power-checks.XXXXXX")
trap 'rm -rf "$ch_power_build"' EXIT HUP INT TERM
c++ -std=c++17 -O1 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -I"$ch_project/tests/chameleon_power/stubs" -I"$ch_project/tests/stubs" -I"$ch_project/main" \
  "$ch_project/main/chameleon_power.cpp" "$ch_project/tests/chameleon_power_test.cpp" \
  -o "$ch_power_build/power_checks"
for ch_power_case in normal create-failure acquire-failure configure-failure transition-failure; do
  "$ch_power_build/power_checks" "$ch_power_case"
done
