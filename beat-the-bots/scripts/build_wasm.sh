#!/usr/bin/env bash
# Builds the engine to WebAssembly and copies the module into the web app.
# Requires Emscripten (emcmake/em++) on PATH.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/engine/build-wasm"
out="$root/web/src/engine/wasm"

emcmake cmake -S "$root/engine" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBTB_BUILD_TESTS=OFF -DBTB_BUILD_BENCH=OFF
cmake --build "$build" --target btb_wasm

mkdir -p "$out"
cp "$build/btb.js" "$build/btb.wasm" "$out/"
echo "wasm module: $(du -h "$out/btb.wasm" | cut -f1) ($(gzip -c "$out/btb.wasm" | wc -c | tr -d ' ') bytes gzipped)"
