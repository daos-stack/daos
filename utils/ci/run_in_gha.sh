#!/bin/bash -e

cd daos

# Probably not needed now, but leave on PRs until we have confidence of landings builds.
echo ::group::Rebuild spdk
rm -rf /opt/daos/prereq/release/spdk
scons --jobs "$DEPS_JOBS" PREFIX=/opt/daos --build-deps=only
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
utils/ci/gha-file-check.sh -n /opt/daos/bin/dmg
scons --jobs "$DEPS_JOBS" client install
utils/ci/gha-file-check.sh -n /opt/daos/bin/daos_engine
utils/ci/gha-file-check.sh -n /opt/daos/bin/vos_tests
utils/ci/gha-file-check.sh /opt/daos/bin/dmg
echo ::endgroup::

echo ::group::Test server only debug build
scons --jobs "$DEPS_JOBS" -c install
utils/ci/gha-file-check.sh -n /opt/daos/bin/daos_engine
scons --jobs "$DEPS_JOBS" server install
utils/ci/gha-file-check.sh /opt/daos/bin/daos_engine
utils/ci/gha-file-check.sh -n /opt/daos/bin/vos_tests
utils/ci/gha-file-check.sh -n /opt/daos/bin/dmg
echo ::endgroup::

echo ::group::Test incremental debug build with test target
scons --jobs "$DEPS_JOBS" test client
echo ::endgroup::

echo ::group::Config file
cat daos.conf
echo ::endgroup::

echo ::group::Install debug
scons install
utils/ci/gha-file-check.sh /opt/daos/bin/daos_engine
utils/ci/gha-file-check.sh /opt/daos/bin/vos_tests
utils/ci/gha-file-check.sh /opt/daos/bin/dmg
echo ::endgroup::

echo ::group::Rebuild ofi in alternative location
rm -rf /opt/daos/prereq/release/{ofi,mercury} build/external/release/{ofi,mercury*}
scons PREFIX=/opt/daos/dep TARGET_TYPE=release --build-deps=only DEPS=ofi --jobs \
      "$DEPS_JOBS"
echo ::endgroup::

echo ::group::Rebuild mercury and daos with ofi from ALT_PREFIX
scons install ALT_PREFIX=/opt/daos/dep/prereq/release/ofi PREFIX=/opt/daos --build-deps=yes \
      DEPS=all BUILD_TYPE=dev --jobs "$DEPS_JOBS"
utils/ci/gha-file-check.sh /opt/daos/bin/daos_engine
utils/ci/gha-file-check.sh /opt/daos/bin/vos_tests
utils/ci/gha-file-check.sh /opt/daos/bin/dmg
echo ::endgroup::

echo ::group::Config file after ALT_PREFIX build
cat daos.conf
echo ::endgroup::

echo ::group::Install pydaos
cd src/client
python3 setup.py install
cd -
echo ::endgroup::

echo ::group::Setting up daos_server_helper
. utils/sl/setup_local.sh
./utils/setup_daos_server_helper.sh
echo ::endgroup::

echo ::group::Container copy test
./utils/node_local_test.py --no-root --memcheck no --system-ram-reserved 1 --test cont_copy
echo ::endgroup::
