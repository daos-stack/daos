#
# Copyright 2019, Intel Corporation
#
# 'recipe' for Docker to build an RPM
#

# Pull base image
FROM suse/sles:12.3
MAINTAINER Brian J. Murrell <brian.murrell@intel.com>

# use same UID as host and default value of 1000 if not specified
ARG UID=1000

# Add build user (to keep rpmbuild happy)
ENV USER build
ENV PASSWD build
RUN useradd -u $UID -ms /bin/bash $USER
RUN groupadd -g $UID $USER
RUN echo "$USER:$PASSWD" | chpasswd

# Install basic tools
RUN zypper --non-interactive update
# basic building
RUN zypper --non-interactive install make rpm-build curl createrepo git    \
                                     lsb-release autoconf automake libtool \
                                     ca-certificates-mozilla
# libfabric
RUN zypper --non-interactive install rdma-core-devel libnl3-devel        \
                                     infinipath-psm-devel valgrind-devel

ARG JENKINS_URL=""

# mercury
RUN zypper --non-interactive ar --gpgcheck-allow-unsigned -f ${JENKINS_URL}job/daos-stack/job/openpa/job/master/lastSuccessfulBuild/artifact/artifacts/sles12.3/ openpa;       \
    zypper --non-interactive ar --gpgcheck-allow-unsigned -f ${JENKINS_URL}job/daos-stack/job/libfabric/job/master/lastSuccessfulBuild/artifact/artifacts/sles12.3/ libfabric; \
    zypper --non-interactive ref openpa libfabric
# our libfabric conflicts with libfabric1
# TODO: consider if we should rename ours or replace libfabric1, etc.
RUN if rpm -q libfabric1; then zypper --non-interactive remove libfabric1; fi
RUN zypper --non-interactive install gcc-c++
RUN zypper --non-interactive --no-gpg-check install openpa-devel      \
                                                    libfabric-devel   \
                                                    cmake boost-devel
# pmix
RUN zypper --non-interactive install libevent-devel

# ompi
RUN zypper --non-interactive ar -f https://download.opensuse.org/repositories/science:/HPC:/SLE12SP3_Missing/SLE_12_SP3/ hwloc;                                                     \
    zypper --non-interactive --gpg-auto-import-keys ref hwloc;                                                                                                                      \
    zypper --non-interactive ar --gpgcheck-allow-unsigned -f ${JENKINS_URL}job/daos-stack/job/pmix/job/master/lastSuccessfulBuild/artifact/artifacts/sles12.3/ pmix; \
    zypper --non-interactive ref pmix
RUN zypper --non-interactive install hwloc-devel pmix-devel flex

# scons
RUN zypper --non-interactive install fdupes

# cart
RUN zypper --non-interactive ar --gpgcheck-allow-unsigned -f ${JENKINS_URL}job/daos-stack/job/mercury/job/master/lastSuccessfulBuild/artifact/artifacts/sles12.3/ mercury; \
    zypper --non-interactive ar --gpgcheck-allow-unsigned -f ${JENKINS_URL}job/daos-stack/job/ompi/job/master/lastSuccessfulBuild/artifact/artifacts/sles12.3/ ompi;       \
    zypper --non-interactive ar --gpgcheck-allow-unsigned -f ${JENKINS_URL}job/daos-stack/job/scons/job/master/lastSuccessfulBuild/artifact/artifacts/sles12.3/ scons;     \
    zypper --non-interactive --gpg-auto-import-keys ref mercury ompi scons
RUN zypper --non-interactive install scons libyaml-devel mercury-devel    \
                                     ompi-devel openssl-devel
RUN zypper --non-interactive ar https://download.opensuse.org/repositories/devel:libraries:c_c++/SLE_12_SP3/devel:libraries:c_c++.repo; \
    zypper --non-interactive --gpg-auto-import-keys ref 'A project for basic libraries shared among multiple projects (SLE_12_SP3)'
RUN zypper --non-interactive install libcmocka-devel

# fio
RUN zypper --non-interactive install libpmem-devel libaio-devel     \
                                     libpmemblk-devel librbd-devel  \
                                     libnuma-devel

# isa-l
RUN zypper --non-interactive install yasm

