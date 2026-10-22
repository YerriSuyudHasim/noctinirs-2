# Release notes template

Release notes are rendered from `CHANGELOG.md`. That file is owned by
[issue #14](https://github.com/YerriSuyudHasim/noctinirs-2/issues/14) and
draft [PR #42](https://github.com/YerriSuyudHasim/noctinirs-2/pull/42).
Do not add a second changelog in this repository.

`CHANGELOG.md` is not on `main` yet. PR #42 uses a Keep a Changelog
`## Unreleased` section and has no `## [0.1.0]` heading. The renderer
does not treat Unreleased text as release notes.

When the merged changelog contains one version section:

```text
python3 tools/render_release_notes.py --changelog CHANGELOG.md --version 0.1.0
```

The heading must be `## [0.1.0]` or `## [0.1.0] - YYYY-MM-DD`. A missing
file, a missing heading, or an empty section exits 1. Paste the stdout
into the GitHub release. The command does not create a tag or a release.
