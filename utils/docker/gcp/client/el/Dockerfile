ARG ROCKY_BUILDER
FROM $ROCKY_BUILDER as builder

ARG GIT_REPOSITORY
ARG GIT_BRANCH_OR_TAG
ARG DAOS_BUILD_TYPE=release
ARG ROCKY_BUILDER

RUN echo $ROCKY_BUILDER

RUN echo $(TZ=PDT date +'%Y%m%d.%H%M%S') > /tmp/build_ts.txt

# Copy the previously cloned daos
COPY daos /daos/

# Copy over a patch script to use scons3
COPY /buildfiles /buildfiles

RUN dnf install -y epel-release dnf-plugins-core

RUN if [[ "$ROCKY_BUILDER" == "rockylinux/rockylinux:9" ]]; then \
  dnf config-manager --enable crb && \
  dnf install -y python Lmod systemd; \
fi

RUN if [[ "$ROCKY_BUILDER" == "rockylinux/rockylinux:8" ]]; then \
  dnf config-manager --enable powertools; \
fi

RUN dnf config-manager --save --setopt=assumeyes=True && \
  dnf install -y \
    git \
    virtualenv \
    rpmdevtools \
    rpmrebuild \
    hdf5-devel \
    python3-distro \
    python3-scons \
    ucx-devel \
    fdupes \
    make \
    perl \
    libuuid-devel \
    numactl-devel \
    valgrind-devel \
    libnl3-devel \
    libpmemobj-devel \
    python3-sphinx \
    doxygen \
    meson \
    wget \
    libpcap-devel \
    python3-configshell \
    python3-pexpect \
    pandoc \
    libfabric-devel \
    python3-pyelftools

# We need some packages for build only because daos.spec does not allow us
# to only build the client. Rather than building them, just download them from
# upstream repo.
RUN EL_VERSION=`echo "$ROCKY_BUILDER" | sed 's/.*://'` && \
      wget -O /etc/yum.repos.d/daos-packages.repo \
              "https://packages.daos.io/v2.6/EL${EL_VERSION}/packages/x86_64/daos_packages.repo"

RUN rpm --import https://packages.daos.io/RPM-GPG-KEY
RUN dnf install -y \
    spdk-devel \
    argobots-devel \
    libpmemobj-devel

WORKDIR /

# Need to pass server argument because no way to build RPM without it
RUN daos/utils/docker/gcp/clone_repos.sh

RUN if [[ "$ROCKY_BUILDER" == "rockylinux/rockylinux:9" ]]; then \
  daos/utils/scripts/install-el9.sh; \
fi

RUN if [[ "$ROCKY_BUILDER" == "rockylinux/rockylinux:8" ]]; then \
  daos/utils/scripts/install-el8.sh; \
fi

# The script in the internal DAOS repo returns the git hash to use for each
# component we check out that matches the current version of DAOS being used.

WORKDIR /libfabric
RUN make -j && \
  awk -v VERSION_SUFFIX=$(cat /tmp/build_ts.txt) -i inplace '/Version:/ {split($0,a,"~"); if (a[2]!="") {print a[1] "." VERSION_SUFFIX "~" a[2]} else {print a[1] "." VERSION_SUFFIX}; next; } ; 1' libfabric.spec && \
  git --no-pager diff libfabric.spec && \
  find _topdir/RPMS -name "*.rpm" | tr "\n" " " | xargs rpm -vU
RUN find _topdir/RPMS -name "*.rpm" > /tmp/libfabric.txt && sed -i 's/\.rpm$//' /tmp/libfabric.txt
RUN readarray -t arr < /tmp/libfabric.txt ; for r in "${arr[@]}" ; do { OLD_RPM_NAME="$r"; NEW_RPM_NAME=$(echo $OLD_RPM_NAME | awk -v VERSION_SUFFIX=$(cat /tmp/build_ts.txt) '{split($0,a,"~"); if (a[2]!="") {print a[1] "." VERSION_SUFFIX "~" a[2]} else {print a[1] "." VERSION_SUFFIX}; }');  mv "$OLD_RPM_NAME.rpm" "$NEW_RPM_NAME.rpm";   } done

WORKDIR /mercury
RUN make -j && \
  awk -v VERSION_SUFFIX=$(cat /tmp/build_ts.txt) -i inplace '/Version:/ {split($0,a,"~"); if (a[2]!="") {print a[1] "." VERSION_SUFFIX "~" a[2]} else {print a[1] "." VERSION_SUFFIX}; next; } ; 1' mercury.spec && \
  git --no-pager diff mercury.spec && \
  find _topdir/RPMS -name "*.rpm" | tr "\n" " " | xargs rpm -vU
