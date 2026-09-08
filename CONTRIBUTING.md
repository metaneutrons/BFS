# Contributing to BFS

## Development workflow

Create a focused branch from `main`. Use descriptive branch names such as `fix/cache-flush` or
`feat/fsck-repair`. Direct commits to `main` are intentionally blocked by the local hooks and by
GitHub's repository rules.

Install the local hooks and run the repository checks:

```sh
make setup
make repository-audit quality-gates
```

The Amiga integration suite additionally requires FS-UAE and Internet access for its first,
checksum-verified AROS ROM download. Optional BFS/PFS3 benchmarks require legally obtained local
AmigaOS, PFS3, and DiskSpeed assets; see `emulator-test/README.md` for the local setup.

## Commits and pull requests

Substantial changes use a versioned plan in `docs/plans/`, linked to an epic and
milestone issues. Plans own requirements; issues own progress and acceptance
evidence. See [release readiness](docs/plans/release-readiness.md) and the
[format and cross-platform plan](docs/plans/cross-platform-development.md).

Commit messages and pull request titles must follow Conventional Commits. Accepted types are
`feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`, and `revert`.
Use a lowercase optional scope, keep the subject at or below 100 characters, and mark breaking
changes with `!` plus a `BREAKING CHANGE:` footer.

Pull requests must explain the reason for the change, describe validation, and disclose any
remaining risk. All review conversations must be resolved before merging. The repository uses
squash merges, so the pull request title becomes the release-relevant commit subject on `main`.

Do not commit generated build output, downloaded tools, ROMs, Workbench files, or other binary
assets. CI downloads external tools by immutable identity and verifies them before use.
