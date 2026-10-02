# 0007: Work lands on main, checked before every push

- Status: Accepted
- Date: 2026-10-02
- Deciders: Daher Alfawares

## Context

The repository has one maintainer. Branches and pull requests added a step to every
change without adding a reviewer, and a stack of them waiting to merge made every
merge a rebase.

## Decision

Work is committed to `main` and pushed to it. Before a push, the full checks pass
locally: the same steps as the `checks` workflow, from a clean clone. The workflow
runs again on every push to `main`.

Every commit is signed. `main` is never force-pushed and its history stays linear.

Commit messages are short plain sentences, with no prefixes and no attribution
lines.

## Consequences

A change reaches `main` in one step. Nothing stops a push that skips the local
checks except the habit of running them, and the workflow reports a failure after
the fact rather than blocking it.

Contributors other than the maintainer will need another way in, likely pull
requests again, when there are any.
