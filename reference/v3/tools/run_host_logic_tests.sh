#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STUBS="$ROOT/tools/host_stubs"
mapfile -t INCLUDE_DIRS < <(find "$ROOT/components" -type d -name include | sort)
INC=("-I$STUBS" "-I$ROOT/components/nocti_v3_adc" "-I$ROOT/main")
for dir in "${INCLUDE_DIRS[@]}"; do INC+=("-I$dir"); done
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
clang -std=gnu11 -Wall -Wextra -Werror -Wformat=2 -ffunction-sections \
    "${INC[@]}" -c "$ROOT/components/nocti_v3_stream/nocti_v3_stream.c" \
    -o "$TMP/stream.o"
clang -std=gnu11 -Wall -Wextra -Werror -Wformat=2 -ffunction-sections \
    "${INC[@]}" -c "$ROOT/tools/test_stream_compat.c" -o "$TMP/test.o"
clang -Wl,--gc-sections "$TMP/test.o" "$TMP/stream.o" -o "$TMP/test_stream"
"$TMP/test_stream"
