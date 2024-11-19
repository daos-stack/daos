ARG BASE_IMAGE
ARG TEST_IMAGE
FROM $BASE_IMAGE as packages
FROM $TEST_IMAGE

ARG IMAGE_TYPE

# Set the open file limits
RUN echo "* soft nofile 131072" >> /etc/security/limits.conf && \
  echo "* hard nofile 131072" >> /etc/security/limits.conf && \
  cat /etc/security/limits.conf

# Copy the buildfiles directory to the Docker image
COPY /buildfiles /buildfiles
RUN chmod -R 0777 /buildfiles

RUN echo "installing GRTE runtimes"; dpkg -i /buildfiles/packages/grtev5-runtimes.deb
RUN echo "installing test-agent"; dpkg -i /buildfiles/packages/test-agent_0.0.0-0_amd64.deb
RUN echo "installing perfutations"; dpkg -i /buildfiles/packages/perfutations_0.0.0-0_amd64.deb

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
  gawk \
  gdb \
  git \
  htop \
  libbz2-dev \
  lsb-release \
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

# Clone DAOS
WORKDIR /
RUN git clone https://gerrit.googlesource.com/gcompute-tools
RUN python3 --version
RUN python3 /gcompute-tools/git-cookie-authdaemon
RUN DAOS_REPOSITORY="$(cat /packages/git_repo.txt)" && DAOS_BRANCH_OR_TAG="$(cat /packages/git_branch_or_tag.txt)" && \
  echo "DAOS_REPOSITORY: $DAOS_REPOSITORY" && echo "DAOS_BRANCH_OR_TAG: $DAOS_BRANCH_OR_TAG" && \
  git clone --recurse-submodules "$DAOS_REPOSITORY" --branch "$DAOS_BRANCH_OR_TAG"

# Remove unnecessary Java dependencies.
RUN gawk -i inplace '!/openjdk/' daos/utils/scripts/install-ubuntu.sh

# Install all the packages needed for the build
RUN daos/utils/scripts/install-ubuntu.sh

# Install all DAOS packages made by the parent Docker image
RUN dpkg -i /packages/libfabric.deb \
 && dpkg -i /packages/isa-l.deb \
 && dpkg -i /packages/libisa-l_crypto.deb \
 && dpkg -i /packages/mercury.deb

RUN dpkg -i /packages/daos-client.deb

# Install  gcsfuse and gcloud
RUN /bin/sh -c "/buildfiles/tools/debian/install_gcsfuse.sh"
RUN /bin/sh -c "/buildfiles/tools/debian/install_gcloud.sh"

# Install Intel MPI
RUN /bin/sh -c "/buildfiles/tools/debian/install_intel_oneapi.sh"

# Install IO500
RUN /bin/sh -c "/buildfiles/tools/debian/install_io500_sc22.sh"

WORKDIR /

# Install FIO
RUN /bin/sh -c "/buildfiles/tools/install_fio.sh"
