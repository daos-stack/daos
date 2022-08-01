#!/bin/bash -uex

# This script is used mainly by dockerfiles to setup a daos_server user
# that matches the UID used for the Docker image.
# If the sudo package is installed that user will be given sudo access
# for testing.
# Docker containers use this user for some testing and for malware scanning.

: "${UID=1000}"

useradd --no-log-init --uid $UID --user-group --create-home --shell /bin/bash \
            --home /home/daos daos_server
echo "daos_server:daos_server" | chpasswd

if command -v sudo; then
  echo "daos_server ALL=(root) NOPASSWD: ALL" >> /etc/sudoers.d/daos_sudo_setup
  chmod 0440 /etc/sudoers.d/daos_sudo_setup
  visudo -c
  sudo -l -U daos_server
fi
