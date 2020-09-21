#!/bin/bash
# Copyright (C) Copyright 2019-2020 Intel Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted for any purpose (including commercial purposes)
# provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions, and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions, and the following disclaimer in the
#    documentation and/or materials provided with the distribution.
#
# 3. In addition, redistributions of modified forms of the source or binary
#    code must carry prominent notices stating that the original code was
#    changed and the date of the change.
#
#  4. All publications or advertising materials mentioning features or use of
#     this software are asked, but not required, to acknowledge that it was
#     developed by Intel Corporation and credit the contributors.
#
# 5. Neither the name of Intel Corporation, nor the name of any Contributor
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

set -ex -o pipefail

# shellcheck disable=SC1091
if [ -f .localenv ]; then
    # read (i.e. environment, etc.) overrides
    . .localenv
fi

TEST_TAG_ARG="${1:-quick}"
mapfile -t TEST_TAG_ARR <<< "$TEST_TAG_ARG"

TEST_TAG_DIR="/tmp/Functional_${TEST_TAG_ARG// /_}"

NFS_SERVER=${NFS_SERVER:-${HOSTNAME%%.*}}

trap 'echo "encountered an unchecked return code, exiting with error"' ERR

IFS=" " read -r -a nodes <<< "${2//,/ }"
TEST_NODES=$(IFS=","; echo "${nodes[*]:1:8}")

# Optional --nvme argument for launch.py
NVME_ARG=""
if [ -n "${3}" ]; then
    NVME_ARG="-n ${3}"
fi

# For nodes that are only rebooted between CI nodes left over mounts
# need to be cleaned up.
pre_clean () {
    i=5
    while [ $i -gt 0 ]; do
        if clush "${CLUSH_ARGS[@]}" -B -l "${REMOTE_ACCT:-jenkins}" -R ssh \
              -S -w "$(IFS=','; echo "${nodes[*]}")" "set -x -e
              mapfile -t ftest_mounts < <(grep 'added by ftest.sh' /etc/fstab)
              for n_mnt in \"\${ftest_mounts[@]}\"; do
                  mpnt=(\${n_mnt})
                  sudo umount \${mpnt[1]}
              done
              sudo sed -i -e \"/added by ftest.sh/d\" /etc/fstab"; then
            break
        fi
        ((i-=1)) || true
    done
}

