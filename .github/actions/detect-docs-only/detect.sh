#!/usr/bin/env bash
# Classify a push or pull-request diff for the docs-only skip.
# docs_only=true only when every changed path is on the allowlist.
# A denylisted path, an unknown path, or any git uncertainty yields
# docs_only=false so the build gates run.
set -euo pipefail

emit() {
  local docs_only="$1"
  local files="$2"
  printf 'docs_only=%s\n' "$docs_only"
  printf 'files=%s\n' "$files"
  if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    {
      printf 'docs_only=%s\n' "$docs_only"
      printf 'files=%s\n' "$files"
    } >> "$GITHUB_OUTPUT"
  fi
}

# 0 = allowlisted documentation. 1 = run every gate.
path_is_docs_only() {
  local f="$1"
  if [[ -z "$f" || "$f" == *$'\n'* || "$f" == *$'\r'* ]]; then
    return 1
  fi
  # Denylist wins, including markdown that lives in a code tree.
  case "$f" in
    components/*|main/*|tests/*|test/*|tools/*|scripts/*|.github/*|reference/*)
      return 1 ;;
    VERSION|project.yaml)
      return 1 ;;
    CMakeLists.txt|*/CMakeLists.txt)
      return 1 ;;
    sdkconfig*|*/sdkconfig*)
      return 1 ;;
    idf_component.yml|*/idf_component.yml)
      return 1 ;;
    dependencies.lock|*/dependencies.lock|*.lock)
      return 1 ;;
    *partition*)
      return 1 ;;
  esac
  case "$f" in
    docs/*)
      return 0 ;;
    *.md|*.markdown|*.mdx|*.rst|*.adoc|*.asciidoc)
      return 0 ;;
    LICENSE|LICENSE.*|NOTICE|NOTICE.*|COPYING|COPYING.*|AUTHORS|AUTHORS.*|CHANGELOG|CHANGELOG.*)
      return 0 ;;
  esac
  return 1
}

classify_paths() {
  local f
  for f in "$@"; do
    if ! path_is_docs_only "$f"; then
      return 1
    fi
  done
  return 0
}

join_paths() {
  local joined=""
  local f
  if [[ $# -eq 0 ]]; then
    printf '%s' '(none)'
    return 0
  fi
  for f in "$@"; do
    if [[ -n "$joined" ]]; then
      joined+=', '
    fi
    joined+="$f"
  done
  if [[ ${#joined} -gt 1800 ]]; then
    joined="${joined:0:1800}..."
  fi
  printf '%s' "$joined"
}

finish() {
  local docs_only="false"
  if classify_paths "$@"; then
    docs_only="true"
  fi
  local files
  files="$(join_paths "$@")"
  emit "$docs_only" "$files"
}

finish_collected() {
  if [[ ${#COLLECTED[@]} -eq 0 ]]; then
    finish
  else
    finish "${COLLECTED[@]}"
  fi
}

collect_from_file() {
  local line
  COLLECTED=()
  while IFS= read -r line || [[ -n "$line" ]]; do
    if [[ -z "$line" ]]; then
      continue
    fi
    COLLECTED+=("$line")
  done < "$1"
}

parse_name_status_z() {
  local file="$1"
  local -a tokens=()
  local token status code
  local i n
  COLLECTED=()
  while IFS= read -r -d '' token; do
    tokens+=("$token")
  done < "$file"
  i=0
  n=${#tokens[@]}
  while [[ "$i" -lt "$n" ]]; do
    status="${tokens[$i]}"
    i=$((i + 1))
    code="${status:0:1}"
    case "$code" in
      R|C)
        if [[ $((i + 1)) -ge "$n" ]]; then
          return 1
        fi
        COLLECTED+=("${tokens[$i]}")
        COLLECTED+=("${tokens[$((i + 1))]}")
        i=$((i + 2))
        ;;
      A|M|D|T)
        if [[ "$i" -ge "$n" ]]; then
          return 1
        fi
        COLLECTED+=("${tokens[$i]}")
        i=$((i + 1))
        ;;
      *)
        return 1
        ;;
    esac
  done
  return 0
}

ensure_commit() {
  local sha="$1"
  if git cat-file -e "${sha}^{commit}" 2>/dev/null; then
    return 0
  fi
  git fetch --no-tags --depth=1 origin "$sha" >/dev/null 2>&1 || return 1
  git cat-file -e "${sha}^{commit}" 2>/dev/null || return 1
}

collect_from_git() {
  local base="$1"
  local head="$2"
  local tmp rc
  if ! ensure_commit "$base"; then
    return 1
  fi
  if ! ensure_commit "$head"; then
    return 1
  fi
  tmp="$(mktemp)"
  if ! git diff -z --name-status --find-renames "$base" "$head" > "$tmp"; then
    rm -f "$tmp"
    return 1
  fi
  parse_name_status_z "$tmp"
  rc=$?
  rm -f "$tmp"
  return "$rc"
}

if [[ -n "${DETECT_PATHS_FILE:-}" ]]; then
  collect_from_file "$DETECT_PATHS_FILE"
  finish_collected
  exit 0
fi

head="$(git rev-parse HEAD 2>/dev/null || true)"
expected="${EXPECTED_SHA:-$head}"
if [[ -z "$head" || -z "$expected" || "$head" != "$expected" ]]; then
  emit false "(uncertain)"
  exit 0
fi

event="${EVENT_NAME:-}"
base=""
case "$event" in
  pull_request)
    base="${BASE_SHA:-}"
    ;;
  push)
    base="${BEFORE_SHA:-}"
    if [[ "$base" == "0000000000000000000000000000000000000000" ]]; then
      emit false "(uncertain)"
      exit 0
    fi
    ;;
  *)
    emit false "(uncertain)"
    exit 0
    ;;
esac

if [[ -z "$base" ]]; then
  emit false "(uncertain)"
  exit 0
fi

if [[ "$base" == "$head" ]]; then
  finish
  exit 0
fi

if ! collect_from_git "$base" "$head"; then
  emit false "(uncertain)"
  exit 0
fi
finish_collected
exit 0
