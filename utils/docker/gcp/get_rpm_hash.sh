#!/bin/bash

cd /daos
set -e

# This is the md5sum of the current utils/build.config. We need to detect when
# it changes so we can update githashes of RPM builds according to what upstream
# is using.
echo "db730219aadc70c128ce4d77a46c8d55  utils/build.config" > md5sum.build && \
  md5sum --check md5sum.build >> /dev/null || \
  (echo "utils/build.config has changed, githashes need updating" && false)

# These represent matching rpm hashes to tags defined in utils/build.config.
# The hash corresponds to the commit in the commented repository. To update
# the hash, one can find the commit where the version specified in the rpm
# spec matches the version in utils/build.config. In many cases, this is
# likely HEAD.
# https://github.com/daos-stack/libfabric.git
libfabric=b21d23ec3ac4085835ed3fbe2c68455cb971dabc
# https://github.com/daos-stack/mercury.git
mercury=8a9b97c248d97f70b0caa64be588f0c404ecfadf
# https://github.com/daos-stack/isa-l.git
isal=500b6b0f4f0748385265e77ec8dc0c2063a6f8d1
# https://github.com/daos-stack/isa-l_crypto.git
isal_crypto=e87aa39e932849e0e8572db92fa8fd5c93d414c4
# https://github.com/daos-stack/argobots.git
argobots=7a123c8bf1c327adef682904eb813d0fd84b5dd6
# https://github.com/daos-stack/dpdk.git
# dpdk isn't specified in utils/build.config but
# changes with spdk
dpdk=372c857e0122f0777a8e2b76c4988614917384bb
# https://github.com/daos-stack/spdk.git
spdk=cefc39f9a511206fcb4e28b0f8f06ac593808a04
raft=HEAD
# https://github.com/daos-stack/pmdk.git
pmdk=7fe78c07067e988c61308139dc1f3ddb5b310607

echo ${!1}
