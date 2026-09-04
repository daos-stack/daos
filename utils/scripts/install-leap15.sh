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
    binutils \
    boost-devel \
    bzip2 \
    cmake \
    cpio \
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

# According to https://pkgs.org/search/?q=lua-lmod, Leap 15.6 only has
# lua-lmod-8.7.34.
# This 8.7.34 version has a problem with loading modules, as described in
# https://github.com/TACC/Lmod/issues/687.
# This affects MPI detection in scons, so DAOS cannot be built with MPI support.
# A custom source of the package is required in CI to install a valid version
# of lua-lmod and its dependencies:
# opensuse-network-cluster for lua-lmod (>=8.7.55)
# opensuse-oss for lua53, lua53-luaterm,, sqlite3-tcl, tcl
# opensuse-devel-languages-lua for lua53-luafilesystem, lua53-luaposix
# repo-helper-leap15.sh ensures the network-cluster (and, when using
# an artifact server, oss-proxy) repos are present before this script runs.
if dnf repolist --disabled '*network-cluster*' 2>/dev/null | tail -n +2 | grep -q .; then
  dnf -y remove lua-lmod
  dnf -y --nogpgcheck install lua-lmod \
      --repo '*network-cluster*' \
      --repo '*oss-proxy*'
fi

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
# ruby-devel ships its own default /etc/gemrc, which rpm installs in place
# of the one repo-helper-leap15.sh wrote, saving ours as /etc/gemrc.rpmorig.
if [ -f /etc/gemrc.rpmorig ]; then
    mv /etc/gemrc.rpmorig /etc/gemrc
fi
# gem does not reliably auto-load /etc/gemrc on every distro/build; force it.
export GEMRC=/etc/gemrc
gem install json -v 2.7.6
gem install dotenv -v 2.8.1
gem install fpm -v 1.16.0
if [ ! -f /usr/bin/fpm ]; then
    ln -s "$(basename "$(ls -1 /usr/bin/fpm.ruby*)")" /usr/bin/fpm
fi
