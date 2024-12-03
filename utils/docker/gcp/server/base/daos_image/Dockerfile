ARG BUILD_BASE=rockylinux/rockylinux:8
ARG BASE=rockylinux/rockylinux:8

FROM $BUILD_BASE as builder
ARG DAOS_BUILD_TYPE="release"
ARG GHA

# Set WORKDIR to a new directory and copy the build context contents into it.
# The build context is expected to be the daos repository root. This makes the
# source available to later RUN commands.
WORKDIR /workdir
COPY . ./


# Build DAOS. BUILD_VENV is inherited from the base image.
RUN source $BUILD_VENV/bin/activate && \
  scons --jobs="$(nproc --all)" --build-deps=yes install PREFIX=/opt/daos BUILD_TYPE="$DAOS_BUILD_TYPE" TARGET_TYPE="release"

RUN if [[ "$GHA" == "false" ]]; then \
  go build -C /workdir/utils/docker/gcp/server/base/src/gcs_copy -o /opt/daos/bin; \
fi

# Start a new image from the base image.
FROM $BASE

COPY --from=builder /opt/daos /opt/daos

RUN mkdir -p /var/run/daos_server
RUN ln -s /opt/daos/prereq/release/spdk/share/spdk /usr/share/spdk

# daos_server_wrapper.sh, git_has, git_describe will not exist upstream
# but this copy should succeed as long as one or more of these files exists
COPY --from=builder \
  /workdir/utils/docker/gcp/server/base/daos_image/daos_server.yml* \
  /workdir/utils/docker/gcp/server/base/daos_image/daos_server_wrapper.sh \
  /workdir/utils/docker/gcp/server/base/daos_image/git_hash \
  /workdir/utils/docker/gcp/server/base/daos_image/git_describe \
  /opt/daos/

COPY --from=builder /workdir/utils/docker/gcp/server/base/daos_image/daos_control.yml /opt/daos/etc/

ENV PATH=/opt/daos/bin:$PATH
