#!/bin/bash

set -eux

systemctl start nfs-server.service
mkdir -p /export/share
chown "${REMOTE_ACCT:-jenkins}" /export/share
echo "/export/share ${NODELIST//,/(rw,no_root_squash) }(rw,no_root_squash)" > \
    /etc/exports
exportfs -ra
