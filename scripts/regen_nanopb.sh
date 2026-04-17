#!/usr/bin/env bash
# Regenerate nanopb sources for LoDB-shipped protos (maintainers only).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GEN="$(python3 -c 'import nanopb, os; print(os.path.join(os.path.dirname(nanopb.__file__), "generator", "nanopb_generator.py"))')"
mkdir -p "$ROOT/include/lodb"
cd "$ROOT/src"
python3 "$GEN" -D "$ROOT/include/lodb" --generated-include-format='#include <lodb/%s>' diagnostics.proto
# PlatformIO compiles C sources from srcDir only — keep the .c under src/
mv -f "$ROOT/include/lodb/diagnostics.pb.c" "$ROOT/src/diagnostics.pb.c"
