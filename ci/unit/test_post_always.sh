#!/bin/bash

# This is a post test processing script for post processing the
# run_test.sh stage CI run

set -uex

if [ -e ./.build_vars.sh ]; then
  # shellcheck disable=SC1091
  source ./.build_vars.sh
else
  echo 'The .build_vars.sh file is missing!'
  exit 1
fi

DAOS_BASE="${SL_PREFIX%/install*}"
NODE="${NODELIST%%,*}"
mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

rm -rf run_test.sh vm_test run_test_memcheck.sh
echo "WITH_VALGRIND=${WITH_VALGRIND}"

# shellcheck disable=SC2029
ssh "$SSH_KEY_ARGS" jenkins@"$NODE" \
  "DAOS_BASE=$DAOS_BASE             \
  WITH_VALGRIND=$WITH_VALGRIND      \
  $(cat "$mydir/test_post_always_node.sh")"

if [ "$WITH_VALGRIND" == "disabled" ]; then
    # Note that we are taking advantage of the NFS mount here and if that
    # should ever go away, we need to pull run_test.sh/ from $NODE
    python utils/fix_cmocka_xml.py
fi
