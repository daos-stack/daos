#!/bin/bash

# This is the script used for running unit testing
# run_utest.py and run_utest.py with memcheck stages on the CI
set -uex

# JENKINS-52781 tar function is breaking symlinks

rm -rf unit_test_memcheck_logs unit-test*.memcheck.xml
rm -rf unit_test_memcheck_logs.tar.gz
rm -rf unit_test_memcheck_bdev_logs.tar.gz
rm -rf unit_test_logs
rm -rf test_results
mkdir test_results
chmod 777 test_results

# Check if this is a Bulleye stage
USE_BULLSEYE=false
BDEV_TEST=false
case $STAGE_NAME in
  *Bullseye**)
  USE_BULLSEYE=true
  ;;
  *bdev**)
  BDEV_TEST=true
  ;;
esac

if $USE_BULLSEYE; then
  rm -rf bullseye
  mkdir -p bullseye
  tar -C bullseye --strip-components=1 -xf bullseye.tar
else
  BULLSEYE=
fi

NODE=${NODELIST%%,*}

# Copy over the install tree and some of the build tree.
rsync -rlpt -z -e "ssh $SSH_KEY_ARGS" . jenkins@"$NODE":build/

# shellcheck disable=SC2029
ssh -tt "$SSH_KEY_ARGS" jenkins@"$NODE" "HOSTNAME=$HOSTNAME        \
                                         HOSTPWD=$PWD              \
                                         WITH_VALGRIND=$WITH_VALGRIND \
                                         BULLSEYE=$BULLSEYE        \
                                         BDEV_TEST=$BDEV_TEST       \
                                         ./build/ci/unit/test_main_node.sh"
