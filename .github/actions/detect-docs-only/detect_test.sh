#!/usr/bin/env bash
# Host checks for the docs-only allowlist. No network and no firmware build.
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
detect="$script_dir/detect.sh"
fail=0
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

check_file() {
  local name="$1"
  local expected="$2"
  local list="$3"
  local out got
  printf '%s\n' "$list" > "$tmpdir/paths.txt"
  out="$(DETECT_PATHS_FILE="$tmpdir/paths.txt" bash "$detect")"
  got="$(printf '%s\n' "$out" | sed -n 's/^docs_only=//p')"
  if [[ "$got" != "$expected" ]]; then
    echo "FAIL $name expected=$expected got=$got"
    printf '%s\n' "$out"
    fail=1
  else
    echo "PASS $name"
  fi
}

check_file "readme" true "README.md"
check_file "agents-md" true "AGENTS.md"
check_file "docs-nested" true "docs/guide/intro.md"
check_file "docs-png" true "docs/images/board.png"
check_file "license" true "LICENSE"
check_file "changelog" true "CHANGELOG.md"
check_file "empty" true ""
check_file "multi-docs" true $'docs/a.md\ndocs/b.rst\nNOTICE'
check_file "component-c" false "components/nn2_core/src/foo.c"
check_file "component-readme" false "components/nn2_core/README.md"
check_file "main" false "main/main.c"
check_file "main-md" false "main/README.md"
check_file "tests" false "tests/architecture/test_ci_minutes.py"
check_file "test-host" false "test/host/CMakeLists.txt"
check_file "tools" false "tools/verify-reference.ps1"
check_file "scripts" false "scripts/build.sh"
check_file "github" false ".github/workflows/firmware-build.yml"
check_file "reference" false "reference/v4/README.md"
check_file "reference-manifest" false "reference/MANIFEST.sha256"
check_file "version" false "VERSION"
check_file "project-yaml" false "project.yaml"
check_file "root-cmake" false "CMakeLists.txt"
check_file "nested-cmake" false "docs/CMakeLists.txt"
check_file "sdkconfig" false "sdkconfig.defaults"
check_file "sdkconfig-diag" false "sdkconfig.ads131-diagnostic"
check_file "component-yml" false "main/idf_component.yml"
check_file "lock" false "dependencies.lock"
check_file "managed-lock" false "components/nn2_core/dependencies.lock"
check_file "partitions" false "partitions.csv"
check_file "docs-partition-note" false "docs/partition-notes.md"
check_file "mixed" false $'docs/a.md\nmain/main.c'
check_file "gitignore" false ".gitignore"
check_file "unknown-bin" false "firmware.bin"
check_file "yaml" false "something.yml"

init_repo() {
  local repo="$1"
  rm -rf "$repo"
  mkdir -p "$repo"
  git -C "$repo" init -q -b main
  git -C "$repo" config user.email "detect@example.com"
  git -C "$repo" config user.name "detect"
}

run_detect() {
  local repo="$1"
  (
    cd "$repo"
    EVENT_NAME="$EVENT_NAME" \
      BASE_SHA="${BASE_SHA:-}" \
      BEFORE_SHA="${BEFORE_SHA:-}" \
      EXPECTED_SHA="${EXPECTED_SHA:-}" \
      bash "$detect"
  )
}

expect_git() {
  local name="$1"
  local expected="$2"
  local repo="$3"
  local out got
  out="$(run_detect "$repo")"
  got="$(printf '%s\n' "$out" | sed -n 's/^docs_only=//p')"
  if [[ "$got" != "$expected" ]]; then
    echo "FAIL git-$name expected=$expected got=$got"
    printf '%s\n' "$out"
    fail=1
  else
    echo "PASS git-$name"
  fi
}

repo="$tmpdir/repo"
init_repo "$repo"
printf 'docs\n' > "$repo/README.md"
git -C "$repo" add README.md
git -C "$repo" commit -q -m base
base="$(git -C "$repo" rev-parse HEAD)"
printf 'more docs\n' >> "$repo/README.md"
git -C "$repo" add README.md
git -C "$repo" commit -q -m docs
head="$(git -C "$repo" rev-parse HEAD)"

EVENT_NAME=push BEFORE_SHA="$base" BASE_SHA="" EXPECTED_SHA="$head" \
  expect_git docs-push true "$repo"
EVENT_NAME=pull_request BASE_SHA="$base" BEFORE_SHA="" EXPECTED_SHA="$head" \
  expect_git docs-pr true "$repo"
EVENT_NAME=push BEFORE_SHA="0000000000000000000000000000000000000000" \
  BASE_SHA="" EXPECTED_SHA="$head" \
  expect_git zero-before false "$repo"
EVENT_NAME=workflow_dispatch BEFORE_SHA="$base" BASE_SHA="" EXPECTED_SHA="$head" \
  expect_git unknown-event false "$repo"
EVENT_NAME=push BEFORE_SHA="$base" BASE_SHA="" \
  EXPECTED_SHA="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
  expect_git head-mismatch false "$repo"
EVENT_NAME=push BEFORE_SHA="$head" BASE_SHA="" EXPECTED_SHA="$head" \
  expect_git empty-diff true "$repo"

mkdir -p "$repo/main"
printf 'int x;\n' > "$repo/main/foo.c"
git -C "$repo" add main/foo.c
git -C "$repo" commit -q -m code
code_head="$(git -C "$repo" rev-parse HEAD)"
EVENT_NAME=push BEFORE_SHA="$head" BASE_SHA="" EXPECTED_SHA="$code_head" \
  expect_git code-push false "$repo"
EVENT_NAME=pull_request BASE_SHA="$base" BEFORE_SHA="" EXPECTED_SHA="$code_head" \
  expect_git code-pr-from-base false "$repo"

# Rename of a docs file into a code path must run gates (both endpoints).
init_repo "$repo"
printf 'docs\n' > "$repo/README.md"
git -C "$repo" add README.md
git -C "$repo" commit -q -m docs
rename_base="$(git -C "$repo" rev-parse HEAD)"
mkdir -p "$repo/main"
git -C "$repo" mv README.md main/foo.c
git -C "$repo" commit -q -m rename
rename_head="$(git -C "$repo" rev-parse HEAD)"
EVENT_NAME=push BEFORE_SHA="$rename_base" BASE_SHA="" EXPECTED_SHA="$rename_head" \
  expect_git rename-into-code false "$repo"

# Deleting a code file is still a code change.
init_repo "$repo"
mkdir -p "$repo/main"
printf 'int x;\n' > "$repo/main/foo.c"
git -C "$repo" add main/foo.c
git -C "$repo" commit -q -m code
del_base="$(git -C "$repo" rev-parse HEAD)"
git -C "$repo" rm -q main/foo.c
git -C "$repo" commit -q -m delete
del_head="$(git -C "$repo" rev-parse HEAD)"
EVENT_NAME=push BEFORE_SHA="$del_base" BASE_SHA="" EXPECTED_SHA="$del_head" \
  expect_git delete-code false "$repo"

# A missing endpoint cannot be proved docs-only.
init_repo "$repo"
printf 'docs\n' > "$repo/README.md"
git -C "$repo" add README.md
git -C "$repo" commit -q -m docs
missing_head="$(git -C "$repo" rev-parse HEAD)"
EVENT_NAME=push BEFORE_SHA="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" \
  BASE_SHA="" EXPECTED_SHA="$missing_head" \
  expect_git missing-base false "$repo"

if [[ "$fail" -ne 0 ]]; then
  echo "detect_test: FAILED"
  exit 1
fi
echo "detect_test: PASS"
exit 0
