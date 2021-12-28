#!/bin/sh

# Install OS updates and package.  Include basic tools and daos dependencies
# that come from the core repo.
# Clean up any repos afterwards to save space.
# Switch to dnf as it seems a bit faster.
# libatomic should be in this list, but can not for now due to CI
# post provisioning issue.
# *** Keep these in as much alphbetical order as possible ***

set -e

fedora_java=$1

dnf upgrade
dnf install \
    boost-python3-devel \
    clang \
    clang-tools-extra \
    cmake \
    CUnit-devel \
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
    python3-pyxattr \
    python3-tabulate \
    python3-scons \
    python3-yaml \
    sg3_utils \
    valgrind-devel \
    which \
    yasm

echo "is fedora java? $fedora_java"

if [ "$fedora_java" = "yes" ]; then
	dnf install java-1.8.0-openjdk-devel
	curl --output ./apache-maven-3.6.3-bin.tar.gz \
https://archive.apache.org/dist/maven/maven-3/3.6.3/binaries/apache-maven-3.6.3-bin.tar.gz \
&& tar -xf apache-maven-3.6.3-bin.tar.gz \
&& find ./ -name "mvn" | \
{ read -r d; f="$(dirname "$d")"; cp -r "$(readlink -f "$f")"/../* /usr/local; }
else
	dnf install maven
fi

dnf clean all
