#!/usr/bin/env bash
set -euo pipefail

echo "=== CI build: Fusion OS ==="
make clean
make all
echo "=== CI build complete ==="
