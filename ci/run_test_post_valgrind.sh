#!/bin/bash

# This is a post test processing script for post processing the
# run_test.sh stage CI run

set -uex

# shellcheck disable=SC1091
source ./.build_vars.sh

rm -rf run_test_valgrind.sh vm_test_valgrind
DAOS_BASE="${SL_PREFIX%/install*}"
NODE="${NODELIST%%,*}"

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# shellcheck disable=SC2029
ssh "$SSH_KEY_ARGS" jenkins@"$NODE" \
  "DAOS_BASE=$DAOS_BASE             \
  $(cat "$mydir/run_test_post_valgrind_node.sh")"
