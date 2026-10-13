#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_build=$(mktemp -d "${TMPDIR:-/tmp}/chameleon-host-tests.XXXXXX")
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
c++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -I"$project_dir/main" "$project_dir/main/chameleon_state.cpp" \
  "$project_dir/tests/chameleon_state_test.cpp" -o "$test_build/chameleon_state_test"
"$test_build/chameleon_state_test"
