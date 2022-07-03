#!/bin/sh -e

# Install OS updates and packages as required for building DAOS on EL 9 and
# derivatives.  Include basic tools and daos dependencies that come from the core repos.

# This script use used by docker but can be invoked from elsewhere, in order to run it
# interactively then these two commands can be used to set dnf into automatic mode.
# dnf --assumeyes install dnf-plugins-core
# dnf config-manager --save --setopt=assumeyes=True

arch=$(uname -i)

dnf --nodocs install \
    boost-python3-devel \
    bzip2 \
    clang \
    clang-tools-extra \
    cmake \
    CUnit-devel \
    diffutils \
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
    help2man \
    hwloc-devel \
    java-1.8.0-openjdk \
    json-c-devel \
    libaio-devel \
    libcmocka-devel \
    libevent-devel \
    libiscsi-devel \
    libtool \
    libtool-ltdl-devel \
    libunwind-devel \
    libuuid-devel \
    libyaml-devel \
    lz4-devel \
    make \
    ndctl \
    numactl \
    numactl-devel \
    openmpi-devel \
    openssl-devel \
    patch \
    patchelf \
    python3-devel \
    python3-pip \
    sg3_utils \
    sudo \
    valgrind-devel \
    which \
    yasm

# No packages for the one below have been
# identified yet. Limit build to client only for now
#    ipmctl \
#    libipmctl-devel \
#    Lmod \
#
# ipmctl is only available on x86_64
if [ "$arch" = x86_64 ]; then
#    dnf --nodocs install \
#        ipmctl \
#        libipmctl-devel
fi
