# Relationship to ai-engineering-hub

`noctinirs-2` is a product repository. The separate private `ai-engineering-hub` repository is the governance source for shared policies, agent roles, handoffs, and project registration.

The repositories are connected by documentation and verified URLs, not by a Git submodule. This keeps product history, access, releases, and build configuration independent from governance history.

The reciprocal registration records are:

1. `project.yaml` in this repository, which identifies the product-owned source and links to the hub.
2. `projects/noctinirs-2/project.yaml` in `ai-engineering-hub`, which defines the governance boundary and links back to this repository.

Verify both records together after either repository URL or boundary changes. Unknown human-owner, risk, or retention fields remain `null` or pending until the human owner approves exact values. Submit changes in each repository through its own issue, branch, review, and human merge approval workflow. Protect both default branches and run a low-risk pilot issue through the full workflow.

Do not duplicate product source in the hub. Do not place hub policy files inside historical snapshots.
