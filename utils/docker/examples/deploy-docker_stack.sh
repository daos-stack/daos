#!/bin/bash

# set -x
set -e -o pipefail

CWD="$(realpath "${0%/*}")"

set -a
source "$CWD/.env"
set +a

docker stack deploy -c "$1" daos_stack
