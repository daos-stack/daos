#!/bin/bash

# This is the script used for running unit testing
# run_test.sh and run_test.sh with memcheck stages on the CI
set -uex

# JENKINS-52781 tar function is breaking symlinks

rm -rf unit_test_memcheck_logs unit-test*.memcheck.xml
rm -rf unit_test_memcheck_logs.tar.gz
rm -rf unit_test_logs
rm -rf test_results
mkdir test_results

# Check if this is a Bulleye stage
USE_BULLSEYE=false
case $STAGE_NAME in
  *Bullseye**)
  USE_BULLSEYE=true
  ;;
esac

if $USE_BULLSEYE; then
  rm -rf bullseye
  mkdir -p bullseye
  tar -C bullseye --strip-components=1 -xf bullseye.tar
else
  BULLSEYE=
fi

# shellcheck disable=SC1091
source ./.build_vars.sh
NODE=${NODELIST%%,*}
mydir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# Copy over the install tree and some of the build tree.
rsync -rlpt -z -e "ssh $SSH_KEY_ARGS" . jenkins@"$NODE":build/

# shellcheck disable=SC2029
ssh -tt "$SSH_KEY_ARGS" jenkins@"$NODE" "DAOS_BASE=$SL_SRC_DIR     \
                                         HOSTNAME=$HOSTNAME        \
                                         HOSTPWD=$PWD              \
                                         SL_PREFIX=$SL_PREFIX      \
                                         WITH_VALGRIND=$WITH_VALGRIND \
                                         BULLSEYE=$BULLSEYE        \
                                         ./build/test_main_node.sh
