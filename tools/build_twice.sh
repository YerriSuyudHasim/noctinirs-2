#!/usr/bin/env bash
# Run in the pinned IDF image. No shared build, sdkconfig, object, or ccache state.
set -euo pipefail
source_root="$(git rev-parse --show-toplevel)"
sha="${SOURCE_SHA:?SOURCE_SHA must be the full source commit}"
evidence="${1:?usage: bash tools/build_twice.sh ABSOLUTE_EVIDENCE_DIRECTORY}"
[[ "$evidence" = /* ]] || { echo 'Evidence directory must be absolute'; exit 1; }
[[ ! -e "$evidence" ]] || { echo 'Refusing to reuse an evidence directory'; exit 1; }
mkdir -p "$evidence"
root="$(mktemp -d "${RUNNER_TEMP:-/tmp}/nn2-issue9.XXXXXXXX")"
printf '%s\n' "$root" > "$evidence/workspace-root.txt"
export CCACHE_DISABLE=1 IDF_CCACHE_ENABLE=0 LC_ALL=C.UTF-8 TZ=UTC
# Exercise real wall-clock variation; do not mask timestamp bugs with an epoch.
unset SOURCE_DATE_EPOCH CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
. "$IDF_PATH/export.sh" > /dev/null
git config --global --add safe.directory "$source_root"
for label in A B; do
    if [[ "$label" = A ]]; then
        work="$root/short-a"
    else
        work="$root/different-depth/longer-independent-workspace-b"
    fi
    mkdir -p "$evidence/$label"
    # --no-local forces independent Git objects (no hardlinks/shared alternates).
    git clone --quiet --no-local --no-hardlinks --no-checkout "$source_root" "$work"
    git -C "$work" checkout --quiet --detach "$sha"
    [[ "$(git -C "$work" rev-parse HEAD)" = "$sha" ]]
    [[ ! -e "$work/build" && ! -e "$work/sdkconfig" ]]
    printf 'CLEAN_START=1\nSOURCE_SHA=%s\nWORKSPACE=%s\nUTC=%s\n' \
        "$sha" "$work" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" | tee "$evidence/$label/clean-start.txt"
    rc=0
    (
        set -euo pipefail
        cd "$work"
        idf.py set-target esp32s3 2>&1 | tee "$evidence/$label/set-target.log" || exit $?
        idf.py build 2>&1 | tee "$evidence/$label/build.log" || exit $?
        idf.py size 2>&1 | tee "$evidence/$label/size.txt" || exit $?
    ) || rc=$?
    # Preserve failed builds too; both snapshots and comparison always run.
    python "$source_root/tools/reproducible_build.py" snapshot \
        --workspace "$work" --output "$evidence/$label" --sha "$sha" --build-exit "$rc" || true
done
# This exit status is the CI verdict. Never discard or normalize binary bytes.
python "$source_root/tools/reproducible_build.py" compare "$evidence/A" "$evidence/B" --output "$evidence"
