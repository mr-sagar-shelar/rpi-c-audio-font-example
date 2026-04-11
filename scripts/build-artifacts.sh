#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT_DIR="${OUT_DIR:-${REPO_ROOT}/build/tinycore/artifacts}"
IMAGE_TAG="${IMAGE_TAG:-rpi-demo-tinycore-builder}"
BUILDER_PLATFORM="${BUILDER_PLATFORM:-linux/amd64}"
TARGET_ARCH="${TARGET_ARCH:-aarch64}"
BUILD_IMAGE="${BUILD_IMAGE:-1}"

mkdir -p "${OUT_DIR}"

docker build \
  --platform "${BUILDER_PLATFORM}" \
  --build-arg TARGET_ARCH="${TARGET_ARCH}" \
  -f "${REPO_ROOT}/tinycore/docker/artifact-builder.Dockerfile" \
  -t "${IMAGE_TAG}" \
  "${REPO_ROOT}"

CID="$(docker create "${IMAGE_TAG}")"
cleanup() {
  docker rm -f "${CID}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}"
docker cp "${CID}:/out/." "${OUT_DIR}/"

printf 'Artifacts exported to %s\n' "${OUT_DIR}"

if [[ "${BUILD_IMAGE}" == "1" ]]; then
  ARTIFACT_DIR="${OUT_DIR}" TARGET_ARCH="${TARGET_ARCH}" sh "${REPO_ROOT}/scripts/build-picore-image-macos.sh"
fi