RUN find _topdir/RPMS -name "*.rpm" > /tmp/mercury.txt && sed -i 's/\.rpm$//' /tmp/mercury.txt
RUN readarray -t arr < /tmp/mercury.txt ; for r in "${arr[@]}" ; do { OLD_RPM_NAME="$r"; NEW_RPM_NAME=$(echo $OLD_RPM_NAME | awk -v VERSION_SUFFIX=$(cat /tmp/build_ts.txt) '{split($0,a,"~"); if (a[2]!="") {print a[1] "." VERSION_SUFFIX "~" a[2]} else {print a[1] "." VERSION_SUFFIX}; }');  mv "$OLD_RPM_NAME.rpm" "$NEW_RPM_NAME.rpm";   } done

WORKDIR /isa-l
RUN make -j && \
  awk -v VERSION_SUFFIX=$(cat /tmp/build_ts.txt) -i inplace '/Version:/ {split($0,a,"~"); if (a[2]!="") {print a[1] "." VERSION_SUFFIX "~" a[2]} else {print a[1] "." VERSION_SUFFIX}; next; } ; 1' isa-l.spec && \
  git --no-pager diff isa-l.spec && \
  find _topdir/RPMS -name "*.rpm" | tr "\n" " " | xargs rpm -vU
RUN find _topdir/RPMS -name "*.rpm" > /tmp/isa-l.txt && sed -i 's/\.rpm$//' /tmp/isa-l.txt
RUN readarray -t arr < /tmp/isa-l.txt ; for r in "${arr[@]}" ; do { OLD_RPM_NAME="$r"; NEW_RPM_NAME=$(echo $OLD_RPM_NAME | awk -v VERSION_SUFFIX=$(cat /tmp/build_ts.txt) '{split($0,a,"~"); if (a[2]!="") {print a[1] "." VERSION_SUFFIX "~" a[2]} else {print a[1] "." VERSION_SUFFIX}; }');  mv "$OLD_RPM_NAME.rpm" "$NEW_RPM_NAME.rpm";   } done

WORKDIR /isa-l_crypto
# Add license, show difference, build, install
RUN git restore isa-l_crypto.spec && \
  awk -i inplace  '/%files/ { print $0 ; print "%license LICENSE"; next } ; 1' isa-l_crypto.spec && \
  make -j && \
  awk -v VERSION_SUFFIX=$(cat /tmp/build_ts.txt) -i inplace '/Version:/ {split($0,a,"~"); if (a[2]!="") {print a[1] "." VERSION_SUFFIX "~" a[2]} else {print a[1] "." VERSION_SUFFIX}; next; } ; 1' isa-l_crypto.spec && \
  git --no-pager diff isa-l_crypto.spec && \
  find _topdir/RPMS -name "*.rpm" | tr "\n" " " | xargs rpm -vU
RUN find _topdir/RPMS -name "isa-l_crypto*.rpm" > /tmp/isa-l_crypto.txt && sed -i 's/\.rpm$//' /tmp/isa-l_crypto.txt
RUN readarray -t arr < /tmp/isa-l_crypto.txt ; for r in "${arr[@]}" ; do { OLD_RPM_NAME="$r"; NEW_RPM_NAME=$(echo $OLD_RPM_NAME | awk -v VERSION_SUFFIX=$(cat /tmp/build_ts.txt) '{split($0,a,"~"); if (a[2]!="") {print a[1] "." VERSION_SUFFIX "~" a[2]} else {print a[1] "." VERSION_SUFFIX}; }');  mv "$OLD_RPM_NAME.rpm" "$NEW_RPM_NAME.rpm";   } done

WORKDIR /raft
RUN make -j -f ./Makefile-rpm.mk && \
  awk -v VERSION_SUFFIX=$(cat /tmp/build_ts.txt) -i inplace '/Version:/ {split($0,a,"~"); if (a[2]!="") {print a[1] "." VERSION_SUFFIX "~" a[2]} else {print a[1] "." VERSION_SUFFIX}; next; } ; 1' raft.spec && \
  git --no-pager diff raft.spec && \
  find _topdir/RPMS -name "*.rpm" | tr "\n" " " | xargs rpm -vU
RUN find _topdir/RPMS -name "raft*.rpm" > /tmp/raft.txt && sed -i 's/\.rpm$//' /tmp/raft.txt
RUN readarray -t arr < /tmp/raft.txt ; for r in "${arr[@]}" ; do { OLD_RPM_NAME="$r"; NEW_RPM_NAME=$(echo $OLD_RPM_NAME | awk -v VERSION_SUFFIX=$(cat /tmp/build_ts.txt) '{split($0,a,"~"); if (a[2]!="") {print a[1] "." VERSION_SUFFIX "~" a[2]} else {print a[1] "." VERSION_SUFFIX}; }');  mv "$OLD_RPM_NAME.rpm" "$NEW_RPM_NAME.rpm";   } done

