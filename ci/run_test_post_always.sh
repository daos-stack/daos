#!/bin/bash

# This is a post test processing script for post processing the
# run_test.sh stage CI run

set -uex

# shellcheck disable=SC1091
source ./.build_vars.sh
DAOS_BASE="${SL_PREFIX%/install*}"
NODE="${NODELIST%%,*}"
mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

if [ $# -ge 1 ] && [ -n "$1" ]; then
    if [ "$1" == "memcheck" ]; then
        echo "run test post always with memcheck"
        rm -rf run_test_memcheck.sh
        WITH_VALGRIND=memcheck
        # shellcheck disable=SC2029
        ssh "$SSH_KEY_ARGS" jenkins@"$NODE" \
          "DAOS_BASE=$DAOS_BASE             \
          WITH_VALGRIND=$WITH_VALGRIND      \
          $(cat "$mydir/run_test_post_always_node.sh")"
    fi
else
    echo "run test post always"
    rm -rf run_test.sh vm_test
    # shellcheck disable=SC2029
    ssh "$SSH_KEY_ARGS" jenkins@"$NODE" \
      "DAOS_BASE=$DAOS_BASE             \
      $(cat "$mydir/run_test_post_always_node.sh")"

    # Note that we are taking advantage of the NFS mount here and if that
    # should ever go away, we need to pull run_test.sh/ from $NODE
    python utils/fix_cmocka_xml.py

fi
