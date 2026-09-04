#!/usr/bin/env bash
# (C) Copyright 2025 Google LLC
# Copyright 2026 Hewlett Packard Enterprise Development LP

# Install OS updates and package.  Include basic tools and daos dependencies
# that come from the core repo.
# Clean up any repos afterwards to save space.
# Switch to dnf as it seems a bit faster.
# libatomic should be in this list, but can not for now due to CI
# post provisioning issue.
# *** Keep these in as much alphabetical order as possible ***

set -e

arch=$(uname -i)

dnf_install_args="${1:-}"

: "${PYTHON_VERSION:=3.11}"

# shellcheck disable=SC2086
dnf --nodocs install ${dnf_install_args} \
    boost-devel \
    bzip2 \
    cmake \
    createrepo_c \
    cunit-devel \
    fdupes \
    flex \
    gcc \
    gcc-c++ \
    git \
    go \
    go-race \
    hdf5-devel \
    hwloc-devel \
    libaio-devel \
    libasan8 \
    libcmocka-devel \
    libcapstone-devel \
    libevent-devel \
    libibverbs-devel \
    libiscsi-devel \
    libjson-c-devel \
    liblz4-devel \
    libndctl-devel \
    libnl3-devel \
    libnuma-devel \
    libpsm2-devel \
    librdmacm-devel \
    libopenssl-devel \
    libprotobuf-c-devel \
    libtool \
    libucp-devel \
    libucs-devel \
    libuct-devel \
    libunwind-devel \
    libuuid-devel \
    libyaml-devel \
    lua-lmod \
    make \
    nasm \
    openmpi3-devel \
    pandoc \
    patch \
    patchelf \
    pciutils-devel \
    python${PYTHON_VERSION//./}-devel \
    rpm-build \
    scons \
    sudo \
    valgrind-devel \
    which \
    yasm

if [[ "${INSTALL_BUILD_CI_ONLY:-}" != "true" ]]; then
    # Optional packages for full-featured images; can be skipped in essential-only mode.
    # shellcheck disable=SC2086
    dnf --nodocs install ${dnf_install_args} \
        clang \
        fuse3 \
        gperftools-devel \
        graphviz \
        gzip \
        ipmctl \
        java-1_8_0-openjdk-devel \
        maven \
        ndctl \
        numactl \
        sg3_utils
fi

# ipmctl is only available on x86_64
if [ "$arch" = x86_64 ]; then
    # shellcheck disable=SC2086
    dnf --nodocs install ${dnf_install_args} \
        ipmctl
fi
# shellcheck disable=SC2086
dnf --nodocs install ${dnf_install_args} ruby-devel
gem install json -v 2.7.6
gem install dotenv -v 2.8.1
gem install fpm -v 1.16.0
if [ ! -f /usr/bin/fpm ]; then
    ln -s "$(basename "$(ls -1 /usr/bin/fpm.ruby*)")" /usr/bin/fpm
fi

