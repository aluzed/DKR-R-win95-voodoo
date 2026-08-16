#!/usr/bin/env bash
# Compatibility wrapper around `check_imports.py`.
#
# This script was E00-S01's tool. E01-S04 replaced it with
# `tools/win95/check_imports.py`, which tells system DLLs from driver DLLs, names
# the offending object and carries a list of justified exceptions.
#
# It survives because the research documents and the ADRs already written give
# this command line, and above all so that there is **only one reference
# baseline**: the old version kept its own in
# `$DKR_WIN95_PREFIX/win95-exports.txt`, outside the repository. Two baselines
# that diverge are worse than a single imperfect one - that is how one ends up
# no longer trusting the tool.
#
#   tools/win95/check-win95-imports.sh <binary>...
#   tools/win95/check-win95-imports.sh --refresh
#
# The versioned baseline now lives in `tools/win95/exports/`, with its
# provenance.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "$HERE/check_imports.py" "$@"
