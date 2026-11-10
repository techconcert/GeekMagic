#!/usr/bin/env bash
# Builds the browser preview: the SAME game core the clock runs, compiled to
# WebAssembly, embedded into a single self-contained preview/dist/index.html.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p preview/dist
CLANG=${CLANG:-clang}
$CLANG --target=wasm32 -O2 -ffreestanding -nostdlib -fno-exceptions -fno-rtti \
  -fno-builtin -Wall -Wextra -Werror=return-type \
  -Wl,--no-entry -Wl,--strip-all -Wl,-z,stack-size=65536 \
  -o preview/dist/gamecore.wasm \
  lib/gamecore/src/gamecore.cpp preview/wasm_shim.cpp
python3 - <<'PY'
import base64, pathlib
wasm = pathlib.Path("preview/dist/gamecore.wasm").read_bytes()
html = pathlib.Path("preview/index.template.html").read_text()
html = html.replace("__WASM_BASE64__", base64.b64encode(wasm).decode())
pathlib.Path("preview/dist/index.html").write_text(html)
print(f"preview/dist/index.html written ({len(wasm)} byte wasm)")
PY
