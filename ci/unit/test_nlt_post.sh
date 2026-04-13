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
      --filter="exclude *" nlt_logs/

rsync -v -dpt -z -e "ssh $SSH_KEY_ARGS" jenkins@"$NODE":build/ \
      --filter="include nlt*.json" --filter="include dnt*.xml" \
      --filter="include nltir.xml" --filter="include nltr.json" \
      --filter="include nlt-junit.xml" --filter="exclude *" ./

# Copy per-test valgrind memcheck XMLs into a dedicated directory so
# they are archived as a logical group. The originals stay at the
# workspace root so unitTestPost's valgrind_stash still picks them up
# for the pipeline-end valgrindReportPublish panel.
mkdir -p nlt_memcheck_logs
cp dnt*.memcheck.xml nlt_memcheck_logs/ 2>/dev/null || true

mkdir -p vm_test
mv nlt-errors.json vm_test/
