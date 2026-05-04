#!/bin/bash
set -e

# Dynamic paths
REPO_ROOT="$(git rev-parse --show-toplevel)"
HOME_DIR="$HOME"
CACHE_DIR="${HOME_DIR}/.cache/sccache"

# Build configuration (Rosetta has problems at high concurrency)
BUILD_JOBS="${BUILD_JOBS:-6}"

# Image configuration
IMAGE="clickhouse/binary-builder:local"

# Check if local image exists, if not offer to build it
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "Image '$IMAGE' not found locally."
    echo "The Docker Hub 'latest' image has clang 19, but ClickHouse requires clang 21."
    echo ""
    echo "Options:"
    echo "  1) Build images locally (takes ~10-20 min)"
    echo "  2) Abort"
    echo ""
    read -p "Build locally? [y/N] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "Building clickhouse/fasttest:local (amd64)..."
        docker build --platform linux/amd64 --network=host -t clickhouse/fasttest:local "${REPO_ROOT}/ci/docker/fasttest/"

        echo "Building clickhouse/binary-builder:local (amd64)..."
        docker build --platform linux/amd64 --network=host --build-arg FROM_TAG=local -t clickhouse/binary-builder:local "${REPO_ROOT}/ci/docker/binary-builder/"
    else
        echo "Aborted. You can also manually build the images with:"
        echo "  docker build --platform linux/amd64 --network=host -t clickhouse/fasttest:local ${REPO_ROOT}/ci/docker/fasttest/"
        echo "  docker build --platform linux/amd64 --network=host --build-arg FROM_TAG=local -t clickhouse/binary-builder:local ${REPO_ROOT}/ci/docker/binary-builder/"
        exit 1
    fi
fi

mkdir -p "$CACHE_DIR"

echo "Building ClickHouse (Release) in ${REPO_ROOT}..."
docker run --rm --platform linux/amd64 \
  -v "${REPO_ROOT}:/ClickHouse" \
  -v "${CACHE_DIR}:/root/.cache/sccache" \
  -w /ClickHouse \
  "$IMAGE" \
  bash -c "cmake -S . -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release -DNO_ARMV81_OR_HIGHER=1 && cmake --build build_release -j${BUILD_JOBS}"
