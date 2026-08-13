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
    # Une dépendance non récupérée n'est pas une dépendance en échec. RT64 par
    # exemple n'est pas cloné sur un poste qui ne vise que Windows 95, où il
    # n'a de toute façon pas de place (aucun de D3D12, Vulkan ou Metal n'y
    # existe). Le dire et passer, plutôt que de s'arrêter sur une trace Python.
    if not (repo / ".git").exists():
        print(f"[--] {dependency['name']}: worktree absent ({repo}) — ignoré")
        continue
    commit = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
    if commit != dependency["expectedCommit"]:
        raise SystemExit(f"{dependency['name']} commit mismatch: expected {dependency['expectedCommit']}, got {commit}")
    # La propreté se constate UNE FOIS, avant d'appliquer quoi que ce soit.
    #
    # Ce contrôle était à l'intérieur de la boucle, ce qui le rendait impossible
    # à satisfaire au-delà du premier patch : dès qu'un patch s'applique,
    # l'arbre porte des modifications suivies, et le patch suivant les prenait
    # pour des éditions locales. La pile entière ne pouvait donc jamais
    # s'appliquer d'un coup — seulement patch par patch, à la main.
    #
    # L'intention du garde-fou est conservée, et sa règle est maintenant simple
    # à énoncer : un arbre sale alors qu'aucun patch n'est encore appliqué ne
    # peut être que le fruit d'une édition directe, que l'ADR 0004 interdit.
    # Sitôt qu'un patch est appliqué, la saleté est la nôtre, et c'est
    # `git apply --check` qui juge la suite — lui ne se trompe pas.
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
            # Cas courant et sans gravite : l'arbre porte deja la pile, mais un
            # patch anterieur ne se detecte plus comme applique parce qu'un
            # patch ulterieur a deplace son contexte. `git apply --reverse`
            # raisonne fichier par fichier et ne sait pas defaire une pile qui
            # se recouvre — ni patch par patch, ni en bloc.
            #
            # On ne devine donc pas : on le dit, et on donne le geste sur.
            raise SystemExit(
                f"{dependency['name']}: {entry['path']} ne s'applique pas.\n"
                f"{check.stderr.strip()}\n"
                f"L'arbre porte probablement deja la pile : les patchs qui se\n"
                f"recouvrent ne se detectent pas un par un. Pour repartir d'un\n"
                f"etat certain :\n"
                f"    git -C {repo} checkout -- .\n"
                f"    bash scripts/apply-dependency-patches.sh")
        subprocess.run(["git", "-C", str(repo), "apply", str(patch)], check=True)
        print(f"[OK] {dependency['name']}: {entry['path']} (applied)")
PY
