#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
manifest="$project_root/patches/manifest.json"

python3 - "$project_root" "$manifest" <<'PY'
import hashlib
import json
import pathlib
import subprocess
import sys

root = pathlib.Path(sys.argv[1])
manifest_path = pathlib.Path(sys.argv[2])
data = json.loads(manifest_path.read_text(encoding="utf-8"))
if data.get("schemaVersion") != 1:
    raise SystemExit("Unsupported patch manifest schema")

for dependency in data["dependencies"]:
    repo = root / dependency["repositoryPath"]
    # A dependency that was not fetched is not a failed dependency. RT64, for
    # instance, is not cloned on a machine that only targets Windows 95, where it
    # has no place anyway (none of D3D12, Vulkan or Metal exists there). Say so and
    # move on, rather than stopping on a Python traceback.
    if not (repo / ".git").exists():
        print(f"[--] {dependency['name']}: worktree absent ({repo}) - skipped")
        continue
    commit = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
    if commit != dependency["expectedCommit"]:
        raise SystemExit(f"{dependency['name']} commit mismatch: expected {dependency['expectedCommit']}, got {commit}")
    # Cleanliness is established ONCE, before anything is applied.
    #
    # This check used to be inside the loop, which made it impossible to satisfy
    # beyond the first patch: as soon as one patch applies, the tree carries tracked
    # modifications, and the next patch took them for local edits. The whole stack
    # could therefore never be applied in one go - only patch by patch, by hand.
    #
    # The guard rail's intent is preserved, and its rule is now simple to state: a
    # dirty tree while no patch has yet been applied can only be the fruit of a
    # direct edit, which ADR 0004 forbids. As soon as one patch is applied, the dirt
    # is ours, and it is `git apply --check` that judges the rest - it does not get
    # it wrong.
    pending = []
    applied_already = 0
    for entry in dependency["patches"]:
        patch = root / entry["path"]
        digest = hashlib.sha256(patch.read_bytes()).hexdigest()
        if digest != entry["sha256"]:
            raise SystemExit(f"Patch checksum mismatch: {entry['path']}")
        reverse = subprocess.run(["git", "-C", str(repo), "apply", "--reverse", "--check", str(patch)], capture_output=True)
        if reverse.returncode == 0:
            applied_already += 1
            print(f"[OK] {dependency['name']}: {entry['path']} (already-applied)")
        else:
            pending.append(entry)

    if pending and applied_already == 0:
        changes = subprocess.check_output(["git", "-C", str(repo), "status", "--short", "--untracked-files=no", "--ignore-submodules=dirty"], text=True)
        if changes.strip():
            raise SystemExit(f"Refusing to patch dependency with tracked changes: {repo}\n{changes}")

    for entry in pending:
        patch = root / entry["path"]
        check = subprocess.run(["git", "-C", str(repo), "apply", "--check", str(patch)],
                               capture_output=True, text=True)
        if check.returncode != 0:
            # A common and harmless case: the tree already carries the stack, but
            # an earlier patch no longer detects as applied because a later one
            # moved its context. `git apply --reverse` reasons file by file and
            # cannot undo an overlapping stack - neither patch by patch nor as a
            # whole.
            #
            # So we do not guess: we say so, and give the safe move.
            raise SystemExit(
                f"{dependency['name']}: {entry['path']} does not apply.\n"
                f"{check.stderr.strip()}\n"
                f"The tree probably already carries the stack: overlapping\n"
                f"patches do not detect one by one. To start again from a\n"
                f"known state:\n"
                f"    git -C {repo} checkout -- .\n"
                f"    bash scripts/apply-dependency-patches.sh")
        subprocess.run(["git", "-C", str(repo), "apply", str(patch)], check=True)
        print(f"[OK] {dependency['name']}: {entry['path']} (applied)")
PY
