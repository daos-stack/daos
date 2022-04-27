#!/bin/sh

# Install OS updates and package.  Include basic tools and daos dependencies
# that come from the core repo.
# Clean up any repos afterwards to save space.
# Switch to dnf as it seems a bit faster.
# libatomic should be in this list, but can not for now due to CI
# post provisioning issue.
# *** Keep these in as much alphbetical order as possible ***

set -e

dnf -y --nodocs install \
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
    go1.14 \
    go1.14-race \
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
    meson \
    numactl \
    ninja \
    openmpi3-devel \
    patch \
    patchelf \
    pciutils \
    python3-defusedxml \
    python3-devel \
    python3-distro \
    python3-junit-xml \
    python3-pyxattr  \
    python3-PyYAML \
    python3-pyelftools \
    python3-tabulate \
    scons \
    sg3_utils \
    sudo \
    valgrind-devel \
    which \
    yasm
