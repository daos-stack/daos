#!/usr/bin/env bash
# (C) Copyright 2025 Google LLC

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

# shellcheck disable=SC2086
zypper --non-interactive install ${dnf_install_args} \
    boost-devel \
    bzip2 \
    curl \
    clang \
    cmake \
    createrepo_c \
    cunit-devel \
    fdupes \
    flex \
    fuse3 \
    gcc \
    gcc-c++ \
    git \
    go \
    go-race \
    graphviz \
    gzip \
    hdf5-devel \
    hwloc-devel \
    java-17-openjdk-devel \
    libaio-devel \
    libasan8 \
    libcmocka-devel \
    libcapstone-devel \
    libevent-devel \
    libibverbs-devel \
    libiscsi-devel \
    libjson-c-devel \
    libltdl7 \
    liblz4-devel \
    libndctl-devel \
    libnl3-devel \
    libnuma-devel \
    libpsm2-devel \
    librdmacm-devel \
    libopenssl-devel \
    libprotobuf-c-devel \
    libtool \
    libunwind-devel \
    libuuid-devel \
    libyaml-devel \
    lua-lmod \
    make \
    maven \
    nasm \
    numactl \
    openmpi4-devel \
    pandoc \
    patch \
    patchelf \
    pciutils \
    pciutils-devel \
    python3-devel \
    rpm-build \
    scons \
    sg3_utils \
    sudo \
    valgrind-devel \
    which \
    yasm

# shellcheck disable=SC2086
#zypper --non-interactive install ${dnf_install_args} ruby-devel
#gem install json -v 2.7.6
#gem install dotenv -v 2.8.1
#gem install fpm -v 1.16.0
# --------------------------------------------------------------
# Have to build newer version of ruby to get fixed version of fpm.
# By default SLE 15 has Ruby 2.5, but we need at least Ruby 3.0 for
# fpm 1.17.0. This is to work around an issue where older version
# of ruby/fpm trying to use lchmod.
# --------------------------------------------------------------
# build deps
zypper --non-interactive install \
  gcc gcc-c++ make autoconf automake bison \
  libopenssl-devel libyaml-devel readline-devel zlib-devel \
  gdbm-devel libffi-devel tar gzip

# build a separate Ruby (example: 3.3.x or 3.2.x)
cd /tmp
curl -LO https://cache.ruby-lang.org/pub/ruby/3.3/ruby-3.3.7.tar.gz
tar xf ruby-3.3.7.tar.gz
cd ruby-3.3.7

./configure --prefix=/opt/ruby-3.3
make -j"$(nproc)"
make install

# use the new Ruby only for packaging
export PATH=/opt/ruby-3.3/bin:$PATH
ruby -v
gem -v

# update rubygems for this Ruby, then install fpm
gem update --system
gem install --no-document fpm -v 1.17.0
gem install json -v 2.7.6
gem install dotenv -v 2.8.1
# Done

#if [ ! -f /usr/bin/fpm ]; then
#    ln -s "$(basename "$(ls -1 /usr/bin/fpm.ruby*)")" /usr/bin/fpm
#fi

# ipmctl is only available on x86_64
if [ "$arch" = x86_64 ]; then
    # shellcheck disable=SC2086
    zypper --non-interactive install ${dnf_install_args} ipmctl-devel
fi
