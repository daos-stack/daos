#!/bin/sh -e

# Install packages as required for installing DAOS on SUSE distros.
# Include basic tools and daos dependencies that come from the core repos.
# This is for a daos test install environment, not a build environment.
# Switch to dnf as it seems a bit faster.
# *** Keep these in as much alphbetical order as possible ***
# CI environment expects git command to be present.
dnf --nodocs install \
    bzip2 \
    curl \
    clang \
    flex \
    git \
    go1.14 \
    go1.14-race \
    gzip \
    hwloc \
    ipmctl \
    java-1_8_0-openjdk \
    libltdl7 \
    lua-lmod \
    numactl \
    pciutils \
    scons \
    sg3_utils \
    sudo \
    which
