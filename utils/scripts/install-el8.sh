#!/usr/bin/env bash

# Install OS updates and packages as required for building DAOS on EL 8 and
# derivatives.  Include basic tools and daos dependencies that come from the core repos.

# This script use used by docker but can be invoked from elsewhere, in order to run it
# interactively then these two commands can be used to set dnf into automatic mode.
# dnf --assumeyes install dnf-plugins-core
# dnf config-manager --save --setopt=assumeyes=True

set -e

arch=$(uname -m)

dnf --nodocs install \
    boost-python3-devel \
    bzip2 \
    capstone-devel \
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
    Lmod \
    lz4-devel \
    make \
    ndctl \
    numactl \
    numactl-devel \
    openmpi-devel \
    openssl-devel \
    patch \
    patchelf \
    protobuf-c-devel \
    python3-devel \
    python3-pip \
    sg3_utils \
    sudo \
    systemd \
    valgrind-devel \
    which \
    yasm

# ipmctl is only available on x86_64
if [ "$arch" = x86_64 ]; then
    dnf --nodocs install \
        ipmctl \
        libipmctl-devel
fi

# For fedora, java-11 is installed along with maven if we install maven from
# repo. But we need java-8 (1.8). The 'devel' package also needs to be
# installed specifically.

if [ -e /etc/fedora-release ]; then
        dnf install java-1.8.0-openjdk-devel maven-openjdk8
else
        dnf install maven
fi
