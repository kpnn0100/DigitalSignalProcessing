#!/bin/bash
# Build the Arstro Kitchen Sink web UI (WebAssembly) with Emscripten.
# Requires `emcc` on PATH (https://emscripten.org). Run from apps/kitchen_sink/.
#
#   ./build_web.sh && (cd web && python3 -m http.server 8000)
#   open http://localhost:8000
set -e
cd "$(dirname "$0")"

if ! command -v emcc >/dev/null 2>&1; then
  echo "error: emcc not found. Install Emscripten and 'source emsdk_env.sh' first." >&2
  exit 1
fi

ROOT=../..
# Library sources on the synth path (spatial/ and util/ are legacy, excluded).
LIB_SRC=$(find "$ROOT/src" -name '*.cpp' -not -path '*/spatial/*' -not -path '*/util/*')

emcc -O2 -std=c++17 --bind \
  -I"$ROOT/src" \
  RackEngine.cpp web_bindings.cpp $LIB_SRC \
  -sMODULARIZE=1 -sEXPORT_NAME=createArstroModule \
  -sEXPORTED_RUNTIME_METHODS=HEAPF32 \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web \
  -o web/arstro.js

echo "built web/arstro.js + web/arstro.wasm"
