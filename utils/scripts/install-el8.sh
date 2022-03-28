#!/bin/sh

# Install OS updates and package.  Include basic tools and daos dependencies
# that come from the core repo.
# Clean up any repos afterwards to save space.
# Switch to dnf as it seems a bit faster.
# libatomic should be in this list, but can not for now due to CI
# post provisioning issue.
# *** Keep these in as much alphbetical order as possible ***

set -e

dnf -y upgrade && \
dnf -y install \
    boost-python3-devel \
    clang-analyzer \
    cmake \
    CUnit-devel \
    e2fsprogs \
    file \
    flex \
    fuse3 \
    fuse3-devel \
    gcc \
    gcc-c++ \
    git \
    glibc-langpack-en \
    golang \
    graphviz \
    hwloc-devel \
    ipmctl \
    java-1.8.0-openjdk \
    json-c-devel \
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
    openmpi-devel \
    openssl-devel \
    patch \
    patchelf \
    pciutils \
    python3-defusedxml \
    python3-devel \
    python3-distro \
    python3-junit_xml \
    python3-pip \
    python3-pyxattr \
    python3-tabulate \
    python3-scons \
    python3-yaml \
    sg3_utils \
    sudo \
    valgrind-devel \
    yasm && \
dnf clean all
