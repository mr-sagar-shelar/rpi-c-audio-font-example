#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "scripts/build-image.sh now forwards to scripts/build-picore-image-macos.sh" >&2
exec sh "${SCRIPT_DIR}/build-picore-image-macos.sh" "$@"
