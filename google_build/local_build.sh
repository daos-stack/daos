#!/usr/bin/env bash

set -ex

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$SCRIPT_DIR"

docker build "$REPO_ROOT" -f build_image/Dockerfile -t daos-build
docker build "$REPO_ROOT" -f daos_base_image/Dockerfile -t daos-base
docker build "$REPO_ROOT" -f daos_image/Dockerfile \
  --build-arg BUILD_BASE=daos-build \
  --build-arg BASE=daos-base \
  -t daos