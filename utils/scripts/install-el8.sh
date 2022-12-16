#!/bin/sh -e

# Install OS updates and packages as required for building DAOS on EL 8 and
# derivatives.  Include basic tools and daos dependencies that come from the core repos.

# This script use used by docker but can be invoked from elsewhere, in order to run it
# interactively then these two commands can be used to set dnf into automatic mode.
# dnf --assumeyes install dnf-plugins-core
# dnf config-manager --save --setopt=assumeyes=True
set -e

dnf -y --nodocs install \
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
    meson \
    ndctl \
    ninja-build \
    numactl \
    numactl-devel \
    openmpi-devel \
    openssl-devel \
    patch \
    patchelf \
    python3-defusedxml \
    python3-devel \
    python3-distro \
    python3-junit_xml \
    python3-pip \
    python3-pyelftools \
    python3-pyxattr \
    python3-scons \
    python3-tabulate \
    python3-yaml \
    sg3_utils \
    sudo \
    valgrind-devel \
    which \
    yasm

# For fedora, java-11 is installed along with maven if we install maven from
# repo. But we need java-8 (1.8). The 'devel' package also needs to be
# installed specifically.

if [ -e /etc/fedora-release ]; then
        dnf -y install java-1.8.0-openjdk-devel maven-openjdk8
else
        dnf -y install maven
fi
