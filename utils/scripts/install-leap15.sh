#!/bin/sh -e

# Install OS updates and package.  Include basic tools and daos dependencies
# that come from the core repo.
# Clean up any repos afterwards to save space.
# Switch to dnf as it seems a bit faster.
# libatomic should be in this list, but can not for now due to CI
# post provisioning issue.
# *** Keep these in as much alphbetical order as possible ***

dnf --nodocs install \
    boost-devel \
    bzip2 \
    curl \
    clang \
    cmake \
    cunit-devel \
    flex \
    fuse3-devel \
    gcc \
    gcc-c++ \
    git \
    go1.18 \
    go1.18-race \
    graphviz \
    gzip \
    hwloc-devel \
    ipmctl-devel \
    java-1_8_0-openjdk-devel \
    libaio-devel \
    libcmocka-devel \
    libevent-devel \
    libiscsi-devel \
    libjson-c-devel \
    libltdl7 \
    liblz4-devel \
    libnuma-devel \
    libopenssl-devel \
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
    python3-devel \
    scons \
    sg3_utils \
    sudo \
    valgrind-devel \
    which \
    yasm