# spdk
RUN zypper --non-interactive ar --gpgcheck-allow-unsigned -f ${JENKINS_URL}job/daos-stack/job/dpdk/job/master/lastSuccessfulBuild/artifact/artifacts/sles12.3/ dpdk; \
    zypper --non-interactive ar --gpgcheck-allow-unsigned -f ${JENKINS_URL}job/daos-stack/job/fio/job/master/lastSuccessfulBuild/artifact/artifacts/sles12.3/ fio;   \
    zypper --non-interactive --gpg-auto-import-keys ref dpdk fio
RUN zypper --non-interactive install dpdk-devel fio-src libiscsi-devel cunit-devel

# meson
RUN zypper --non-interactive install python-rpm-macros python3-base

# fuse
RUN zypper --non-interactive ar --gpgcheck-allow-unsigned -f ${JENKINS_URL}job/daos-stack/job/meson/job/master/lastSuccessfulBuild/artifact/artifacts/sles12.3/ meson; \
    zypper --non-interactive --gpg-auto-import-keys ref meson
RUN zypper --non-interactive install meson ninja

# pmdk
RUN zypper --non-interactive install man libunwind-devel gdb bc

# daos
RUN zypper --non-interactive ar --gpgcheck-allow-unsigned -f ${JENKINS_URL}job/daos-stack/job/spdk/job/master/lastSuccessfulBuild/artifact/artifacts/sles12.3/ spdk;             \
    zypper --non-interactive ar --gpgcheck-allow-unsigned -f ${JENKINS_URL}job/daos-stack/job/isa-l/job/master/lastSuccessfulBuild/artifact/artifacts/sles12.3/ isa-l;           \
    zypper --non-interactive ar --gpgcheck-allow-unsigned -f ${JENKINS_URL}job/daos-stack/job/protobuf-c/job/master/lastSuccessfulBuild/artifact/artifacts/sles12.3/ protobuf-c; \
    zypper --non-interactive ar --gpgcheck-allow-unsigned -f ${JENKINS_URL}job/daos-stack/job/fuse/job/master/lastSuccessfulBuild/artifact/artifacts/sles12.3/ fuse;             \
    zypper --non-interactive ar --gpgcheck-allow-unsigned -f ${JENKINS_URL}job/daos-stack/job/pmdk/job/master/lastSuccessfulBuild/artifact/artifacts/sles12.3/ pmdk;             \
    zypper --non-interactive ar --gpgcheck-allow-unsigned -f ${JENKINS_URL}job/daos-stack/job/raft/job/master/lastSuccessfulBuild/artifact/artifacts/sles12.3/ raft;             \
    zypper --non-interactive ar --gpgcheck-allow-unsigned -f ${JENKINS_URL}job/daos-stack/job/argobots/job/master/lastSuccessfulBuild/artifact/artifacts/sles12.3/ argbots;      \
    zypper --non-interactive ar --gpgcheck-allow-unsigned -f ${JENKINS_URL}job/daos-stack/job/cart/job/daos_devel/lastSuccessfulBuild/artifact/artifacts/sles12.3/ cart;         \
    zypper --non-interactive ar http://download.opensuse.org/repositories/devel:/languages:/go/SLE_12_SP3_Backports/devel:languages:go.repo;                                                    \
    zypper --non-interactive ar https://download.opensuse.org/repositories/home:/jhli/SLE_15/home:jhli.repo;                                                                                    \
    zypper --non-interactive --gpg-auto-import-keys ref spdk isa-l protobuf-c                                                                                                                   \
                                                        fuse pmdk raft argbots                                                                                                                  \
                                                        cart fio                                                                                                                                \
                                                        'The Go Programming Language (SLE_12_SP3_Backports)'                                                                                    \
                                                        'home:jhli (SLE_15)'
ARG go_version=1.10
ENV go_version=${go_version}
# Note the need to erase old go packages: CORCI-697
RUN zypper --non-interactive rm libpmem1;                                      \
    zypper --non-interactive install spdk-devel spdk-tools libisa-l-devel      \
                                     protobuf-c-devel fuse-devel libpmem-devel \
                                     libpmemobj-devel raft-devel               \
                                     argobots-devel cart-devel                 \
                                     ipmctl-devel readline-devel               \
                                     go${go_version} fio\ \<\ 3.4;             \
    rpm -qa | grep ^go\[1-9\]\* | grep -v ^go${go_version/\./\\.} |            \
        xargs zypper --non-interactive rm

# force an upgrade to get any newly built RPMs
ARG CACHEBUST=1
RUN zypper --non-interactive up
