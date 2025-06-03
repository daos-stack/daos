#!/usr/bin/env bash

# Install OS updates and package.  Include basic tools and daos dependencies
# that come from the core repo.
# Clean up any repos afterwards to save space.
# Switch to dnf as it seems a bit faster.
# libatomic should be in this list, but can not for now due to CI
# post provisioning issue.
# *** Keep these in as much alphabetical order as possible ***

set -e

arch=$(uname -i)

dnf --nodocs install \
    boost-devel \
    bzip2 \
    curl \
    clang \
    cmake \
    cunit-devel \
    fdupes \
    flex \
    fuse3 \
    gcc \
    gcc-c++ \
    git \
    go \
    go-race \
    graphviz \
    gzip \
    hdf5-devel \
    hwloc-devel \
    java-1_8_0-openjdk-devel \
    libaio-devel \
    libasan8 \
    libcmocka-devel \
    libcapstone-devel \
    libevent-devel \
    libibverbs-devel \
    libiscsi-devel \
    libjson-c-devel \
    libltdl7 \
    liblz4-devel \
    libndctl-devel \
    libnl3-devel \
    libnuma-devel \
    libpsm2-devel \
    librdmacm-devel \
    libopenssl-devel \
    libprotobuf-c-devel \
    libtool \
    libunwind-devel \
    libuuid-devel \
    libyaml-devel \
    lua-lmod \
    make \
    maven \
    numactl \
    openmpi3-devel \
    pandoc \
    patch \
    patchelf \
    pciutils \
    pciutils-devel \
    python3-devel \
    rpm-build \
    scons \
    sg3_utils \
    sudo \
    valgrind-devel \
    which \
    yasm

dnf install ruby-devel
gem install json -v 2.7.6
gem install dotenv -v 2.8.1
gem install fpm
if [ ! -f /usr/bin/fpm ]; then
    ln -s "$(basename "$(ls -1 /usr/bin/fpm.ruby*)")" /usr/bin/fpm
fi

# ipmctl is only available on x86_64
if [ "$arch" = x86_64 ]; then
    dnf --nodocs install \
        ipmctl-devel
fi
