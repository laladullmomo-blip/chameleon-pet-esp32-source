#!/bin/sh
set -eu
ch_project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ch_forest_build=$(mktemp -d "${TMPDIR:-/tmp}/chameleon-forest-checks.XXXXXX")
trap 'rm -rf "$ch_forest_build"' EXIT HUP INT TERM
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=undefined -fno-sanitize-recover=all \
 -I"$ch_project/main" "$ch_project/main/chameleon_forest.cpp" \
 "$ch_project/tests/chameleon_forest_test.cpp" -o "$ch_forest_build/checks"
"$ch_forest_build/checks"
