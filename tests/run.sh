#!/bin/sh
# Host test of BEATPEN's sounds and looper: WAVs in build/render-out/, and a fuzz.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/render-out
${CC:-cc} -O1 -g -fsanitize=undefined -fno-sanitize-recover -Wall -Isrc -Iplatform \
  -DRING_PUBLISH\(\)= tests/render.c src/sound.c src/seq.c -lm -o build/render
build/render build/render-out
