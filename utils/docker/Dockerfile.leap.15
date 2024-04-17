# Copyright 2018-2024 Intel Corporation
# All rights reserved.
#
# 'recipe' for Docker to build an image of Leap based
# environment for building the DAOS project.
#

# Pull base image
ARG POINT_RELEASE=
ARG BASE_DISTRO=registry.opensuse.org/opensuse/leap-dnf:15$POINT_RELEASE
FROM $BASE_DISTRO as basic
LABEL maintainer="daos@daos.groups.io"
# Needed for later use of BASE_DISTRO
ARG BASE_DISTRO

# Intermittent cache-bust.  Used to reduce load on the actual CB1 later.
ARG CB0

ARG REPO_FILE_URL
ARG JENKINS_URL
ARG REPOS
# script to install OS updates basic tools and daos dependencies
COPY ./utils/scripts/install-leap15.sh /tmp/install.sh
# script to setup local repo if available
COPY ./utils/scripts/helpers/repo-helper-leap15.sh /tmp/repo-helper.sh

RUN chmod +x /tmp/repo-helper.sh /tmp/install.sh && \
    /tmp/repo-helper.sh &&                          \
    rm -f /tmp/repo-helper.sh

FROM basic
# Install OS updates and package.  Include basic tools and daos dependencies
RUN dnf upgrade &&        \
    /tmp/install.sh &&    \
    dnf clean all &&      \
    rm -f /tmp/install.sh

# Add DAOS users
ARG UID=1000
COPY ./utils/scripts/helpers/daos-server-user-setup.sh \
     /tmp/daos-server-user-setup.sh
RUN set -e;                                    \
    chmod +x /tmp/daos-server-user-setup.sh && \
    /tmp/daos-server-user-setup.sh
RUN useradd --no-log-init --user-group --create-home --shell /bin/bash daos_agent
RUN echo "daos_agent:daos_agent" | chpasswd

# Create directory for DAOS backend storage
RUN mkdir -p /opt/daos /mnt/daos /var/run/daos_server /var/run/daos_agent /home/daos/pre /home/daos/daos &&   \
    chown -R daos_server.daos_server /opt/daos /mnt/daos /var/run/daos_server /home/daos &&  \
    chown daos_agent.daos_agent /var/run/daos_agent

USER daos_server:daos_server

# Setup a python venv so that python packages can be installed locally.
RUN python3 -m venv /home/daos/venv
ENV PATH=/home/daos/venv/bin:$PATH
ENV VIRTUAL_ENV=/home/daos/venv/

# Install latest versions of python tools.
COPY requirements-build.txt requirements-utest.txt ./
RUN python3 -m pip --no-cache-dir install --upgrade pip && \
    python3 -m pip --no-cache-dir install -r requirements-build.txt -r requirements-utest.txt

WORKDIR /home/daos/pre
RUN mkdir -p /home/daos/pre/site_scons/prereq_tools /home/daos/pre/site_scons/components
COPY --chown=daos_server:daos_server SConstruct .
COPY --chown=daos_server:daos_server site_scons/prereq_tools site_scons/prereq_tools
COPY --chown=daos_server:daos_server site_scons/components site_scons/components
COPY --chown=daos_server:daos_server utils/build.config utils/

# Control what to build.  By default Dockerfiles build everything to allow for
# ease-of-use for users, however in CI everything is turned off and then
# selectively enabled.  Turning off any step disables all future steps.
ARG DAOS_DEPS_BUILD=yes
ARG DAOS_KEEP_BUILD=no
ARG DAOS_TARGET_TYPE=release

# Now do an update to ensure software is up to date for the deps build.  If the
# src hasn't changed then this won't do anything, but if it has then we want to
# ensure that latest dependencies are used.
# The dnf upgrade can add or re-enable distro repositories.
USER root:root
RUN [ "$DAOS_DEPS_BUILD" != "yes" ] || {                                                             \
        dnf upgrade --exclude=fuse,fuse-libs,fuse-devel,libraft0,raft-devel,mercury,mercury-devel && \
        dnf clean all;                                                                               \
    }
