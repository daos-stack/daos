#!/bin/bash
# shellcheck disable=SC1113
# /*
#  * (C) Copyright 2016-2023 Intel Corporation.
#  *
#  * SPDX-License-Identifier: BSD-2-Clause-Patent
# */

set -eux
# allow core files to be generated
sudo bash -c "set -ex
if ! $TEST_RPMS; then
cat <<EOF > /etc/sysctl.d/10-daos-server.conf
$(cat utils/rpms/10-daos_server.conf)
EOF
fi
# disable Leap15.3 (at least) from restricting dmesg to root
cat <<EOF > /etc/sysctl.d/10-dmesg-for-all.conf
kernel.dmesg_restrict=0
EOF
# For verbs enable servers in dual-nic setups to talk to each other; no adverse effect for tcp
cat <<EOF > /etc/sysctl.d/10-daos-verbs.conf
net.ipv4.conf.all.accept_local=1
net.ipv4.conf.all.arp_ignore=2
net.ipv4.conf.all.rp_filter=2
EOF
for x in \$(cd /sys/class/net/ && ls -d ib*); do
    echo \"net.ipv4.conf.\$x.rp_filter=2\"
done >> /etc/sysctl.d/10-daos-verbs.conf
sysctl --system
if [ \"\$(ulimit -c)\" != \"unlimited\" ]; then
    echo \"*  soft  core  unlimited\" >> /etc/security/limits.d/80_daos_limits.conf
fi
if [ \"\$(ulimit -l)\" != \"unlimited\" ]; then
    echo \"*  soft  memlock  unlimited\" >> /etc/security/limits.d/80_daos_limits.conf
    echo \"*  hard  memlock  unlimited\" >> /etc/security/limits.d/80_daos_limits.conf
fi
if [ \"\$(ulimit -n)\" != \"1048576\" ]; then
    echo \"*  soft  nofile 1048576\" >> /etc/security/limits.d/80_daos_limits.conf
    echo \"*  hard  nofile 1048576\" >> /etc/security/limits.d/80_daos_limits.conf
fi
cat /etc/security/limits.d/80_daos_limits.conf
ulimit -a
echo \"/var/tmp/core.%e.%t.%p\" > /proc/sys/kernel/core_pattern"
sudo rm -f /var/tmp/core.*
if [ "${HOSTNAME%%.*}" != "$FIRST_NODE" ]; then
    if grep /mnt/daos\  /proc/mounts; then
        sudo umount /mnt/daos
    else
        if [ ! -d /mnt/daos ]; then
            sudo mkdir -p /mnt/daos
        fi
    fi

    tmpfs_size=16777216
    memsize="$(sed -ne '/MemTotal:/s/.* \([0-9][0-9]*\) kB/\1/p' \
               /proc/meminfo)"
    if [ "$memsize" -gt "32000000" ]; then
        # make it twice as big on the hardware cluster
        tmpfs_size=$((tmpfs_size*2))
    fi
    sudo ed <<EOF /etc/fstab
\$a
tmpfs /mnt/daos tmpfs rw,relatime,size=${tmpfs_size}k 0 0 # added by ftest.sh
.
wq
EOF
    sudo mount /mnt/daos
fi

# make sure to set up for daos_agent. The test harness will take care of
# creating the /var/run/daos_{agent,server} directories when needed.
sudo bash -c "set -ex
if [ -d  /var/run/daos_agent ]; then
    rm -rf /var/run/daos_agent
fi
if [ -d  /var/run/daos_server ]; then
    rm -rf /var/run/daos_server
fi
if $TEST_RPMS || [ -f $DAOS_BASE/SConstruct ]; then
    echo \"No need to NFS mount $DAOS_BASE\"
else
    mkdir -p $DAOS_BASE
    ed <<EOF /etc/fstab
\\\$a
$NFS_SERVER:$PWD $DAOS_BASE nfs defaults,vers=3 0 0 # DAOS_BASE # added by ftest.sh
.
wq
EOF
    mount \"$DAOS_BASE\"
fi"

if ! $TEST_RPMS; then
    # set up symlinks to spdk scripts (none of this would be
    # necessary if we were testing from RPMs) in order to
    # perform NVMe operations via daos_server_helper
    sudo mkdir -p /usr/share/daos/control
    sudo ln -sf "$SL_PREFIX"/share/daos/control/setup_spdk.sh \
               /usr/share/daos/control
    sudo mkdir -p /usr/share/spdk/scripts
    if [ ! -f /usr/share/spdk/scripts/setup.sh ]; then
        sudo ln -sf "$SL_PREFIX"/share/spdk/scripts/setup.sh \
                   /usr/share/spdk/scripts
    fi
    if [ ! -f /usr/share/spdk/scripts/common.sh ]; then
        sudo ln -sf "$SL_PREFIX"/share/spdk/scripts/common.sh \
                   /usr/share/spdk/scripts
    fi
    if [ ! -f /usr/share/spdk/include/spdk/pci_ids.h ]; then
        sudo rm -f /usr/share/spdk/include
        sudo ln -s "$SL_PREFIX"/include \
                   /usr/share/spdk/include
    fi

    # first, strip the execute bit from the in-tree binary,
    # then copy daos_server_helper binary into \$PATH and fix perms
    chmod -x "$DAOS_BASE"/install/bin/daos_server_helper && \
    sudo cp "$DAOS_BASE"/install/bin/daos_server_helper /usr/bin/daos_server_helper && \
	    sudo chown root /usr/bin/daos_server_helper && \
	    sudo chmod 4755 /usr/bin/daos_server_helper
fi

rm -rf "${TEST_TAG_DIR:?}/"
mkdir -p "$TEST_TAG_DIR/"
if [ -z "$JENKINS_URL" ]; then
    exit 0
fi
