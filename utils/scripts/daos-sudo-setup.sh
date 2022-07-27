#!/bin/bash -uex

: "${UID=1000}"

useradd --no-log-init --uid $UID --user-group --create-home --shell /bin/bash \
            --home /home/daos daos_server
echo "daos_server:daos_server" | chpasswd

echo "daos_server ALL=(root) NOPASSWD: ALL" >> /etc/sudoers.d/daos_sudo_setup
chmod 0440 /etc/sudoers.d/daos_sudo_setup
visudo -c
sudo -l -U daos_server
