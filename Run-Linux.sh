#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
exe="$PWD/build/linux-core-release/bin/Release/DKRPort"
[[ -x "$exe" ]] || { echo "Core executable missing. Run ./Build-Linux.sh first." >&2; exit 1; }
cat >&2 <<'MSG'
This milestone's Linux helper builds the command-line core without the native launcher.
Use commands such as:
  --headless-self-test
  --validate-rom /path/to/rom.z64
The one-window RmlUi launcher is packaged through Build-Windows.cmd in this delivery.
MSG
exec "$exe" --help
