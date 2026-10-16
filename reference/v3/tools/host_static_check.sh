#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STUBS="$ROOT/tools/host_stubs"
mapfile -t INCLUDE_DIRS < <(find "$ROOT/components" -type d -name include | sort)
INC=("-I$STUBS" "-I$ROOT/components/nocti_v3_adc" "-I$ROOT/main")
for dir in "${INCLUDE_DIRS[@]}"; do INC+=("-I$dir"); done
passed=0
while IFS= read -r file; do
    clang -std=gnu11 -Wall -Wextra -Werror -Wformat=2 -fsyntax-only \
        "${INC[@]}" "$file"
    printf 'PASS %s\n' "${file#$ROOT/}"
    passed=$((passed + 1))
done < <(find "$ROOT/components" "$ROOT/main" -name '*.c' | sort)
printf 'SUMMARY translation_units=%d passed=%d failed=0\n' "$passed" "$passed"
