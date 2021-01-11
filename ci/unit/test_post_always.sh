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
    *NLT*)
	test_log_dir="nlt_logs"
	;;
esac

rm -rf $test_log_dir
mkdir $test_log_dir

# Copy any log files.  Use rsync filters here to allow us to specify
# all files we want to copy, as it's much more flexible than using
# standard wildcards.
rsync -v -rpt -z -e "ssh $SSH_KEY_ARGS" jenkins@"$NODE":/tmp/ \
      --filter="include dnt*.log" --filter="include daos*.log" \
      --filter="exclude *" "$test_log_dir/"

# Note that we are taking advantage of the NFS mount here and if that
# should ever go away, we need to pull run_test.sh/ from $NODE
python utils/fix_cmocka_xml.py
