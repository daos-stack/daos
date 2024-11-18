#!/bin/bash

cd /daos
set -e

# This is the md5sum of the current utils/build.config. We need to detect when
# it changes so we can update githashes of RPM builds according to what upstream
# is using.
echo "0707ec2020e3c3b6490fd57cb8828c2d  utils/build.config" > md5sum.build && \
  md5sum --check md5sum.build >> /dev/null || \
  (echo "utils/build.config has changed, githashes need updating" && false)

# These represent matching rpm hashes to tags defined in utils/build.config.
# The hash corresponds to the commit in the commented repository. To update
# the hash, one can find the commit where the version specified in the rpm
# spec matches the version in utils/build.config. In many cases, this is
# likely HEAD.
# https://github.com/daos-stack/libfabric.git
libfabric=2e97711b8faf0876b95d813b28fae0beb4d6016e
# https://github.com/daos-stack/mercury.git
mercury=8a9b97c248d97f70b0caa64be588f0c404ecfadf
# https://github.com/daos-stack/isa-l.git
isal=500b6b0f4f0748385265e77ec8dc0c2063a6f8d1
# https://github.com/daos-stack/isa-l_crypto.git
isal_crypto=f98224c2f481e3f119248ff6e27aa7d34a39b8f0
# https://github.com/daos-stack/argobots.git
argobots=7a123c8bf1c327adef682904eb813d0fd84b5dd6
# https://github.com/daos-stack/dpdk.git
# dpdk isn't specified in utils/build.config but
# changes with spdk
dpdk=372c857e0122f0777a8e2b76c4988614917384bb
# https://github.com/daos-stack/spdk.git
spdk=cefc39f9a511206fcb4e28b0f8f06ac593808a04
# this one should never be out of date
raft=$(cat .git/modules/raft/HEAD)
# https://github.com/daos-stack/pmdk.git
pmdk=7fe78c07067e988c61308139dc1f3ddb5b310607

echo ${!1}
