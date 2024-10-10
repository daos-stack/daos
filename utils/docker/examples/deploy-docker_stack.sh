#!/bin/bash

# set -x
set -u -e -o pipefail

CWD="$(realpath "${0%}")"
CWD="${CWD%/*}"

set -a
# shellcheck disable=SC1091
source "$CWD/.env"
set +a

docker stack deploy -c "$1" daos_stack
