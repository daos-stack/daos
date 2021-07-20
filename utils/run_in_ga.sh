#!/bin/bash

set -e

SCONS=scons

if [ ! -e /usr/bin/scons ]
then
    SCONS=scons-3
fi

echo ::group::Rebuild spdk
rm -rf /opt/daos/prereq/release/spdk
$SCONS PREFIX=/opt/daos --build-deps=yes --deps-only
echo ::endgroup::

echo "::group::Stack analyzer output (post build)"
$SCONS --jobs 10 --analyze-stack="-x tests -c 128" server
echo ::endgroup::

echo "::group::Stack analyzer output (immediate)"
$SCONS -Q --jobs 10 --analyze-stack="-x tests -c 128 -e" server
echo ::endgroup::

echo ::group::Test client only debug build
$SCONS --jobs 10 PREFIX=/opt/daos COMPILER="$COMPILER" BUILD_TYPE=debug \
       TARGET_TYPE=release -c install
utils/check.sh -n /opt/daos/bin/dmg
$SCONS --jobs 10 client install
utils/check.sh -n /opt/daos/bin/daos_engine
utils/check.sh -n /opt/daos/bin/vos_tests
utils/check.sh /opt/daos/bin/dmg
echo ::endgroup::

echo ::group::Test server only debug build
$SCONS --jobs 10 -c install
utils/check.sh -n /opt/daos/bin/daos_engine
$SCONS --jobs 10 server install
utils/check.sh /opt/daos/bin/daos_engine
utils/check.sh -n /opt/daos/bin/vos_tests
utils/check.sh -n /opt/daos/bin/dmg
echo ::endgroup::

echo ::group::Test incremental debug build with test target
$SCONS --jobs 10 test client
echo ::endgroup::

echo ::group::Config file
cat daos.conf
echo ::endgroup::

echo ::group::Install debug
$SCONS install
utils/check.sh /opt/daos/bin/daos_engine
utils/check.sh /opt/daos/bin/vos_tests
utils/check.sh /opt/daos/bin/dmg
echo ::endgroup::

echo ::group::Setting up daos_admin
. utils/sl/setup_local.sh
./utils/setup_daos_admin.sh
echo ::endgroup::

echo ::group::Container copy test
./utils/node_local_test.py --no-root --memcheck no --test cont_copy
echo ::endgroup::
