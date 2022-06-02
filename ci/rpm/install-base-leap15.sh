#!/bin/sh -e

# Install OS updates and package.  Include basic tools and daos dependencies
# that come from the core repo.
# This is for a daos test install environment, not a build environment.
# Switch to dnf as it seems a bit faster.
# *** Keep these in as much alphbetical order as possible ***

dnf --nodocs install \
    bzip2 \
    curl \
    clang \
    flex \
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
