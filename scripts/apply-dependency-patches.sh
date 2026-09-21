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
    #
    # `.exists()` and not `.is_dir()`: a dependency checked out as a git submodule
    # carries `.git` as a **file** pointing into the superproject, and a directory
    # test skips it as absent -- which reads as "not fetched" for a dependency that
    # is right there.
    if not (repo / ".git").exists():
        print(f"[SKIP] {dependency['name']}: checkout not present at {repo}")
        continue
    commit = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
    if commit != dependency["expectedCommit"]:
        raise SystemExit(f"{dependency['name']} commit mismatch: expected {dependency['expectedCommit']}, got {commit}")

    patches = []
    for entry in dependency["patches"]:
        patch = root / entry["path"]
        digest = hashlib.sha256(patch.read_bytes()).hexdigest()
        if digest != entry["sha256"]:
            raise SystemExit(f"Patch checksum mismatch: {entry['path']}")
        patches.append((entry, patch))

    # Check if every patch can be cleanly reverse-applied (all already applied).
    all_applied = all(
        subprocess.run(["git", "-C", str(repo), "apply", "--reverse", "--check", str(p)], capture_output=True).returncode == 0
        for _, p in patches
    )
    if all_applied:
        for entry, _ in patches:
            print(f"[OK] {dependency['name']}: {entry['path']} (already-applied)")
        continue

    # Check if every patch can be cleanly forward-applied (none applied yet).
    all_unapplied = all(
        subprocess.run(["git", "-C", str(repo), "apply", "--check", str(p)], capture_output=True).returncode == 0
        for _, p in patches
    )
    if all_unapplied:
        for entry, p in patches:
            subprocess.run(["git", "-C", str(repo), "apply", str(p)], check=True)
            print(f"[OK] {dependency['name']}: {entry['path']} (applied)")
        continue

    # Partially applied or context-shifted: reset to the pinned commit and
    # re-apply everything. Safe because the commit is pinned and verified.
    print(f"[INFO] {dependency['name']}: resetting to pinned commit and re-applying all patches")
    subprocess.run(["git", "-C", str(repo), "checkout", "--", "."], check=True)
    # `checkout` restores tracked files and leaves untracked ones where they
    # are. A patch that *adds* a file -- the platform seams add three between
    # them -- then refuses on the second run, because its new file already
    # exists, and the stack stops there for good. Anything untracked in a
    # pinned dependency checkout came from a patch, so clean it: the checkout
    # is meant to be the pinned commit plus this list and nothing else.
    subprocess.run(["git", "-C", str(repo), "clean", "-fdq"], check=True)
    for entry, p in patches:
        subprocess.run(["git", "-C", str(repo), "apply", str(p)], check=True)
        print(f"[OK] {dependency['name']}: {entry['path']} (applied)")
PY
