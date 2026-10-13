#!/bin/sh
set -eu
ch_project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ch_games_build=$(mktemp -d "${TMPDIR:-/tmp}/chameleon-games-checks.XXXXXX")
trap 'rm -rf "$ch_games_build"' EXIT HUP INT TERM
c++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -I"$ch_project/main" "$ch_project/main/chameleon_games.cpp" \
  "$ch_project/tests/chameleon_games_test.cpp" -o "$ch_games_build/chameleon_games_test"
"$ch_games_build/chameleon_games_test"
