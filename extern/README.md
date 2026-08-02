# External dependencies

This directory is deliberately empty in the distributed source ZIP.

Run `python scripts/bootstrap_dependencies.py` to clone the exact revisions recorded in
`dependencies.lock.json`. Milestone 0 does not link these dependencies yet, so the standalone
launcher, ROM validator and tests build without downloading them.
