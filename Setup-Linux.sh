#!/usr/bin/env bash
set -euo pipefail
for tool in cmake ninja c++; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Missing required tool: $tool" >&2
    echo "Debian/Ubuntu: sudo apt install build-essential cmake ninja-build" >&2
    echo "Fedora: sudo dnf install gcc-c++ cmake ninja-build" >&2
    echo "Arch: sudo pacman -S base-devel cmake ninja" >&2
    exit 1
  fi
done
cmake --version | head -n1
c++ --version | head -n1
echo "Linux setup checks passed. Run ./Build-Linux.sh."
