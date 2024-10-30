#!/bin/bash
path_to_daos=$(realpath $(dirname ${BASH_SOURCE[0]})/..)
cd ${path_to_daos}

set -e

# This is the md5sum of the current utils/build.config. We need to detect when
# it changes so we can update githashes of RPM builds according to what upstream
# is using.
echo "9a661935ead4273b1b57c1b439060a96  utils/build.config" > md5sum.build && \
  md5sum --check md5sum.build >> /dev/null || \
  (echo "utils/build.config has changed, githashes need updating" && false)

# These represent matching rpm hashes to tags defined in utils/build.config.
# The hash corresponds to the commit in the commented repository. To update
# the hash, one can find the commit where the version specified in the rpm
# spec matches the version in utils/build.config. In many cases, this is
# likely HEAD.
# https://github.com/daos-stack/libfabric.git
libfabric=bb8354f951c01ecdeba5a5c47b4060bf8e21c8a7
# https://github.com/daos-stack/mercury.git
mercury=bd23ff3cc4299dede8c8cdd5aeb93010068a4ecd
# https://github.com/daos-stack/isa-l.git
isal=836a0b239d20af2a192588c194ea0f1cf57b3527
# https://github.com/daos-stack/isa-l_crypto.git
isal_crypto=6ba5377b9e13e80c8be97f51abb875f11f66c151
# https://github.com/daos-stack/argobots.git
argobots=738857942d52bd23d6d4575f4eea48975cc7adcc
# https://github.com/daos-stack/dpdk.git
# dpdk isn't specified in utils/build.config but
# changes with spdk
dpdk=70e89c0054ebb7865c60e98eadfc2840614b0885
# https://github.com/daos-stack/spdk.git
spdk=0deb13d2290a55535df2bf57e2d2b820fce42e9b
# this one should never be out of date
raft=$(cat .git/modules/raft/HEAD)
# https://github.com/daos-stack/pmdk.git
pmdk=d741e9df4db3951f8192340d0ed7e5d88e4f44f8

echo ${!1}