USER daos_server:daos_server

ARG DEPS_JOBS=1

RUN [ "$DAOS_DEPS_BUILD" != "yes" ] || {                            \
        scons --build-deps=only --jobs $DEPS_JOBS PREFIX=/opt/daos  \
              TARGET_TYPE=$DAOS_TARGET_TYPE &&                      \
        ([ "$DAOS_KEEP_BUILD" != "no" ] || /bin/rm -rf build *.gz); \
    }
USER root:root

# select compiler to use
# Load the COMPILER arg early, and optionally install the Intel compiler
# Do this before the CB1 to make best use of cache as this is a big
# download, and make it optional on the value of COMPILER for the same
# reason.  Tell zypper to only consider the oneAPI repo here using the
# discouraged --repo flag, however this prevents an update of the entire
# upstream package list which would be immediately discarded, then reloaded
# below in the general update section.  Only install the compilers at
# ~900Mb rather than the entire stack at ~3Gb download.
ARG COMPILER=gcc
RUN if [ "$COMPILER" = "icc" ]; then                                                                     \
        if [ -z "$REPO_FILE_URL" ] || true; then                                                         \
            rpm --import https://yum.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB && \
            dnf config-manager --add-repo https://yum.repos.intel.com/oneapi oneAPI;                     \
        fi;                                                                                              \
        dnf install intel-oneapi-compiler-dpcpp-cpp &&                                                   \
        dnf clean all;                                                                                   \
    fi

# force an upgrade to get any newly built RPMs, but only if CB1 is set.
ARG CB1
RUN [ -z "$CB1" ] || {                                                                         \
        dnf upgrade --exclude=fuse,fuse-libs,fuse-devel,libraft0,raft-devel,mercury,mercury-devel && \
        dnf clean all;                                                                               \
    }
USER daos_server:daos_server

# Set a label.  This is useful for searching for DAOS images, but is also used
# in github-actions to prune elements of the dockerfile below this point.
LABEL DAOS=true

WORKDIR /home/daos/daos/
COPY --chown=daos_server:daos_server VERSION LICENSE ftest.sh SConstruct requirements-ftest.txt .clang-format ./
COPY --chown=daos_server:daos_server site_scons site_scons
COPY --chown=daos_server:daos_server utils utils
COPY --chown=daos_server:daos_server src src

ARG JOBS=$DEPS_JOBS
ARG DAOS_BUILD_TYPE=$DAOS_TARGET_TYPE
ARG DAOS_BUILD=$DAOS_DEPS_BUILD

# Build DAOS
RUN [ "$DAOS_BUILD" != "yes" ] || {                                        \
        scons --jobs $JOBS install PREFIX=/opt/daos COMPILER=$COMPILER     \
              BUILD_TYPE=$DAOS_BUILD_TYPE TARGET_TYPE=$DAOS_TARGET_TYPE && \
        ([ "$DAOS_KEEP_BUILD" != "no" ] || /bin/rm -rf build) &&           \
        go clean -cache &&                                                 \
        cp -r utils/config/examples /opt/daos;                             \
    }

# Set environment variables
ENV PATH=/opt/daos/bin:$PATH
ENV FI_SOCKETS_MAX_CONN_RETRY=1

# Build java and hadoop bindings
WORKDIR /home/daos/daos/src/client/java

ARG DAOS_JAVA_BUILD=$DAOS_BUILD

RUN [ "$DAOS_JAVA_BUILD" != "yes" ] || {                                                      \
        mkdir /home/daos/.m2 &&                                                               \
        cp /home/daos/daos/utils/scripts/helpers/maven-settings.xml.in /home/daos/.m2/settings.xml &&      \
        mvn clean install -ntp -T 1C -DskipITs -Dgpg.skip -Ddaos.install.path=/opt/daos;      \
    }
WORKDIR /home/daos

ARG DAOS_KEEP_SRC=no
# Remove local copy
RUN [ "$DAOS_KEEP_SRC" != "no" ] || rm -rf /home/daos/daos /home/daos/pre
