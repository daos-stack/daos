#!/bin/sh

set -e
set -x

apt-get -y install scons python3-distro autoconf bash clang cmake curl \
	doxygen flex gcc git graphviz libaio-dev libboost-dev libcmocka0 \
	libcmocka-dev libcunit1-dev libevent-dev libibverbs-dev libiscsi-dev \
        libltdl-dev libnuma-dev librdmacm-dev libreadline6-dev      \
        libssl-dev libtool-bin libyaml-dev                          \
        locales make meson nasm ninja-build pandoc patch            \
        pylint python-dev python3-dev scons sg3-utils uuid-dev      \
        yasm valgrind libhwloc-dev man            \
        openjdk-8-jdk maven libopenmpi-dev patchelf libjson-c-dev   \
        liblz4-dev python3-distro \
	ndctl golang-go python-distro

set +e
add-apt-repository universe
apt-get search fuse
apt-get -y install fuse3 libfuse3-dev
