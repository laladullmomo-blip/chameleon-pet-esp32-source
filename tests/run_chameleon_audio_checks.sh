#!/bin/sh
set -eu
ch_project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ch_audio_build=$(mktemp -d "${TMPDIR:-/tmp}/chameleon-audio-checks.XXXXXX")
trap 'rm -rf "$ch_audio_build"' EXIT HUP INT TERM
c++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -I"$ch_project/main" "$ch_project/tests/chameleon_audio_test.cpp" -o "$ch_audio_build/checks"
"$ch_audio_build/checks"
