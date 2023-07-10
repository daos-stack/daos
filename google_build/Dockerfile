ARG BASE=rockylinux/rockylinux:8
FROM $BASE as builder

RUN dnf -y install git virtualenv epel-release dnf-plugins-core && \
    dnf config-manager --enable powertools && \
    dnf config-manager --save --setopt=assumeyes=True

RUN git clone --recurse-submodules https://github.com/daos-stack/daos.git --branch release/2.4

WORKDIR daos

RUN utils/scripts/install-el8.sh

RUN virtualenv venv
RUN source venv/bin/activate && \
  pip install --upgrade pip && \
  pip install -r requirements.txt

RUN source venv/bin/activate && \
  scons --jobs="$(nproc --all)" --build-deps=yes install PREFIX=/opt/daos

FROM $BASE

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
