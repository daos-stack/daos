#!/bin/bash

# set -x
set -e -o pipefail

CWD="$(realpath "$(dirname $0)")"

source "$CWD/.env"

docker run --rm -v "$DAOS_AGENT_RUNTIME_DIR:/var/run/daos_agent" -u $DAOS_CLIENT_UID:$DAOS_CLIENT_GID --network host "daos-client:$DAOS_DOCKER_IMAGE_TAG" -c "$*"
