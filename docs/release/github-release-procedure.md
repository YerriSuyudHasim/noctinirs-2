# GitHub release procedure

Tagging and publishing are Owner-only. This document does not authorize an
agent to merge, tag, publish a GitHub Release, sign firmware, or flash a
device. No tag or release is created by following it during review.

## Merge authority

A merge onto `main` needs all of the following, recorded on the PR or issue:

1. Required CI on the exact reviewed SHA.
2. Independent review by someone who is not the author or coauthor (Agent 07 for this change).
3. Merge approval by Nori jointly with Agent 01. The Owner confirmed that delegation directly to Agent 07 at 15:24 KST on 2026-10-22.

A green check, repository access, or an agent's own review is not merge
approval. Preparation of a draft PR is not permission to merge. Tags and
GitHub Releases remain with the Owner (M) via Nori.

## Preconditions

- `python3 tools/check_version_consistency.py --root .` passes on the commit to be tagged.
- The tag name equals `VERSION` exactly. For the current tree that string is `0.1.0`. A `v` prefix fails the check. Do not create the tag until the Owner chooses that spelling.
- Release notes come from `python3 tools/render_release_notes.py --changelog CHANGELOG.md --version <tag>`. `CHANGELOG.md` is added by issue #14 / PR #42 and is absent on current `main`. Stop if the command fails. Do not write a substitute changelog.
- Issue #46 packages are attached only when that PR is merged and the packages were built from the tagged SHA, with their checksum files. Packages from another SHA are not assets of this tag. PR #46 is still a draft.
- Class H stays NOT RUN for v0.1.0. The Owner deferred it at 15:25 KST on 2026-10-22. Flash needs the Owner's direct OK via Nori. H-COMMS-1 (#53 / PR #54) is a parked draft and is not a v0.1.0 gate.

## Tag and CI

The Owner, on the reviewed commit:

```text
git fetch origin
git checkout <40-character SHA>
git tag -a <version> -m "NoctiNIRS 2 <version>"
git push origin <version>
```

Do not push `main` in this step. An unfiltered `push` workflow runs for a tag
push. Required workflows: Firmware build, Reproducible build, Reference
integrity, and Version consistency. Version consistency reads
`GITHUB_REF_TYPE=tag` and fails if the tag disagrees. Do not publish while
any required job is failing. CI artifact retention is `retention-days: 90`;
do not lower it.

## Evidence copy

Copy the tagged run's logs, `SHA256SUMS`, images, and the matching #46
package zips plus checksums to controlled storage the Owner names. GitHub
retention is at least 90 days and is not a permanent archive. Record the
run URL, attempt, expiry, and SHA-256 next to the copy.

## Publish

The Owner creates the GitHub Release on that tag, pastes the rendered notes,
and attaches the #46 packages and checksums from the tagged SHA. Publishing
is not a signature and not permission to flash.

## Rollback

If tag CI fails, do not publish. Deleting the tag is a separate Owner
decision; keep the failed run URL. If a release was published in error, the
Owner unpublishes or marks it prerelease without force-pushing `main`.
A device that was already flashed needs its own reviewed recovery procedure.
That procedure has not been run.