cleanup() {
    i=5
    while [ $i -gt 0 ]; do
        if clush "${CLUSH_ARGS[@]}" -B -l "${REMOTE_ACCT:-jenkins}" -R ssh \
             -S -w "$(IFS=','; echo "${nodes[*]}")" "set -x
            if grep /mnt/daos /proc/mounts; then
                if ! sudo umount /mnt/daos; then
                    echo \"During shutdown, failed to unmount /mnt/daos.  \"\
                         \"Continuing...\"
                fi
            fi
            x=0
            if grep \"# DAOS_BASE # added by ftest.sh\" /etc/fstab; then
                nfs_mount=true
            else
                nfs_mount=false
            fi
            sudo sed -i -e \"/added by ftest.sh/d\" /etc/fstab
            if [ -n \"$DAOS_BASE\" ] && \$nfs_mount; then
                while [ \$x -lt 30 ] &&
                      grep $DAOS_BASE /proc/mounts &&
                      ! sudo umount $DAOS_BASE; do
                    sleep 1
                    let x+=1
                done
                if grep $DAOS_BASE /proc/mounts; then
                    echo \"Failed to unmount $DAOS_BASE\"
                    exit 1
                fi
                if [ -d $DAOS_BASE ] && ! sudo rmdir $DAOS_BASE; then
                    echo \"Failed to remove $DAOS_BASE\"
                    if [ -d $DAOS_BASE ]; then
                        ls -l $DAOS_BASE
                    else
                        echo \"because it does not exist\"
                    fi
                    exit 1
                fi
            fi"; then
            break
        fi
        ((i-=1)) || true
    done
}

pre_clean

# shellcheck disable=SC1091
if ${TEST_RPMS:-false}; then
    PREFIX=/usr
    SL_PREFIX=$PWD
else
    TEST_RPMS=false
    PREFIX=install
    . .build_vars.sh
fi

if ${TEARDOWN_ONLY:-false}; then
    cleanup
    exit 0
fi

trap 'set +e; cleanup' EXIT

# doesn't work: mapfile -t CLUSH_ARGS <<< "$CLUSH_ARGS"
# shellcheck disable=SC2206
CLUSH_ARGS=($CLUSH_ARGS)

DAOS_BASE=${SL_PREFIX%/install}
if ! clush "${CLUSH_ARGS[@]}" -B -l "${REMOTE_ACCT:-jenkins}" -R ssh -S \
    -w "$(IFS=','; echo "${nodes[*]}")" "set -ex
# allow core files to be generated
sudo bash -c \"set -ex
if [ \\\"\\\$(ulimit -c)\\\" != \\\"unlimited\\\" ]; then
    echo \\\"*  soft  core  unlimited\\\" >> /etc/security/limits.conf
fi
echo \\\"/var/tmp/core.%e.%t.%p\\\" > /proc/sys/kernel/core_pattern\"
rm -f /var/tmp/core.*
if [ \"\${HOSTNAME%%.*}\" != \"${nodes[0]}\" ]; then
    if grep /mnt/daos\\  /proc/mounts; then
        sudo umount /mnt/daos
    else
        if [ ! -d /mnt/daos ]; then
            sudo mkdir -p /mnt/daos
        fi
    fi

    tmpfs_size=16777216
    memsize=\"\$(sed -ne '/MemTotal:/s/.* \([0-9][0-9]*\) kB/\1/p' \
               /proc/meminfo)\"
    if [ \$memsize -gt 32000000 ]; then
        # make it twice as big on the hardware cluster
        tmpfs_size=\$((tmpfs_size*2))
    fi
    sudo ed <<EOF /etc/fstab
\\\$a
tmpfs /mnt/daos tmpfs rw,relatime,size=\${tmpfs_size}k 0 0 # added by ftest.sh
.
wq
EOF
    sudo mount /mnt/daos
fi

# make sure to set up for daos_agent
current_username=\$(whoami)
sudo bash -c \"set -ex
if [ -d  /var/run/daos_agent ]; then
    rm -rf /var/run/daos_agent
fi
if [ -d  /var/run/daos_server ]; then
    rm -rf /var/run/daos_server
fi
mkdir /var/run/daos_{agent,server}
chown \$current_username -R /var/run/daos_{agent,server}
chmod 0755 /var/run/daos_{agent,server}
if $TEST_RPMS || [ -f $DAOS_BASE/SConstruct ]; then
    echo \\\"No need to NFS mount $DAOS_BASE\\\"
else
    mkdir -p $DAOS_BASE
    ed <<EOF /etc/fstab
\\\\\\\$a
$NFS_SERVER:$PWD $DAOS_BASE nfs defaults,vers=3 0 0 # DAOS_BASE # added by ftest.sh
.
wq
EOF
    mount \\\"$DAOS_BASE\\\"
fi\"

if ! $TEST_RPMS; then
    # set up symlinks to spdk scripts (none of this would be
    # necessary if we were testing from RPMs) in order to
    # perform NVMe operations via daos_admin
    sudo mkdir -p /usr/share/daos/control
    sudo ln -sf $SL_PREFIX/share/daos/control/setup_spdk.sh \
               /usr/share/daos/control
    sudo mkdir -p /usr/share/spdk/scripts
    if [ ! -f /usr/share/spdk/scripts/setup.sh ]; then
        sudo ln -sf $SL_PREFIX/share/spdk/scripts/setup.sh \
                   /usr/share/spdk/scripts
    fi
    if [ ! -f /usr/share/spdk/scripts/common.sh ]; then
        sudo ln -sf $SL_PREFIX/share/spdk/scripts/common.sh \
                   /usr/share/spdk/scripts
    fi
    if [ ! -f /usr/share/spdk/include/spdk/pci_ids.h ]; then
        sudo rm -f /usr/share/spdk/include
        sudo ln -s $SL_PREFIX/include \
                   /usr/share/spdk/include
    fi

    # first, strip the execute bit from the in-tree binary,
    # then copy daos_admin binary into \$PATH and fix perms
    chmod -x $DAOS_BASE/install/bin/daos_admin && \
    sudo cp $DAOS_BASE/install/bin/daos_admin /usr/bin/daos_admin && \
	    sudo chown root /usr/bin/daos_admin && \
	    sudo chmod 4755 /usr/bin/daos_admin
fi

rm -rf \"${TEST_TAG_DIR:?}/\"
mkdir -p \"$TEST_TAG_DIR/\"
if [ -z \"\$JENKINS_URL\" ]; then
    exit 0
fi"; then
    echo "Cluster setup (i.e. provisioning) failed"
    exit 1
fi

args="${1:-quick}"
shift || true
args+=" $*"

# shellcheck disable=SC2029
# shellcheck disable=SC2086
if ! ssh -A $SSH_KEY_ARGS ${REMOTE_ACCT:-jenkins}@"${nodes[0]}" "set -ex
if $TEST_RPMS; then
    rm -rf $PWD/install/tmp
    mkdir -p $PWD/install/tmp
    # set the shared dir
    # TODO: remove the need for a shared dir by copying needed files to
    #       the test nodes
    export DAOS_TEST_SHARED_DIR=${DAOS_TEST_SHARED_DIR:-$PWD/install/tmp}
    logs_prefix=\"/var/tmp\"
else
    rm -rf $DAOS_BASE/install/tmp
    mkdir -p $DAOS_BASE/install/tmp
    logs_prefix=\"$DAOS_BASE/install/lib/daos/TESTING\"
    cd $DAOS_BASE
fi

export CRT_PHY_ADDR_STR=ofi+sockets

# Disable OFI_INTERFACE to allow launch.py to pick the fastest interface
unset OFI_INTERFACE

# At Oct2018 Longmond F2F it was decided that per-server logs are preferred
# But now we need to collect them!  Avoid using 'client_daos.log' due to
# conflicts with the daos_test log renaming.
export D_LOG_FILE=\"$TEST_TAG_DIR/daos.log\"

mkdir -p ~/.config/avocado/
cat <<EOF > ~/.config/avocado/avocado.conf
[datadir.paths]
logs_dir = \$logs_prefix/ftest/avocado/job-results

[sysinfo.collectibles]
files = \$HOME/.config/avocado/sysinfo/files
# File with list of commands that will be executed and have their output
# collected
commands = \$HOME/.config/avocado/sysinfo/commands
EOF

mkdir -p ~/.config/avocado/sysinfo/
cat <<EOF > ~/.config/avocado/sysinfo/commands
ps axf
dmesg
df -h
EOF

cat <<EOF > ~/.config/avocado/sysinfo/files
/proc/mounts
EOF

# apply patch for https://github.com/avocado-framework/avocado/pull/3076/
if ! grep TIMEOUT_TEARDOWN \
    /usr/lib/python2.7/site-packages/avocado/core/runner.py; then
    sudo patch -p0 -d/ << \"EOF\"
From d9e5210cd6112b59f7caff98883a9748495c07dd Mon Sep 17 00:00:00 2001
From: Cleber Rosa <crosa@redhat.com>
Date: Wed, 20 Mar 2019 12:46:57 -0400
Subject: [PATCH] [RFC] Runner: add extra timeout for tests in teardown

The current time given to tests performing teardown is pretty limited.
Let's add a 60 seconds fixed timeout just for validating the idea, and
once settled, we can turn that into a configuration setting.

Signed-off-by: Cleber Rosa <crosa@redhat.com>
---
 avocado/core/runner.py             | 11 +++++++++--
 examples/tests/longteardown.py     | 29 +++++++++++++++++++++++++++++
 selftests/functional/test_basic.py | 18 ++++++++++++++++++
 3 files changed, 56 insertions(+), 2 deletions(-)
 create mode 100644 examples/tests/longteardown.py

diff --git /usr/lib/python2.7/site-packages/avocado/core/runner.py.old /usr/lib/python2.7/site-packages/avocado/core/runner.py
index 1fc84844b..17e6215d0 100644
--- /usr/lib/python2.7/site-packages/avocado/core/runner.py.old
+++ /usr/lib/python2.7/site-packages/avocado/core/runner.py
@@ -45,6 +45,8 @@
 TIMEOUT_PROCESS_DIED = 10
 #: when test reported status but the process did not finish
 TIMEOUT_PROCESS_ALIVE = 60
+#: extra timeout to give to a test in TEARDOWN phase
+TIMEOUT_TEARDOWN = 60
 
 
 def add_runner_failure(test_state, new_status, message):
@@ -219,7 +221,7 @@ def finish(self, proc, started, step, deadline, result_dispatcher):
         wait.wait_for(lambda: not proc.is_alive() or self.status, 1, 0, step)
         if self.status:     # status exists, wait for process to finish
             deadline = min(deadline, time.time() + TIMEOUT_PROCESS_ALIVE)
-            while time.time() < deadline:
+            while time.time() < deadline + TIMEOUT_TEARDOWN:
                 result_dispatcher.map_method('test_progress', False)
                 if wait.wait_for(lambda: not proc.is_alive(), 1, 0, step):
                     return self._add_status_failures(self.status)
@@ -422,7 +424,12 @@ def sigtstp_handler(signum, frame):     # pylint: disable=W0613
 
         while True:
             try:
-                if time.time() >= deadline:
+                now = time.time()
+                if test_status.status.get('phase') == 'TEARDOWN':
+                    reached = now >= deadline + TIMEOUT_TEARDOWN
+                else:
+                    reached = now >= deadline
+                if reached:
                     abort_reason = \"Timeout reached\"
                     try:
                         os.kill(proc.pid, signal.SIGTERM)
EOF
fi
# apply fix for https://github.com/avocado-framework/avocado/issues/2908
sudo ed <<EOF /usr/lib/python2.7/site-packages/avocado/core/runner.py
/TIMEOUT_TEST_INTERRUPTED/s/[0-9]*$/60/
wq
EOF
# apply fix for https://github.com/avocado-framework/avocado/pull/2922
if grep \"testsuite.setAttribute('name', 'avocado')\" \
    /usr/lib/python2.7/site-packages/avocado/plugins/xunit.py; then
    sudo ed <<EOF /usr/lib/python2.7/site-packages/avocado/plugins/xunit.py
/testsuite.setAttribute('name', 'avocado')/s/'avocado'/os.path.basename(os.path.dirname(result.logfile))/
wq
EOF
fi

pushd $PREFIX/lib/daos/TESTING/ftest

# make sure no lingering corefiles or junit files exist
rm -f core.* *_results.xml

# see if we just wanted to set up
if ${SETUP_ONLY:-false}; then
    exit 0
fi

# check if slurm needs to be configured for soak
if [[ \"${TEST_TAG_ARG}\" =~ soak ]]; then
    if ! ./slurm_setup.py -c ${nodes[0]} -n ${TEST_NODES} -s -i; then
        exit \${PIPESTATUS[0]}
    else
        rc=0
    fi
fi


# can only process cores on EL7 currently
if [ $(lsb_release -s -i) = CentOS ]; then
    process_cores=\"p\"
else
    process_cores=\"\"
fi
# now run it!
# DAOS-5622: Temporarily disable certs in CI
if ! ./launch.py -cris\${process_cores}a -ts ${TEST_NODES} ${NVME_ARG} -ins \\
                 ${TEST_TAG_ARR[*]}; then
    rc=\${PIPESTATUS[0]}
else
    rc=0
fi

exit \$rc"; then
    rc=${PIPESTATUS[0]}
    if ${SETUP_ONLY:-false}; then
        exit "$rc"
    fi
else
    if ${SETUP_ONLY:-false}; then
        trap '' EXIT
        exit 0
    fi
    rc=0
fi

exit "$rc"
