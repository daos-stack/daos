ARG BUILD_BASE=rockylinux/rockylinux:8
ARG BASE=rockylinux/rockylinux:8

FROM $BUILD_BASE as builder

# Set WORKDIR to a new directory and copy the build context contents into it.
# The build context is expected to be the daos repository root. This makes the
# source available to later RUN commands.
WORKDIR /workdir
COPY . ./

# Build DAOS. BUILD_VENV is inherited from the base image.
RUN source $BUILD_VENV/bin/activate && \
  scons --jobs="$(nproc --all)" --build-deps=yes install PREFIX=/opt/daos

# Start a new image from the base image.
FROM $BASE

COPY --from=builder /opt/daos /opt/daos

RUN mkdir -p /var/run/daos_server
RUN ln -s /opt/daos/prereq/release/spdk/share/spdk /usr/share/spdk

ENV PATH=/opt/daos/bin:$PATH
