#!/bin/bash -e

cd daos

# Probably not needed now, but leave on PRs until we have confidence of landings builds.
echo ::group::Rebuild spdk
rm -rf /opt/daos/prereq/release/spdk
scons --jobs "$DEPS_JOBS" PREFIX=/opt/daos --build-deps=yes --deps-only
echo ::endgroup::

echo "::group::Stack analyzer output (post build)"
# $SCONS --jobs "$DEPS_JOBS" PREFIX=/opt/daos --analyze-stack="-x tests -c 128" server
scons --jobs "$DEPS_JOBS" --analyze-stack="-x tests -c 128" server
echo ::endgroup::

echo "::group::Stack analyzer output (immediate)"
scons -Q --jobs "$DEPS_JOBS" --analyze-stack="-x tests -c 128 -e" server
echo ::endgroup::

echo ::group::Test client only debug build
scons --jobs "$DEPS_JOBS" PREFIX=/opt/daos COMPILER="$COMPILER" BUILD_TYPE=debug \
       TARGET_TYPE=release -c install
utils/check.sh -n /opt/daos/bin/dmg
scons --jobs "$DEPS_JOBS" client install
utils/check.sh -n /opt/daos/bin/daos_engine
utils/check.sh -n /opt/daos/bin/vos_tests
utils/check.sh /opt/daos/bin/dmg
echo ::endgroup::

echo ::group::Test server only debug build
scons --jobs "$DEPS_JOBS" -c install
utils/check.sh -n /opt/daos/bin/daos_engine
scons --jobs "$DEPS_JOBS" server install
utils/check.sh /opt/daos/bin/daos_engine
utils/check.sh -n /opt/daos/bin/vos_tests
utils/check.sh -n /opt/daos/bin/dmg
echo ::endgroup::

echo ::group::Test incremental debug build with test target
scons --jobs "$DEPS_JOBS" test client
echo ::endgroup::

echo ::group::Config file
cat daos.conf
echo ::endgroup::

echo ::group::Install debug
scons install
utils/check.sh /opt/daos/bin/daos_engine
utils/check.sh /opt/daos/bin/vos_tests
utils/check.sh /opt/daos/bin/dmg
echo ::endgroup::

echo ::group::Install pydaos
cd src/client
python3 setup.py install
cd -
echo ::endgroup::

echo ::group::Setting up daos_admin
. utils/sl/setup_local.sh
./utils/setup_daos_admin.sh
echo ::endgroup::

echo ::group::Container copy test
./utils/node_local_test.py --no-root --memcheck no --test cont_copy
echo ::endgroup::
