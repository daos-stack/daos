#!/bin/bash
# Copyright (C) 2019 Intel Corporation
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

TEST_TAG="${1:-quick}"

NFS_SERVER=${NFS_SERVER:-${HOSTNAME%%.*}}

trap 'echo "encountered an unchecked return code, exiting with error"' ERR

IFS=" " read -r -a nodes <<< "${2//,/ }"

# put yaml files back
restore_dist_files() {
    local dist_files="$*"

    for file in $dist_files; do
        if [ -f "$file".dist ]; then
            mv -f "$file".dist "$file"
        fi
    done

}

cleanup() {
    restore_dist_files "${yaml_files[@]}"
    i=5
    while [ $i -gt 0 ]; do
        pdsh -l "${REMOTE_ACCT:-jenkins}" -R ssh -S \
             -w "$(IFS=','; echo "${nodes[*]}")" "set -x
        if grep /mnt/daos /proc/mounts; then
            if ! sudo umount /mnt/daos; then
                echo \"During shutdown, failed to unmount /mnt/daos.  \"\
                     \"Continuing...\"
            fi
        fi
        x=0
        sudo sed -i -e \"/added by ftest.sh/d\" /etc/fstab
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
                echo \"because it doesnt exist\"
            fi
            exit 1
        fi" 2>&1 | dshbak -c
        if [ "${PIPESTATUS[0]}" = 0 ]; then
            i=0
        fi
        ((i-=1))
    done
}

# shellcheck disable=SC1091
. .build_vars.sh

if ${TEARDOWN_ONLY:-false}; then
    cleanup
    exit 0
fi

# set our machine names
mapfile -t yaml_files < <(find src/tests/ftest -name \*.yaml)

trap 'set +e; restore_dist_files "${yaml_files[@]}"' EXIT

# shellcheck disable=SC2086
sed -i.dist -e "s/- boro-A/- ${nodes[1]}/g" \
            -e "s/- boro-B/- ${nodes[2]}/g" \
            -e "s/- boro-C/- ${nodes[3]}/g" \
            -e "s/- boro-D/- ${nodes[4]}/g" \
            -e "s/- boro-E/- ${nodes[5]}/g" \
            -e "s/- boro-F/- ${nodes[6]}/g" \
            -e "s/- boro-G/- ${nodes[7]}/g" \
            -e "s/- boro-H/- ${nodes[8]}/g" "${yaml_files[@]}"


