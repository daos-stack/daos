#!/bin/bash

# This is a post test processing script for post processing the
# NLT stage CI run

set -uex

NODE="${NODELIST%%,*}"

case $STAGE_NAME in
    "NLT on "*)
      test_log_dir="nlt_logs"
      ;;
    "NLT with Bullseye on "*)
      test_log_dir="nlt_bullseye_logs"
      ;;
    *)
      echo "Unexpected STAGE_NAME '$STAGE_NAME' in test_nlt_post.sh"
      exit 1
      ;;
esac

rm -rf "$test_log_dir"
mkdir "$test_log_dir"

# Copy any log files.  Use rsync filters here to allow us to specify
# all files we want to copy, as it's much more flexible than using
# standard wildcards.
rsync -v -dprt -e "ssh $SSH_KEY_ARGS" jenkins@"$NODE":/tmp/ \
      --filter="include dnt*.log" --filter="include dnt*.log.bz2" \
      --filter="include dnt_fi_*_logs" --filter="include test.cov" \
      --filter="exclude *" "${test_log_dir}/"

rsync -v -dpt -z -e "ssh $SSH_KEY_ARGS" jenkins@"$NODE":build/ \
      --filter="include nlt*.json" --filter="include dnt*.xml" \
      --filter="include nltir.xml" --filter="include nltr.json" \
      --filter="include nlt-junit.xml" --filter="exclude *" ./
mkdir -p vm_test
mv nlt-errors.json vm_test/
