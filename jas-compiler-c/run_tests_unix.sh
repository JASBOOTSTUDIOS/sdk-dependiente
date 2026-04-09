#!/usr/bin/env bash
# Ejecuta la suite de tests con jbc (compilador C).
# Uso: desde la raíz del repo: ./sdk-dependiente/jas-compiler-c/run_tests_unix.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
JBC="${JBC:-$ROOT/bin/jbc}"
TESTS_DIR="${TESTS_DIR:-$SCRIPT_DIR/tests}"

if [ ! -x "$JBC" ]; then
  echo "Error: No se encuentra jbc. Compile el compilador C en sdk-dependiente/jas-compiler-c"
  exit 1
fi

"$JBC" test --dir "$TESTS_DIR"
