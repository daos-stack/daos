#!/usr/bin/env bash

# Install OS updates and packages as required for building DAOS on EL 9 and
# derivatives.  Include basic tools and daos dependencies that come from the core repos.

# This script use used by docker but can be invoked from elsewhere, in order to run it
# interactively then these two commands can be used to set dnf into automatic mode.
# dnf --assumeyes install dnf-plugins-core
# dnf config-manager --save --setopt=assumeyes=True

set -e

dnf --nodocs install \
    boost-python3-devel \
    bzip2 \
    capstone-devel \
    clang \
    clang-tools-extra \
    cmake \
    CUnit-devel \
    daxctl-devel \
    diffutils \
    e2fsprogs \
    file \
    flex \
    fuse3 \
    gcc \
    gcc-c++ \
    git \
    glibc-langpack-en \
    golang \
    graphviz \
    help2man \
    hwloc-devel \
    ipmctl \
    java-1.8.0-openjdk \
    json-c-devel \
    libaio-devel \
    libasan \
    libcmocka-devel \
    libevent-devel \
    libipmctl-devel \
    libiscsi-devel \
    libtool \
    libtool-ltdl-devel \
    libunwind-devel \
    libuuid-devel \
    libyaml-devel \
    lz4-devel \
    make \
    ndctl \
    ndctl-devel \
    numactl \
    numactl-devel \
    openmpi-devel \
    openssl-devel \
    patch \
    patchelf \
    pciutils \
    pciutils-devel \
    protobuf-c-devel \
    python3-devel \
    python3-pip \
    sg3_utils \
    sudo \
    valgrind-devel \
    which \
    yasm
