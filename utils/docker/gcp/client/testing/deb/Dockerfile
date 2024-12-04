ARG BASE_IMAGE
ARG TEST_IMAGE
FROM $BASE_IMAGE as packages
FROM $TEST_IMAGE

ARG IMAGE_TYPE
ARG DAOS_SRC_DIR

# Copy the previously cloned daos
COPY "$DAOS_SRC_DIR" /daos/

# Copy buildfiles to the image if it exists
# Have to include at least one file (LICENSE) that exists of the command will fail
COPY LICENSE /buildfiles/* /buildfiles/

# Set the open file limits
RUN echo "* soft nofile 131072" >> /etc/security/limits.conf && \
  echo "* hard nofile 131072" >> /etc/security/limits.conf && \
  cat /etc/security/limits.conf

RUN if [[ "$GHA" == "false" ]]; then \
  chmod -R 0777 /buildfiles && \
  echo "installing GRTE runtimes"; dpkg -i /buildfiles/packages/grtev5-runtimes.deb && \
  echo "installing test-agent"; dpkg -i /buildfiles/packages/test-agent_0.0.0-0_amd64.deb && \
  echo "installing perfutations"; dpkg -i /buildfiles/packages/perfutations_0.0.0-0_amd64.deb && \
  /bin/sh -c "/buildfiles/tools/install_gcsfuse.sh" && \
  /bin/sh -c "/buildfiles/tools/install_gcloud.sh"; \
fi

# Install DAOS Client with Performance testing packages
COPY --from=packages /out /packages

# Prevent interactive prompts
RUN echo "APT::Get::Assume-Yes \"true\";" > /etc/apt/apt.conf.d/no-prompt

# Install miscellaneous packages
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
  clustershell \
  curl \
  fio \
  fuse3 \
  gdb \
  git \
  htop \
  libbz2-dev \
  jq \
  openssh-server \
  patch \
  pdsh \
  psmisc \
  python3 \
  rsync \
  screen \
  sudo \
  wget \
  hostname

WORKDIR /

# Install all the packages needed for the build
RUN daos/utils/scripts/install-ubuntu.sh

# Install all DAOS packages made by the parent Docker image
RUN dpkg -i /packages/libfabric.deb \
 && dpkg -i /packages/isa-l.deb \
 && dpkg -i /packages/libisa-l_crypto.deb \
 && dpkg -i /packages/mercury.deb

RUN dpkg -i /packages/daos-client.deb

# Install Intel MPI
RUN /bin/sh -c "/daos/utils/docker/gcp/client/testing/deb/buildfiles/install_intel_oneapi.sh"

# Install IO500
RUN /bin/sh -c "/daos/utils/docker/gcp/client/testing/buildfiles/install_io500_sc22.sh"

WORKDIR /

# Install FIO
RUN /bin/sh -c "/daos/utils/docker/gcp/client/testing/buildfiles/install_fio.sh"
