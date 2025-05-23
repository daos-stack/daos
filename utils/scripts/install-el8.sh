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
    daxctl-devel \
    diffutils \
    e2fsprogs \
    fdupes \
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
    hdf5-devel \
    hwloc-devel \
    java-1.8.0-openjdk \
    json-c-devel \
    libaio-devel \
    libasan \
    libcmocka-devel \
    libevent-devel \
    libibverbs-devel \
    libiscsi-devel \
    libnl3-devel \
    libpsm2-devel \
    librdmacm-devel \
    libtool \
    libtool-ltdl-devel \
    libunwind-devel \
    libuuid-devel \
    libyaml-devel \
    Lmod \
    lz4-devel \
    make \
    ndctl \
    ndctl-devel \
    numactl \
    numactl-devel \
    openmpi-devel \
    openssl-devel \
    pandoc \
    patch \
    patchelf \
    pciutils \
    pciutils-devel \
    protobuf-c-devel \
    python3-devel \
    python3-pip \
    rpm-build \
    sg3_utils \
    squashfs-tools \
    sudo \
    systemd \
    valgrind-devel \
    which \
    yasm

ruby_version=$(dnf module list ruby | grep -Eow "3\.[0-9]+" | tail -1)
dnf --nodocs install \
    "@ruby:${ruby_version}" \
    rubygems \
    rubygem-json

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

gem install fpm