WORKDIR /daos
# Remove broken dependencies (daos-raft-devel, isa-l-devel, libpmemobj-devel), add license,
# show difference
RUN git restore utils/rpms/daos.spec && \
  sed -i '/Version:/ s/$/.'$(cat /tmp/build_ts.txt)'/' utils/rpms/daos.spec && \
  awk -i inplace '!/BuildRequires: daos-raft-devel/' utils/rpms/daos.spec && \
  awk -i inplace '!/BuildRequires: libpmemobj-devel/' utils/rpms/daos.spec && \
  awk -i inplace '!/BuildRequires: isa-l-devel/' utils/rpms/daos.spec && \
  sed -i 's/USE_INSTALLED/GOOGLE_INTERNAL_UTILS=0 \\\n      USE_INSTALLED/' utils/rpms/daos.spec && \
  awk -i inplace '!/Requires: fuse3-devel/' utils/rpms/daos.spec && \
  awk -i inplace '!/Requires: ior/' utils/rpms/daos.spec && \
  sed -i 's/STATIC_FUSE=0//' utils/rpms/daos.spec && \
  awk -i inplace  '/%files/ { print $0 ; print "%license LICENSE"; print "%license third_party_programs.txt"; next } ; 1' utils/rpms/daos.spec && \
  git --no-pager diff utils/rpms/daos.spec

RUN if [[ "$ROCKY_BUILDER" == "rockylinux/rockylinux:9" ]]; then \
  /bin/sh -c "/buildfiles/scripts/patch_daos_spec.sh"; \
fi

RUN virtualenv venv && \
  source venv/bin/activate && \
  pip install --upgrade pip && \
  { [[ -f requirements-build.txt ]] && pip install -r requirements-build.txt || pip install -r requirements.txt; } && \
  echo "Building fuse" && \
  scons --jobs="$(nproc --all)" --build-deps=only \
        USE_INSTALLED=spdk,pmdk,ucx,mercury,ofi,argobots,isal,isal_crypto && \
  cp install/prereq/release/fuse/lib64/libfuse3.a /usr/lib64 && \
  cp -r install/prereq/release/fuse/include/* /usr/include && \
  echo "Building daos RPM" && \
  scons --jobs="$(nproc --all)" --build-deps=yes rpms BUILD_TYPE="$DAOS_BUILD_TYPE" \
        TARGET_TYPE="release"

WORKDIR /

# Copy the client dependencies RPMs, excluding mercury-ucx which is not needed for clients
RUN mkdir -p out && find \
  libfabric/_topdir \
  mercury/_topdir  \
  isa-l/_topdir \
  isa-l_crypto/_topdir \
  argobots/_topdir \
  -name "*.rpm" -not -name "*tests*" -not -name "mercury-ucx*" | \
    xargs -IFILE cp -v -f "FILE" out

# Copy daos, daos-client RPMs
RUN mkdir -p out && \
  find daos/utils/rpms/_topdir/RPMS/ -name "*daos-[0-9]*.rpm" | xargs -IFILE cp -v -f "FILE" out && \
  find daos/utils/rpms/_topdir/RPMS/ -name "*daos-debugsource-[0-9]*.rpm" | xargs -IFILE cp -v -f "FILE" out && \
  find daos/utils/rpms/_topdir/RPMS/ -name "*daos-debuginfo-[0-9]*.rpm" | xargs -IFILE cp -v -f "FILE" out && \
  find daos/utils/rpms/_topdir/RPMS/ -name "*daos-devel-[0-9]*.rpm" | xargs -IFILE cp -v -f "FILE" out && \
  find daos/utils/rpms/_topdir/RPMS/ -name "*daos-client-[0-9]*.rpm" | xargs -IFILE cp -v -f "FILE" out && \
  find daos/utils/rpms/_topdir/RPMS/ -name "*daos-client-debuginfo-[0-9]*.rpm" | xargs -IFILE cp -v -f "FILE" out && \
  find daos/utils/rpms/_topdir/RPMS/ -name "*daos-admin-[0-9]*.rpm" | xargs -IFILE cp -v -f "FILE" out && \
  find daos/utils/rpms/_topdir/RPMS/ -name "*daos-client-tests-[0-9]*.rpm" | xargs -IFILE cp -v -f "FILE" out

RUN echo $GIT_REPOSITORY > /out/git_repo.txt && \
    echo $GIT_BRANCH_OR_TAG > /out/git_branch_or_tag.txt

FROM scratch

COPY --from=builder /out /out
