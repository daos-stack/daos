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

echo ::group::Test client only build
$SCONS --jobs 10 PREFIX=/opt/daos COMPILER="$COMPILER" TARGET_TYPE=release \
       BUILD_TYPE=dev -c install
utils/check.sh -n /opt/daos/bin/dmg
$SCONS --jobs 10 PREFIX=/opt/daos COMPILER="$COMPILER" TARGET_TYPE=release \
       BUILD_TYPE=dev client install
utils/check.sh -n /opt/daos/bin/daos_engine
utils/check.sh -n /opt/daos/bin/vos_tests
utils/check.sh /opt/daos/bin/dmg
echo ::endgroup::

echo ::group::Test server only build
$SCONS --jobs 10 PREFIX=/opt/daos COMPILER="$COMPILER" TARGET_TYPE=release \
       BUILD_TYPE=dev -c install
utils/check.sh -n /opt/daos/bin/daos_engine
$SCONS --jobs 10 PREFIX=/opt/daos COMPILER="$COMPILER" TARGET_TYPE=release \
       BUILD_TYPE=dev server install
utils/check.sh /opt/daos/bin/daos_engine
utils/check.sh -n /opt/daos/bin/vos_tests
utils/check.sh -n /opt/daos/bin/dmg
echo ::endgroup::

echo ::group::Test incremental build with test target
$SCONS --jobs 10 PREFIX=/opt/daos COMPILER="$COMPILER" TARGET_TYPE=release \
       BUILD_TYPE=dev test install
utils/check.sh /opt/daos/bin/daos_engine
utils/check.sh /opt/daos/bin/vos_tests
utils/check.sh /opt/daos/bin/dmg
echo ::endgroup::

echo ::group::Build type debug.
$SCONS --jobs 10 PREFIX=/opt/daos COMPILER="$COMPILER" TARGET_TYPE=release \
       BUILD_TYPE=debug
echo ::endgroup::

cat daos.conf

echo ::group::Install debug
$SCONS install
echo ::endgroup::

echo ::group::Setting up daos_admin
. utils/sl/setup_local.sh
./utils/setup_daos_admin.sh
echo ::endgroup::

echo ::group::Container copy test
# DAOS-7440
export LD_LIBRARY_PATH=/opt/daos/prereq/release/spdk/lib/
./utils/node_local_test.py --no-root --test cont_copy
echo ::endgroup::
