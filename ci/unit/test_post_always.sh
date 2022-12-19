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

DAOS_BASE="${SL_SRC_DIR}"
NODE="${NODELIST%%,*}"

mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

: "${STAGE_NAME:="Unit Test"}"

# shellcheck disable=SC2029
ssh "$SSH_KEY_ARGS" jenkins@"$NODE" \
  "DAOS_BASE=$DAOS_BASE             \
   $(cat "$mydir/test_post_always_node.sh")"

case $STAGE_NAME in
    *Bullseye*)
	test_log_dir="covc_test_logs"
	;;
    *memcheck*)
	test_log_dir="unit_test_memcheck_logs"
	;;
    *Unit*)
	test_log_dir="unit_test_logs"
	;;
esac

rm -rf $test_log_dir
mkdir $test_log_dir

rsync -v -rlpt -z -e "ssh $SSH_KEY_ARGS" jenkins@"$NODE":build/ .

# Copy any log files.  Use rsync filters here to allow us to specify
# all files we want to copy, as it's much more flexible than using
# standard wildcards.
rsync -v -dpt -z -e "ssh $SSH_KEY_ARGS" jenkins@"$NODE":/tmp/ \
      --filter="include daos*.log" --filter="include test.cov" \
      --filter="exclude *" "$test_log_dir/"
