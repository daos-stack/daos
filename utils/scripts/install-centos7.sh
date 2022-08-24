#!/bin/sh

# Install OS updates and package.  Include basic tools and daos dependencies
# that come from the core repo.
# Clean up any repos afterwards to save space.
# Switch to dnf as it seems a bit faster.
# libatomic should be in this list, but can not for now due to CI
# post provisioning issue.
# *** Keep these in as much alphbetical order as possible ***

set -e

dnf -y install deltarpm
dnf -y --nodocs install \
    boost-python36-devel \
    bzip2 \
    clang-analyzer \
    cmake3 \
    CUnit-devel \
    e2fsprogs \
    file \
    flex \
    fuse3 \
    fuse3-devel \
    gcc \
    gcc-c++ \
    git \
    golang \
    graphviz \
    hwloc-devel \
    ipmctl \
    java-1.8.0-openjdk \
    json-c-devel \
    lcov \
    libaio-devel \
    libcmocka-devel \
    libevent-devel \
    libipmctl-devel \
    libiscsi-devel \
    libtool \
    libtool-ltdl-devel \
    libunwind-devel \
    libuuid-devel \
    libyaml-devel \
    Lmod \
    lz4-devel \
    make \
    maven \
    meson \
    ndctl \
    ninja-build \
    numactl \
    numactl-devel \
    openmpi3-devel \
    openssl-devel \
    patch \
    patchelf \
    pciutils \
    python3-pip \
    python36-defusedxml \
    python36-devel \
    python36-distro \
    python36-junit_xml \
    python36-tabulate \
    python36-pyxattr \
    python36-PyYAML \
    python36-scons \
    sg3_utils \
    sudo \
    valgrind-devel \
    yasm
ln -s cmake3 /usr/bin/cmake
