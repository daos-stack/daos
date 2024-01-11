#!/bin/bash

# set -x
set -e -o pipefail

CWD="$(realpath "${0%/*}")"

set -a
# shellcheck disable=SC1091
source "$CWD/.env"
set +a

docker stack deploy -c "$1" daos_stack
