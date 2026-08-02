#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

build_dir="$PWD/build/linux-core-release"
cmake -S . -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DDKRPORT_BUILD_NATIVE_UI=OFF \
  -DDKRPORT_BUILD_TESTS=ON \
  -DDKRPORT_ENABLE_WARNINGS_AS_ERRORS=ON
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure
"$build_dir/bin/Release/DKRPort" --headless-self-test --portable

printf '\nCore build and tests passed.\n'
printf 'This script intentionally builds the asset-free command-line core only.\n'
printf 'The automatic native launcher build supplied in this milestone targets Windows through Build-Windows.cmd.\n'
printf 'Executable: %s\n' "$build_dir/bin/Release/DKRPort"
