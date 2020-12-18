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

dnt_logs=false
case $STAGE_NAME in
    *Bullseye*)
	test_log_dir="covc_test_logs/"
	;;
    *memcheck*)
	test_log_dir="unit_test_memcheck_logs/"
	;;
    *Unit*)
	test_log_dir="unit_test_logs/"
	;;
    *NLT*)
	test_log_dir="nlt_logs/"
	dnt_logs=true
	;;
esac

rm -rf $test_log_dir
mkdir $test_log_dir

if $dnt_logs
then
    rsync -av -z -e "ssh $SSH_KEY_ARGS" jenkins@"$NODE":/tmp/dnt*.log $test_log_dir
else
    rsync -av -z -e "ssh $SSH_KEY_ARGS" jenkins@"$NODE":/tmp/daos*.log $test_log_dir
fi

# Note that we are taking advantage of the NFS mount here and if that
# should ever go away, we need to pull run_test.sh/ from $NODE
python utils/fix_cmocka_xml.py
