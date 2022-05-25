#!/bin/sh -e

# Install OS updates and package.  Include basic tools and daos dependencies
# that come from the core repo.

# libatomic should be in this list, but can not for now due to CI
# post provisioning issue.

# This script use used by docker but can be invoked from elsewhere, in order to run it
# interactively then these this commands can be used to set apt-get into automatic mode.
# echo "APT::Get::Assume-Yes \"true\";" > /etc/apt/apt.conf.d/no-prompt

apt-get install \
    autoconf \
    build-essential \
    clang \
    clang-format \
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
    numactl \
    openjdk-8-jdk \
    patchelf \
    pciutils \
    pkg-config \
    python3-dev \
    python3-venv \
    uuid-dev \
    valgrind \
    yasm
