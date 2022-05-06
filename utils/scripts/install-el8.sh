#!/bin/sh -e

# Install OS updates and packages as required for building DAOS on EL 8 and
# derivatives.  Include basic tools and daos dependencies that come from the core repos.

# This script use used by docker but can be invoked from elsewhere, in order to run it
# interactively then these two commands can be used to set dnf into automatic mode.
# dnf --assumeyes install dnf-plugins-core
# dnf config-manager --save --setopt=assumeyes=True

# For fedora, java-11 is installed along with maven if we install maven from
# repo. But we need java-8 (1.8). The 'devel' package also needs to be
# installed specifically.

# Fedora is already on python 3.10 but Rocky has several versions available and defaults to the
# oldest so tell it to install the latest which is 3.9.

if [ -e /etc/fedora-release ]; then
        PACKAGES="java-1.8.0-openjdk-devel maven-openjdk8 python3-devel"
else
        PACKAGES="maven python39-devel"
fi

dnf -y --nodocs install \
    $PACKAGES \
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
    ndctl \
    numactl \
    numactl-devel \
    openmpi-devel \
    openssl-devel \
    patch \
    patchelf \
    sg3_utils \
    sudo \
    valgrind-devel \
    which \
    yasm
