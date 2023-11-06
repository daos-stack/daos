ARG BASE=rockylinux/rockylinux:8
FROM $BASE

# Set WORKDIR to a new directory and copy the build context contents into it.
# The build context is expected to be the daos repository root. This makes the
# source available to later RUN commands.
WORKDIR /workdir
COPY . ./

# Install build dependencies.
RUN dnf -y install virtualenv epel-release dnf-plugins-core && \
    dnf config-manager --enable powertools && \
    dnf config-manager --save --setopt=assumeyes=True
RUN utils/scripts/install-el8.sh

# Install Python packages in a virtualenv.
# BUILD_VENV is accessible to images that use this image as base.
ENV BUILD_VENV=/build_venv
RUN virtualenv $BUILD_VENV
RUN source $BUILD_VENV/bin/activate && \
  pip install --require-hashes -r google_build/base_requirements.txt && \
  pip install --require-hashes -r google_build/requirements.txt

# Cleanup. No need to store the source in the image.
WORKDIR /
RUN rm -rf /workdir
