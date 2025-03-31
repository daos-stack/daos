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
    flex \
    fuse3 \
    gcc \
    gcc-c++ \
    git \
    go \
    go-race \
    graphviz \
    gzip \
    hwloc-devel \
    java-1_8_0-openjdk-devel \
    libaio-devel \
    libasan8 \
    libcmocka-devel \
    libcapstone-devel \
    libevent-devel \
    libiscsi-devel \
    libjson-c-devel \
    libltdl7 \
    liblz4-devel \
    libndctl-devel \
    libnuma-devel \
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
    patch \
    patchelf \
    pciutils \
    pciutils-devel \
    python3-devel \
    scons \
    sg3_utils \
    sudo \
    valgrind-devel \
    which \
    yasm

# ipmctl is only available on x86_64
if [ "$arch" = x86_64 ]; then
    dnf --nodocs install \
        ipmctl-devel
fi
