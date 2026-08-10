# External dependencies

Dependency worktrees are created locally and are intentionally absent from
source archives. Exact repositories and commits are recorded in
`dependencies.lock.json`; project changes are applied only through the
checksummed Patch Pipeline in `patches/manifest.json`.

Use `Build-DKR-Runtime.cmd` for a complete prepared checkout. Do not edit,
commit or distribute dependency worktrees from this directory.
