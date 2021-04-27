#!/bin/bash

# This is a post test processing script for post processing the
# run_test.sh stage CI run

set -uex

NODE="${NODELIST%%,*}"

rm -rf nlt_logs
mkdir nlt_logs

# Copy any log files.  Use rsync filters here to allow us to specify
# all files we want to copy, as it's much more flexible than using
# standard wildcards.
rsync -v -dpt -e "ssh $SSH_KEY_ARGS" jenkins@"$NODE":/tmp/ \
      --filter="include dnt*.log" --filter="include dnt*.log.bz2" \
      --filter="exclude *" nlt_logs/

rsync -v -dpt -z -e "ssh $SSH_KEY_ARGS" jenkins@"$NODE":build/ \
      --filter="include nlt*.json" --filter="include dnt*.xml" \
      --filter="include nlt-junit.xml" --filter="exclude *" ./
mkdir -p vm_test
mv nlt-errors.json vm_test/
