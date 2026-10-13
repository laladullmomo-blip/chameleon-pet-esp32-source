#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_build=$(mktemp -d "${TMPDIR:-/tmp}/pet2-host-tests.XXXXXX")
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -I"$project_dir/tests/stubs" -I"$project_dir/main" \
  -c "$project_dir/main/pet_frames.c" -o "$test_build/pet_frames.o"
for tick_ms in 1 10; do
  c++ -std=c++17 -Wall -Wextra -Werror -Wno-unused-variable \
    -fsanitize=undefined -fno-sanitize-recover=all -DHOST_TICK_MS="$tick_ms" \
    -I"$project_dir/tests/stubs" -I"$project_dir/main" \
    "$project_dir/main/pet_state.cpp" "$project_dir/main/pet_save.cpp" \
    "$project_dir/tests/pet_core_test.cpp" "$test_build/pet_frames.o" \
    -o "$test_build/pet_core_test"
  "$test_build/pet_core_test"
done
