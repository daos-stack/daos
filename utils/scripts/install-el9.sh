#!/usr/bin/env bash
# (C) Copyright 2025 Google LLC
# Copyright 2026 Hewlett Packard Enterprise Development LP

# Install OS updates and packages as required for building DAOS on EL 9 and
# derivatives.  Include basic tools and daos dependencies that come from the core repos.

# This script use used by docker but can be invoked from elsewhere, in order to run it
# interactively then these two commands can be used to set dnf into automatic mode.
# dnf --assumeyes install dnf-plugins-core
# dnf config-manager --save --setopt=assumeyes=True

set -e

dnf_install_args="${1:-}"

: "${PYTHON_VERSION:=3.11}"

# shellcheck disable=SC2086
dnf --nodocs install ${dnf_install_args} \
    bzip2 \
    capstone-devel \
    cmake \
    createrepo \
    CUnit-devel \
    daxctl-devel \
    diffutils \
    e2fsprogs \
    fdupes \
    file \
    flex \
    gcc \
    gcc-c++ \
    git \
    glibc-langpack-en \
    golang \
    help2man \
    hdf5-devel \
    hwloc-devel \
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
    lz4-devel \
    Lmod \
    make \
    nasm \
    ndctl-devel \
    numactl-devel \
    openssl-devel \
    pandoc \
    patch \
    patchelf \
    pciutils-devel \
    protobuf-c-devel \
    python${PYTHON_VERSION}-devel \
    python${PYTHON_VERSION}-pip \
    rpm-build \
    sudo \
    valgrind-devel \
    which \
    ncurses-devel \
    yasm

if [[ "${INSTALL_BUILD_CI_ONLY:-}" != "true" ]]; then
    # Optional packages for full-featured images; can be skipped in essential-only mode.
    # shellcheck disable=SC2086
    dnf --nodocs install ${dnf_install_args} \
        clang \
        clang-tools-extra \
        fuse3 \
        gperftools-devel \
        graphviz \
        ipmctl \
        java-1.8.0-openjdk \
        ndctl \
        numactl \
        sg3_utils \
        squashfs-tools
fi

if [[ -z "${NO_OPENMPI_DEVEL+set}" ]]; then
    # shellcheck disable=SC2086
    dnf --nodocs install ${dnf_install_args} \
    	openmpi-devel 
fi

ruby_version=$(dnf module list ruby | grep -Eow "3\.[0-9]+" | tail -1)
# shellcheck disable=SC2086
dnf --nodocs install ${dnf_install_args} \
    "@ruby:${ruby_version}" \
    rubygems \
    rubygem-json

gem install fpm
