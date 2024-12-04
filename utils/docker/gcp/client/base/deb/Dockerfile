ARG GO_BUILDER
ARG BASE_IMAGE_BUILDER

FROM $GO_BUILDER as gobuilder
FROM $BASE_IMAGE_BUILDER as builder

ARG GIT_REPOSITORY
ARG GIT_BRANCH_OR_TAG
ARG DAOS_BUILD_TYPE=release
ARG DAOS_SRC_DIR

RUN echo $(TZ=PDT date +'%Y%m%d.%H%M%S') > /tmp/build_ts.txt

# Copy the previously cloned daos
COPY "${DAOS_SRC_DIR}" /daos/

# Making bash a default shell
SHELL ["/bin/bash", "-c"]

WORKDIR /

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    curl gpg
# Install basic tools
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
            autoconf bash ca-certificates curl debhelper dh-make        \
            dpkg-dev dh-python doxygen gcc git git-buildpackage locales \
            make patch pbuilder pkg-config python3-dev python3-distro   \
            python3-distutils rpm scons wget cmake valgrind

RUN daos/utils/docker/gcp/clone_repos.sh

# Install git and packages required by the build process
RUN apt-get update && apt-get install -y \
  gawk \
  virtualenv

# Remove openjdk from the installation list, since we believe it's not needed
RUN gawk -i inplace '!/openjdk/' daos/utils/scripts/install-ubuntu.sh
# Prevent interactive prompts
RUN echo "APT::Get::Assume-Yes \"true\";" > /etc/apt/apt.conf.d/no-prompt
# Install all the packages needed for the build
RUN daos/utils/scripts/install-ubuntu.sh

RUN rm -rf /usr/bin/go
COPY --from=gobuilder /usr/local/go /usr

WORKDIR /daos

#
RUN virtualenv venv && \
  source venv/bin/activate && \
  pip install --upgrade pip && \
  { [[ -f requirements-build.txt ]] && pip install -r requirements-build.txt || pip install -r requirements.txt; } && \
  scons --jobs="$(nproc --all)" --build-deps=yes client install GOOGLE_INTERNAL_UTILS=0 BUILD_TYPE="$DAOS_BUILD_TYPE" TARGET_TYPE="release"

WORKDIR /

  #mkdir -p debs/isa-l/usr/bin/libisal && \
  #mkdir -p debs/libisa-l_crypto/usr/bin/libisal_crypto && \
  #mkdir -p debs/mercury/usr/bin/mercury && \
  #mkdir -p debs/libfabric/usr/bin/libfabric && \

# RUN mkdir -p debs/libfabric/usr/lib/libfabric && \
#   mkdir -p debs/libfabric/usr/share/libfabric && \
#   mkdir -p debs/daos-client/usr && \
#   mkdir -p debs/isa-l/usr/lib/libisal && \
#   mkdir -p debs/isa-l/usr/share/libisal && \
#   mkdir -p debs/libisa-l_crypto/usr/lib/libisal_crypto && \
#   mkdir -p debs/libisa-l_crypto/usr/share/libisal_crypto && \
#   mkdir -p debs/mercury/usr/lib/mercury && \
#   mkdir -p debs/mercury/usr/share/mercury

RUN mkdir -p debs/libfabric/usr/lib && \
  mkdir -p debs/daos-client/usr/lib64 && \
  mkdir -p debs/daos-client/etc/daos && \
  mkdir -p debs/isa-l/usr/lib && \
  mkdir -p debs/libisa-l_crypto/usr/lib && \
  mkdir -p debs/mercury/usr/lib

