#!/bin/bash

# This is the script used for generating the Bullseye Reports stage in CI
set -ex

rm -rf bullseye
mkdir -p bullseye
tar -C bullseye --strip-components=1 -xf bullseye.tar

# shellcheck disable=SC1091
source ./.build_vars.sh
DAOS_BASE=${SL_PREFIX%/install*}
NODE=${NODELIST%%,*}
mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# shellcheck disable=SC2029
ssh "$SSH_KEY_ARGS" jenkins@"$NODE" "DAOS_BASE=$DAOS_BASE      \
                                     HOSTNAME=$HOSTNAME        \
                                     HOSTPWD=$PWD              \
                                     SL_PREFIX=$SL_PREFIX      \
                                     BULLSEYE=$BULLSEYE        \
                              $(cat "$mydir/bullseye_generate_report_node.sh")"
