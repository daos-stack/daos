#!/bin/bash

set -eux

dnf -y install vagrant-libvirt libvirt-client
systemctl start libvirtd.socket
mkdir -p "$WORKDIR"
chown jenkins.jenkins "$WORKDIR"
echo "$NFS_SERVER:$WORKDIR $WORKDIR nfs defaults,vers=3 0 0" >> /etc/fstab
if ! mount "$WORKDIR"; then
    exit 99
fi
