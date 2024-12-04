ARG BASE=rockylinux/rockylinux:8
FROM $BASE

# Install runtime dependencies.
RUN dnf -y install epel-release && dnf config-manager --enable powertools
RUN dnf -y install \
  kmod hwloc openssl Lmod ipmctl daxctl ndctl numactl protobuf-c \
  json-c glibc-langpack-en e2fsprogs file sg3_utils \
  libevent libunwind libyaml librdmacm libuuid pciutils \
  libtool libtool libtool-ltdl libiscsi libipmctl libaio lmdb-libs

# Needed by envsubst in daos_server_wrapper.sh 
RUN dnf -y install gettext 
