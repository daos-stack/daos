#!/bin/sh -e

# Install OS updates and packages as required for building DAOS on EL 8 and
# derivatives.  Include basic tools and daos dependencies that come from the core repos.
# This is for a daos test install environment, not a build environment.

# This script use used by docker but can be invoked from elsewhere, in order to run it
# interactively then these two commands can be used to set dnf into automatic mode.
# dnf --assumeyes install dnf-plugins-core
# dnf config-manager --save --setopt=assumeyes=True

dnf --nodocs install \
    bzip2 \
    diffutils \
    e2fsprogs \
    file \
    flex \
    fuse3 \
    glibc-langpack-en \
    golang \
    ipmctl \
    java-1.8.0-openjdk \
    Lmod \
    ndctl \
    numactl \
    sg3_utils \
    sudo \
    systemd \
    which
