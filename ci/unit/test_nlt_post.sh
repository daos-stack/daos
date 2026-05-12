#!/bin/bash

# This is a post test processing script for post processing the
# NLT stage CI run

set -uex

NODE="${NODELIST%%,*}"

rm -rf nlt_logs
mkdir nlt_logs

# Copy any log files.  Use rsync filters here to allow us to specify
# all files we want to copy, as it's much more flexible than using
# standard wildcards.
rsync -v -dprt -e "ssh $SSH_KEY_ARGS" jenkins@"$NODE":/tmp/ \
      --filter="include dnt*.log" --filter="include dnt*.log.bz2" \
      --filter="include dnt_fi_*_logs" \
      --filter="exclude *" nlt_logs/ || true

# When running with --no-root, DAOS logs go to build/nlt_logs/ on the node
# instead of /tmp/, so fetch them from there as well.
rsync -v -rlpt -e "ssh $SSH_KEY_ARGS" jenkins@"$NODE":build/nlt_logs/ \
      --filter="include dnt*.log" --filter="include dnt*.log.bz2" \
      --filter="include dnt_fi_*_logs" --filter="include */" \
      --filter="exclude *" nlt_logs/ || true

rsync -v -dpt -z -e "ssh $SSH_KEY_ARGS" jenkins@"$NODE":build/ \
      --filter="include nlt*.json" --filter="include dnt*.xml" \
      --filter="include nltir.xml" --filter="include nltr.json" \
      --filter="include nlt-junit.xml" --filter="exclude *" ./

mkdir -p vm_test
mv nlt-errors.json vm_test/
