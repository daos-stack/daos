ARG BASE=rockylinux/rockylinux:8
FROM $BASE as builder

# Set WORKDIR to a new directory and copy the build context contents into it.
# The build context is expected to be the daos repository root. This makes the
# source available to later RUN commands.
WORKDIR /daos
COPY . ./

# Install build dependencies.
RUN dnf -y install virtualenv epel-release dnf-plugins-core && \
    dnf config-manager --enable powertools && \
    dnf config-manager --save --setopt=assumeyes=True
RUN utils/scripts/install-el8.sh

# Install Python packages in a virtualenv.
ARG VENV=/tmp/build_venv
RUN virtualenv $VENV
RUN source $VENV/bin/activate && \
  pip install --upgrade pip && \
  pip install -r google_build/requirements.txt

# Build DAOS.
RUN source $VENV/bin/activate && \
  scons --jobs="$(nproc --all)" --build-deps=yes install PREFIX=/opt/daos

# Start a new image from the base image.
FROM $BASE

# Install runtime dependencies.
RUN dnf -y install epel-release && dnf config-manager --enable powertools
RUN dnf -y install \
  kmod hwloc openssl Lmod ipmctl ndctl numactl protobuf-c \
  json-c glibc-langpack-en e2fsprogs file sg3_utils \
  libevent libunwind libyaml librdmacm libuuid \
  libtool libtool libtool-ltdl libiscsi libipmctl libaio lmdb-libs

COPY --from=builder /opt/daos /opt/daos

RUN mkdir -p /var/run/daos_server
RUN ln -s /opt/daos/prereq/release/spdk/share/spdk /usr/share/spdk

ENV PATH=/opt/daos/bin:$PATH
