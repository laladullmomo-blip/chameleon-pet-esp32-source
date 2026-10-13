#!/bin/sh
set -eu
ch_project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ch_hardware_build=$(mktemp -d "${TMPDIR:-/tmp}/chameleon-hardware-checks.XXXXXX")
trap 'rm -rf "$ch_hardware_build"' EXIT HUP INT TERM
c++ -std=c++17 -O1 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  "$ch_project/tests/chameleon_hardware_schedule_test.cpp" \
  -o "$ch_hardware_build/hardware_schedule_checks"
"$ch_hardware_build/hardware_schedule_checks"
