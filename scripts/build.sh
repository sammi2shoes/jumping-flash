#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
export JUMPING_FLASH_TOOLS="${JUMPING_FLASH_TOOLS:-/workspace/tooling}"
export PLATFORMIO_CORE_DIR="$JUMPING_FLASH_TOOLS/platformio"
exec "$JUMPING_FLASH_TOOLS/venv/bin/pio" run -d "$project_dir" -j 4 "$@"