# lib fabric
RUN cp -r libfabric/debian/ debs/libfabric/DEBIAN
RUN cp -r daos/install/prereq/release/ofi/lib/* debs/libfabric/usr/lib/
# RUN cp -r daos/install/prereq/release/ofi/bin/* debs/libfabric/usr/bin/libfabric/
RUN cp -r daos/install/prereq/release/ofi/share/ debs/libfabric/usr/
run rm -rf debs/libfabric/DEBIAN/patches/
run rm -rf debs/libfabric/DEBIAN/source/

RUN echo -e \
'Package: libfabric1\n\
Architecture: amd64\n\
Maintainer: Debian HPC Team <debian-hpc@lists.debian.org>\n\
Version: ${VERSION}\n\
Description: libfabric communication library\n\
 Libfabric is a communication library that exports interfaces for\n\
 fabric services to applications.  Libfabric is the core component\n\
 of the Open Fabrics Interfaces (OFI) framework.\n\
 .\n\
 This package contains the shared libraries.' | \
  VERSION=$(cat "libfabric/libfabric.spec" | awk -v VERSION_SUFFIX=$(cat /tmp/build_ts.txt) '/^Version:/ {split($2,a,"~"); if (a[2]!="") {print a[1] "." VERSION_SUFFIX "~" a[2]} else {print a[1] "." VERSION_SUFFIX}; }') envsubst | \
  tee /debs/libfabric/DEBIAN/control

RUN dpkg-deb -Zxz --build debs/libfabric/

#isa-l
RUN cp -r isa-l/debian/ debs/isa-l/DEBIAN
RUN cp -r daos/install/prereq/release/isal/lib/* debs/isa-l/usr/lib/

run rm -rf debs/isa-l/DEBIAN/source/
run rm -rf debs/isa-l/DEBIAN/upstream/
run rm -rf debs/isa-l/DEBIAN/tests/
# RUN cp -r daos/install/prereq/release/isal/bin/* debs/isa-l/usr/bin/libisal/
# RUN cp -r daos/install/prereq/release/isal/share/* debs/isa-l/usr/share/libisal/
# dpkg-deb --build --root-owner-group debs/isa-l/

RUN echo -e \
'Package: libisal2\n\
Architecture: amd64\n\
Maintainer: Debian OpenStack <team+openstack@tracker.debian.org>\n\
Version: ${VERSION}\n\
Description: Intel(R) Intelligent Storage Acceleration Library - shared library\n\
 Collection of low-level functions used in storage applications.\n\
 Contains fast erasure codes that implement a general\n\
 Reed-Solomon type encoding for blocks of data that helps protect against\n\
 erasure of whole blocks. The general ISA-L library contains an expanded\n\
 set of functions used for data protection, hashing, encryption, etc.\n\
 .\n\
 This package contains the shared library.' | \
  VERSION=$(cat "isa-l/isa-l.spec" | awk -v VERSION_SUFFIX=$(cat /tmp/build_ts.txt) '/^Version:/ {split($2,a,"~"); if (a[2]!="") {print a[1] "." VERSION_SUFFIX "~" a[2]} else {print a[1] "." VERSION_SUFFIX}; }') envsubst | \
  tee /debs/isa-l/DEBIAN/control

RUN dpkg-deb -Zxz --build debs/isa-l/

#isa-l_crypto
RUN cp -r isa-l_crypto/debian/ debs/libisa-l_crypto/DEBIAN
RUN cp -r daos/install/prereq/release/isal_crypto/lib/* debs/libisa-l_crypto/usr/lib/
RUN rm -rf debs/libisa-l_crypto/DEBIAN/source/
# dpkg-deb --build --root-owner-group debs/libisa-l_crypto/

RUN echo -e \
'Package: libisal2-crypto\n\
Architecture: amd64\n\
Maintainer: daos-stack <daos@daos.groups.io>\n\
Version: ${VERSION}\n\
Description: Intel(R) Intelligent Storage Acceleration Library Crypto - shared library\n\
 Collection of optimized low-level functions targeting storage applications.\n\
 ISA-L_crypto includes:\n\
 .\n\
   Multi-buffer hashes - run multiple hash jobs together on one core for\n\
                         much better throughput than single-buffer versions.\n\
    SHA1, SHA256, SHA512, MD5\n\
 .\n\
   Multi-hash - Get the performance of multi-buffer hashing with a\n\
                single-buffer interface.\n\
 .\n\
   Multi-hash + murmur - run both together.\n\
 .\n\
   AES - block ciphers\n\
     XTS, GCM, CBC\n\
 .\n\
   Rolling hash - Hash input in a window which moves through the input\n\
 .\n\
 This package contains the shared library.' | \
  VERSION=$(cat "isa-l_crypto/isa-l_crypto.spec" | awk -v VERSION_SUFFIX=$(cat /tmp/build_ts.txt) '/^Version:/ {split($2,a,"~"); if (a[2]!="") {print a[1] "." VERSION_SUFFIX "~" a[2]} else {print a[1] "." VERSION_SUFFIX}; }') envsubst | \
  tee /debs/libisa-l_crypto/DEBIAN/control

RUN dpkg-deb -Zxz --build debs/libisa-l_crypto/

#mercury
RUN cp -r mercury/debian/ debs/mercury/DEBIAN
RUN cp -r daos/install/prereq/release/mercury/lib/* debs/mercury/usr/lib/
# RUN cp -r daos/install/prereq/release/mercury/bin/* debs/mercury/usr/bin/mercury/
RUN cp -r daos/install/prereq/release/mercury/share/* debs/mercury/usr/share/

RUN rm -rf debs/mercury/DEBIAN/source/
# dpkg-deb --build --root-owner-group debs/mercury/

RUN echo -e \
'Package: libmercury1\n\
Architecture: amd64\n\
Maintainer: daos-stack <daos@daos.groups.io>\n\
Version: ${VERSION}\n\
Description: high-performance Computing C RPC library\n\
 Support for remote function calls which include large data arguments.\n\
 .\n\
 Mercury is partially supported by DOE Office of Science Advanced Scientific\n\
 Computing Research (ASCR) research and by NSF Directorate for Computer &\n\
 Information Science & Engineering (CISE) Division of Computing and\n\
 Communication Foundations (CCF) core program funding.  Mercury is part of the\n\
 Mochi project.\n\
 .\n\
 This package contains the shared libraries.' | \
  VERSION=$(cat "mercury/mercury.spec" | awk -v VERSION_SUFFIX=$(cat /tmp/build_ts.txt) '/^Version:/ {split($2,a,"~"); if (a[2]!="") {print a[1] "." VERSION_SUFFIX "~" a[2]} else {print a[1] "." VERSION_SUFFIX}; }') envsubst | \
  tee /debs/mercury/DEBIAN/control

RUN dpkg-deb -Zxz --build debs/mercury/
#daos
RUN cp -r daos/debian/ debs/daos-client/DEBIAN
RUN cp -r daos/install/lib64/* debs/daos-client/usr/lib64
RUN cp -r daos/install/include/ debs/daos-client/usr/include
RUN cp -r daos/install/bin/ debs/daos-client/usr/bin
RUN cp -r daos/install/etc/* debs/daos-client/etc/daos
RUN cp -r daos/install/share/ debs/daos-client/usr/share
#RUN cp -r doas/install/utils/systemd/* /debs/daos-client/usr/lib/systemd/system

RUN rm -rf debs/daos-client/DEBIAN/source/

RUN echo -e \
'Package: daos-client\n\
Section: net\n\
Maintainer: daos-stack <daos@daos.groups.io>\n\
Architecture: amd64\n\
Version: ${VERSION}\n\
Depends: openmpi-bin, fuse3, libfabric1 (>= 1.15.1-1), libatomic1, libprotobuf-c-dev, libisal2, libisal2-crypto, libmercury1\n\
Description: The Distributed Asynchronous Object Storage (DAOS) is an open-source\n\
 software-defined object store designed from the ground up for\n\
 massively distributed Non Volatile Memory (NVM). DAOS takes advantage\n\
 of next generation NVM technology like Storage Class Memory (SCM) and\n\
 NVM express (NVMe) while presenting a key-value storage interface and\n\
 providing features such as transactional non-blocking I/O, advanced\n\
 data protection with self healing on top of commodity hardware, end-\n\
 to-end data integrity, fine grained data control and elastic storage\n\
 to optimize performance and cost.\n\
 .\n\
 This package contains the DAOS client.' | \
  VERSION=$(cat "daos/utils/rpms/daos.spec" | awk -v VERSION_SUFFIX=$(cat /tmp/build_ts.txt) '/^Version:/ {split($2,a,"~"); if (a[2]!="") {print a[1] "." VERSION_SUFFIX "~" a[2]} else {print a[1] "." VERSION_SUFFIX}; }') envsubst | \
  tee /debs/daos-client/DEBIAN/control

#some copied files do not have the required permissions for dpkg-deb (must be >=0755 and <=0775)
RUN chmod -R 755 debs/daos-client && dpkg-deb -Zxz --build debs/daos-client/
# RUN find daos/ -name

RUN mkdir -p out && \
  cp -v -f debs/libfabric.deb out && \
  cp -v -f debs/isa-l.deb out && \
  cp -v -f debs/libisa-l_crypto.deb out && \
  cp -v -f debs/mercury.deb out && \
  cp -v -f debs/daos-client.deb out

RUN echo $GIT_REPOSITORY > /out/git_repo.txt && \
  echo $GIT_BRANCH_OR_TAG > /out/git_branch_or_tag.txt

FROM scratch

COPY --from=builder /out /out

