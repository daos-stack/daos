#!/bin/bash

# This is a post test processing script for post processing the
# run_test.sh stage CI run

set -uex

# shellcheck disable=SC1091
source ./.build_vars.sh

rm -rf run_test.sh vm_test
DAOS_BASE="${SL_PREFIX%/install*}"
NODE="${NODELIST%%,*}"

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

scp -i ci_key "$mydir/run_test_post_always_node.sh" \
              jenkins@"$NODE":/var/tmp

# shellcheck disable=SC2029
ssh "$SSH_KEY_ARGS" jenkins@"$NODE" \
  "DAOS_BASE=$DAOS_BASE      \
   /var/tmp/post_always_node.sh"

# Note that we are taking advantage of the NFS mount here and if that
# should ever go away, we need to pull run_test.sh/ from $NODE
python utils/fix_cmocka_xml.py
