#!/bin/bash

# set -x
set -e -o pipefail

# shellcheck disable=SC2086
CWD="$(realpath "$(dirname $0)")"

set -a
# shellcheck disable=SC1091
source "$CWD/.env"
set +a

docker stack up -c "$1" daos_stack
