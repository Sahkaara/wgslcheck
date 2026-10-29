#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${1:-"$root/build"}

cmake -S "$root" -B "$build_dir"
cmake --build "$build_dir" --config Release
ctest --test-dir "$build_dir" --build-config Release --output-on-failure

executable="$build_dir/wgslcheck"
if [ -f "$build_dir/Release/wgslcheck.exe" ]; then
    executable="$build_dir/Release/wgslcheck.exe"
elif [ -f "$build_dir/Debug/wgslcheck.exe" ]; then
    executable="$build_dir/Debug/wgslcheck.exe"
fi
"$executable" check "$root/examples/sample.wgsl"