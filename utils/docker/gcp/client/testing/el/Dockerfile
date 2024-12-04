ARG BASE_IMAGE
ARG TEST_IMAGE
FROM $BASE_IMAGE as packages
FROM $TEST_IMAGE

ARG IMAGE_TYPE
ARG IMAGE_VERSION
ARG GHA
ARG DAOS_SRC_DIR
RUN echo "IMAGE_TYPE: $IMAGE_TYPE"
RUN echo "IMAGE_VERSION: $IMAGE_VERSION"

# Set the open file limits
RUN echo "* soft nofile 131072" >> /etc/security/limits.conf && \
  echo "* hard nofile 131072" >> /etc/security/limits.conf && \
  cat /etc/security/limits.conf

# Copy buildfiles to the image if it exists
# Have to include at least one file (LICENSE) that exists of the command will fail
COPY LICENSE /buildfiles/* /buildfiles/

RUN if [[ "$GHA" == "false" ]]; then \
  chmod -R 0777 /buildfiles && \
  echo "installing test-agent rpm"; rpm -i /buildfiles/packages/test-agent-rpm && \
  /bin/sh -c "/buildfiles/install_gcsfuse.sh" && \
  /bin/sh -c "/buildfiles/install_gcloud.sh"; \
fi

RUN dnf install -y epel-release dnf-plugins-core

# Install extras
RUN dnf config-manager --save --setopt=assumeyes=True && \
  dnf group install "Development Tools" && \
  dnf --enablerepo='*debug*' install \
    clustershell \
    fio \
    fio-debuginfo \
    fio-debugsource \
    gdb \
    git \
    hostname \
    htop \
    jq \
    libunwind-devel \
    openssh-server \
    patch \
    pdsh \
    perf \
    psmisc \
    rsync \
    screen \
    sudo \
    wget

RUN if [[ "$IMAGE_VERSION" == "rocky9" ]]; then \
  dnf config-manager --enable crb && \
  dnf install -y libatomic protobuf-devel libuuid-devel capstone-devel; \
fi

RUN if [[ "$IMAGE_VERSION" == "rocky8" ]]; then \
  dnf config-manager --enable powertools && \
  dnf install -y curl; \
fi

COPY --from=packages /out /packages

WORKDIR /

# Copy the previously cloned daos
COPY "$DAOS_SRC_DIR" /daos/

RUN if [[ "$IMAGE_VERSION" == "rocky9" ]]; then \
  /bin/sh -c /daos/utils/scripts/install-el9.sh; \
fi

RUN if [[ "$IMAGE_VERSION" == "rocky8" ]]; then \
  /bin/sh -c /daos/utils/scripts/install-el8.sh; \
fi

WORKDIR /daos
RUN if [[ "$IMAGE_TYPE" == "ftests" ]]; then \
  echo "Installing RPMs from base image" && \
  find /packages -name "*.rpm" -not -name "*src*" -not -name "*mercury-devel*" -not -name "*daos-admin*" -not -name "*tests*"  -not -name "*argobots*" | tr "\n" " " | xargs dnf install && \
  echo "Installing packages for ftests" && \
  dnf --enablerepo='*debug*' install \
    attr \
    capstone-devel \
    dbench \
    fuse3-devel \
    go \
    lbzip2 \
    libcmocka \
    Lmod \
    mpich-devel \
    openmpi-devel \
    protobuf-c-devel \
    python3-avocado{,-plugins-{output-html,varianter-yaml-to-mux}} \
    python3-defusedxml \
    python3-devel \
    python3-distro \
    python3-tabulate \
    sshfs && \
  find /packages -name "*.rpm" -name "*daos-admin*" | tr "\n" " " | xargs dnf install && \
  find /packages -name "*.rpm" -name "*tests*" | tr "\n" " " | xargs dnf install && \
  find /packages -name "*.rpm" -name "*argobots*" | tr "\n" " " | xargs dnf install && \
  export PATH="/usr/lib64/mpich/bin:${PATH}" && echo 'export PATH=/usr/lib64/mpich/bin:${PATH}' >> ~/.bashrc && \
  python3 -m pip install --upgrade pip && \
  { [[ -f requirements-build.txt ]] && python3 -m pip install -r requirements-build.txt || python3 -m pip install -r requirements.txt; } && \
  python3 -m pip install defusedxml \
    distro \
    jira \
    junit_xml \
    meson \
    ninja \
    pyelftools \
    pyxattr \
    pyyaml \
    scons    \
    tabulate \
    wheel \
    ruamel.yaml \
    parse && \
  /usr/bin/python3 /daos/utils/docker/gcp/client/testing/buildfiles/ftests/modify_yamls.py && \
  cp /daos/utils/systemd/* /etc/systemd/system && \
  echo 'root:root' | chpasswd && \
  printf '#!/bin/sh\nexit 0' > /usr/sbin/policy-rc.d && \
  dnf install systemd systemd-sysv dbus && \
  systemctl enable daos_agent && \
  mkdir -p /var/run/daos_agent && chown daos_agent.daos_agent /var/run/daos_agent && \
  /bin/sh -c "/daos/utils/docker/gcp/client/testing/buildfiles/install_ior.sh" && \
  cp /daos/utils/docker/gcp/client/testing/buildfiles/ftests/*.yaml /usr/lib/daos/TESTING/ftest; \
fi

WORKDIR /

RUN if [[ "$IMAGE_TYPE" == "perf" ]]; then \
  echo "Installing RPMs from base image" && \
  find /packages -name "*.rpm" -not -name "*src*" -not -name "*mercury-devel*" -not -name "*daos-admin*" -not -name "*tests*"  -not -name "*argobots*" | tr "\n" " " | xargs dnf install && \
  echo "Installing packages for perf" && \
  /bin/sh -c "/daos/utils/docker/gcp/client/testing/el/buildfiles/install_intel_oneapi.sh" && \
  /bin/sh -c "/daos/utils/docker/gcp/client/testing/buildfiles/install_fio.sh" && \
  /bin/sh -c "/daos/utils/docker/gcp/client/testing/buildfiles/install_io500_sc22.sh" && \
  if [[ "$GHA" == "false" ]]; then /bin/sh -c "rpm -i /buildfiles/packages/perfutations-rpm"; fi; \
fi

RUN if [[ "$IMAGE_TYPE" == "unit_tests" ]]; then \
  /bin/sh -c "cd /daos && source utils/ci/gcp/helper.sh && create_test_user && build_daos"; \
fi
