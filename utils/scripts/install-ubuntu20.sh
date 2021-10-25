#!/bin/sh

# Install OS updates and package.  Include basic tools and daos dependencies
# that come from the core repo.
# Clean up any repos afterwards to save space.
# Switch to dnf as it seems a bit faster.
# libatomic should be in this list, but can not for now due to CI
# post provisioning issue.
# *** Keep these in as much alphbetical order as possible ***

export DEBIAN_FRONTEND=noninteractive
set -e

apt-get -y update
apt-get -y install apt-utils
apt-get -y upgrade
apt-get -y install \
    autoconf \
    build-essential \
    clang \
    cmake \
    curl \
    fuse3 \
    git \
    golang-go \
    kmod \
    libaio-dev \
    libboost-dev \
    libcmocka-dev \
    libcunit1-dev \
    libfuse3-dev \
    libhwloc-dev \
    libibverbs-dev \
    libipmctl-dev \
    libjson-c-dev \
    liblz4-dev \
    libnuma-dev \
    libopenmpi-dev \
    librdmacm-dev \
    libssl-dev \
    libtool-bin \
    libunwind-dev \
    libyaml-dev \
    locales \
    maven \
    meson \
    numactl \
    ninja-build \
    openjdk-8-jdk \
    patchelf \
    pciutils \
    pkg-config \
    python3-defusedxml \
    python3-dev \
    python3-distro \
    python3-junit.xml \
    python3-pyelftools \
    python3-pyxattr \
    python3-tabulate \
    scons \
    sudo \
    uuid-dev \
    valgrind \
    yasm
apt-get clean all
