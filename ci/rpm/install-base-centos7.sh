#!/bin/sh -e

# Install OS updates and package.  Include basic tools and daos dependencies
# that come from the core repo.
# This is for a daos test install environment, not a build environment.
# Switch to dnf as it seems a bit faster.
# *** Keep these in as much alphbetical order as possible ***

dnf --nodocs install \
    bzip2 \
    e2fsprogs \
    file \
    flex \
    fuse3 \
    golang \
    ipmctl \
    java-1.8.0-openjdk \
    lcov \
    Lmod \
    ndctl \
    numactl \
    pciutils \
    sg3_utils \
    sudo
