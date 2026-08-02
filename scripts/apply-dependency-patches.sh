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
    commit = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
    if commit != dependency["expectedCommit"]:
        raise SystemExit(f"{dependency['name']} commit mismatch: expected {dependency['expectedCommit']}, got {commit}")
    for entry in dependency["patches"]:
        patch = root / entry["path"]
        digest = hashlib.sha256(patch.read_bytes()).hexdigest()
        if digest != entry["sha256"]:
            raise SystemExit(f"Patch checksum mismatch: {entry['path']}")
        reverse = subprocess.run(["git", "-C", str(repo), "apply", "--reverse", "--check", str(patch)], capture_output=True)
        if reverse.returncode == 0:
            print(f"[OK] {dependency['name']}: {entry['path']} (already-applied)")
            continue
        changes = subprocess.check_output(["git", "-C", str(repo), "status", "--short", "--untracked-files=no", "--ignore-submodules=dirty"], text=True)
        if changes.strip():
            raise SystemExit(f"Refusing to patch dependency with tracked changes: {repo}\n{changes}")
        subprocess.run(["git", "-C", str(repo), "apply", "--check", str(patch)], check=True)
        subprocess.run(["git", "-C", str(repo), "apply", str(patch)], check=True)
        print(f"[OK] {dependency['name']}: {entry['path']} (applied)")
PY