# let's output to a dir in the tree
rm -rf src/tests/ftest/avocado ./*_results.xml
mkdir -p src/tests/ftest/avocado/job-results

trap 'set +e; cleanup' EXIT

DAOS_BASE=${SL_PREFIX%/install}
if ! pdsh -l "${REMOTE_ACCT:-jenkins}" -R ssh -S \
    -w "$(IFS=','; echo "${nodes[*]}")" "set -ex
ulimit -c unlimited
if [ \"\${HOSTNAME%%%%.*}\" != \"${nodes[0]}\" ]; then
    if grep /mnt/daos\\  /proc/mounts; then
        sudo umount /mnt/daos
    else
        if [ ! -d /mnt/daos ]; then
            sudo mkdir -p /mnt/daos
        fi
    fi

    sudo ed <<EOF /etc/fstab
\\\$a
tmpfs /mnt/daos tmpfs rw,relatime,size=16777216k 0 0 # added by ftest.sh
.
wq
EOF
    sudo mount /mnt/daos
fi

# make sure to set up for daos_agent
current_username=\$(whoami)
sudo bash -c \"set -ex
if [ -d  /var/run/daos_agent ]; then
    rmdir /var/run/daos_agent
fi
if [ -d  /var/run/daos_server ]; then
    rmdir /var/run/daos_server
fi
mkdir /var/run/daos_{agent,server}
chown \$current_username -R /var/run/daos_{agent,server}
chmod 0755 /var/run/daos_{agent,server}
mkdir -p $DAOS_BASE
ed <<EOF /etc/fstab
\\\\\\\$a
$NFS_SERVER:$PWD $DAOS_BASE nfs defaults 0 0 # added by ftest.sh
.
wq
EOF
mount \\\"$DAOS_BASE\\\"\"

rm -rf /tmp/Functional_$TEST_TAG/
mkdir -p /tmp/Functional_$TEST_TAG/
if [ -z \"\$JENKINS_URL\" ]; then
    exit 0
fi
sudo bash -c 'set -ex
yum -y install yum-utils
repo_file_base=\"*_job_${JOB_NAME%%/*}_job_\"
# pkgs are of the format pkgname[:branch]
pkgs=\"ior-hpc:daos\"
install_pkgs=\"\"
for ext in \$pkgs; do
    IFS=':' read -ra ext <<< \"\$ext\"
    ext=\"\${ext[0]}\"
    if [ -n \"\${ext[1]}\" ]; then
        branch=\"\${ext[1]}\"
    else
        branch=\"master\"
    fi
    install_pkgs+=\" \$ext\"
    rm -f /etc/yum.repos.d/\${repo_file_base}\${ext}_job_*.repo
    yum-config-manager --add-repo=${JENKINS_URL}job/${JOB_NAME%%/*}/job/\${ext}/job/\${branch}/lastSuccessfulBuild/artifact/artifacts/
    echo \"gpgcheck = False\" >> /etc/yum.repos.d/\${repo_file_base}\${ext}_job_\${branch}_lastSuccessfulBuild_artifact_artifacts_.repo
done
# for testing with a PR for a dependency:
depname=     # i.e. depname=mercury
pr_num=      # set to which PR number your PR is
if [ -n \"\$depname\" ]; then
    rm -f /etc/yum.repos.d/\${repo_file_base}\${depname}_job_PR-\${pr_num}_lastSuccessfulBuild_artifact_artifacts_.repo
    yum-config-manager --add-repo=${JENKINS_URL}job/${JOB_NAME%%/*}/job/\${depname}/job/PR-\${pr_num}/lastSuccessfulBuild/artifact/artifacts/
    echo \"gpgcheck = False\" >> /etc/yum.repos.d/\${repo_file_base}\${depname}_job_PR-\${pr_num}_lastSuccessfulBuild_artifact_artifacts_.repo
    install_pkgs+=\" \${depname}\"
fi
yum -y erase \$install_pkgs
yum -y install \$install_pkgs'" 2>&1 | dshbak -c; then
    echo "Cluster setup (i.e. provisioning) failed"
    exit 1
fi

args="${1:-quick}"
shift || true
args+=" $*"

# shellcheck disable=SC2029
# shellcheck disable=SC2086
if ! ssh $SSH_KEY_ARGS "${REMOTE_ACCT:-jenkins}"@"${nodes[0]}" "set -ex
ulimit -c unlimited
rm -rf $DAOS_BASE/install/tmp
mkdir -p $DAOS_BASE/install/tmp
cd $DAOS_BASE
export CRT_PHY_ADDR_STR=ofi+sockets
export OFI_INTERFACE=eth0
# At Oct2018 Longmond F2F it was decided that per-server logs are preferred
# But now we need to collect them!
export D_LOG_FILE=/tmp/Functional_$TEST_TAG/client_daos.log

mkdir -p ~/.config/avocado/
cat <<EOF > ~/.config/avocado/avocado.conf
[datadir.paths]
logs_dir = $DAOS_BASE/src/tests/ftest/avocado/job-results

[sysinfo.collectibles]
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

# apply patch for https://github.com/avocado-framework/avocado/pull/3076/
if ! grep TIMEOUT_TEARDOWN \
    /usr/lib/python2.7/site-packages/avocado/core/runner.py; then
    sudo yum -y install patch
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

pushd src/tests/ftest

# make sure no lingering corefiles or junit files exist
rm -f core.* *_results.xml

# see if we just wanted to set up
if ${SETUP_ONLY:-false}; then
    exit 0
fi

# now run it!
export PYTHONPATH=./util:../../utils/py/:./util/apricot
if ! ./launch.py -c -a -r -s \"$TEST_TAG\"; then
    rc=\${PIPESTATUS[0]}
else
    rc=0
fi

# Remove the latest avocado symlink directory to avoid inclusion in the
# jenkins build artifacts
unlink $DAOS_BASE/src/tests/ftest/avocado/job-results/latest

# get stacktraces for the core files
if ls core.*; then
    # this really should be a debuginfo-install command but our systems lag
    # current releases
    python_rpm=\$(rpm -q python)
    python_debuginfo_rpm=\"\${python_rpm/-/-debuginfo-}\"

    if ! rpm -q \$python_debuginfo_rpm; then
        sudo yum -y install \
 http://debuginfo.centos.org/7/x86_64/\$python_debuginfo_rpm.rpm
    fi
    sudo yum -y install gdb
    for file in core.*; do
        gdb -ex \"set pagination off\"                 \
            -ex \"thread apply all bt full\"           \
            -ex \"detach\"                             \
            -ex \"quit\"                               \
            /usr/bin/python2 \$file > \$file.stacktrace
    done
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
