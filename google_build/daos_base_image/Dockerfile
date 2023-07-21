ARG BASE=rockylinux/rockylinux:8
FROM $BASE

# Install runtime dependencies.
RUN dnf -y install epel-release && dnf config-manager --enable powertools
RUN dnf -y install \
  kmod hwloc openssl Lmod ipmctl ndctl numactl protobuf-c \
  json-c glibc-langpack-en e2fsprogs file sg3_utils \
  libevent libunwind libyaml librdmacm libuuid \
  libtool libtool libtool-ltdl libiscsi libipmctl libaio lmdb-libs
