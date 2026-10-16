# Relationship to ai-engineering-hub

`noctinirs-2` is a product repository. The separate private `ai-engineering-hub` repository is the governance source for shared policies, agent roles, handoffs, and project registration.

The repositories are connected by documentation and verified URLs, not by a Git submodule. This keeps product history, access, releases, and build configuration independent from governance history.

After the private GitHub repository is published:

1. Verify the exact repository URL, default branch, visibility, and human owner.
2. Complete `ai-engineering-hub-registration-draft.yaml` without guessing unknown values.
3. In `ai-engineering-hub`, create a governance issue and a branch for `projects/registry.yaml`.
4. Add the verified project record and link back to this repository's root `AGENTS.md`.
5. Submit the hub change through review and human merge approval.
6. Protect both default branches and run a low-risk pilot issue through the full workflow.

Do not duplicate product source in the hub. Do not place hub policy files inside historical snapshots.
