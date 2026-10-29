# Feat35 CI: explicit editor profile regression

Date: 2026-10-29 KST. Scope: author tooling/tests correction for PR #75.
Baseline: `edb70411d2d7cc3da529c92af375bbd21f7f6016`.
No firmware runtime, workflow gating, board configuration, flash layout,
historical source, hardware operation, merge or release change is included.

## Diagnosis

GitHub Desktop showed zero changed files because the local tracked tree and
`origin/feat/35-ads131-acquisition` already matched. Its red indicator reported
CI failure, not a file synchronization error.

On that exact baseline, 28 of 30 GitHub checks passed. Both failed checks were
`architecture-contract`, one from push and one from pull_request. The actual
suite had one failure: `test_default_stays_fake_and_shared_editor_does_not_opt_in`.
The earlier reviewed-source C11 negative tests failed intentionally and were
correctly reported as successful regression controls; they were not the defect.

The failing assertion required the editor defaults list to contain only
`sdkconfig.defaults`. Feat35's versioned editor configuration instead explicitly
enables `NN2_USE_BRANCH_PROFILE=ON`, selects overlays declared by
`config/active-build-profile.cmake`, and uses isolated build/sdkconfig paths.
This was intentional branch-profile behavior that the older test did not cover.

Original GitHub job evidence:
[architecture-contract](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/40810213728/job/123238482156).

## Correction and retained protections

- Generic project/Kconfig defaults remain FAKE; CI still explicitly selects its
  fake, communication and diagnostic build profiles.
- Editor settings are checked against the versioned branch profile when its
  opt-in is explicit, including overlay existence/order and isolated paths.
- Negative fixtures reject implicit board overlays, mismatched defaults, root
  sdkconfig reuse, shared build-directory reuse and conflicting profile flags.
- Tests execute the actual CMake profile-preparation helper to prove that its
  default OFF preserves the generic configuration and explicit ON selects an
  isolated configuration with the declared overlays.
- The N6 Bash exit-code fixture writes LF explicitly. This preserves its exact
  failure-propagation assertions on Windows instead of creating an `exit 0\r`
  shell syntax error.

No test or CI job is skipped, no failed assertion is simply removed, and no
firmware/branch default is changed to make the checks pass.

## Author validation and handoff

All 63 architecture tests pass locally with Python 3.13, CMake 4.0.3, Ninja
1.12.1, Git Bash and the installed Xtensa GCC 15.2.0 used for compile-only C11
contract fixtures. Earlier local failures with GCC 5.1's missing GNU17 support
and Windows shell selection are retained in the local evidence logs; they are
not presented as GitHub failures. Historical integrity passes for 319 files;
`git diff --check` passes.

Remote validation must use the final pushed SHA and all completed checks,
not the previous commit or a screenshot containing in-progress checks. Exact
commit/check URLs and synchronization state are retained in the author handoff.
Independent review remains a separate role; this author supplies no approval.
Rollback is a reviewed revert of this test-only correction. Generated build
outputs and local configurations remain outside Git per repository policy.
