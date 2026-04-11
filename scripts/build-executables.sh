#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

BUILD_IMAGE=0 bash "${SCRIPT_DIR}/build-artifacts.sh"
