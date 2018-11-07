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

# shellcheck disable=SC1091
. .build_vars.sh

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

# shellcheck disable=SC2154
trap 'set +e
restore_dist_files "${yaml_files[@]}"
i=5
while [ $i -gt 0 ]; do
    pdsh -l jenkins -R ssh -S -w "$(IFS=','; echo "${nodes[*]}")" "sudo umount /mnt/daos
    x=0
    while [ \$x -lt 30 ] &&
          grep $DAOS_BASE /proc/mounts &&
          ! sudo umount $DAOS_BASE; do
        sleep 1
        let x+=1
    done
    sudo sed -i -e \"/added by ftest.sh/d\" /etc/fstab
    sudo rmdir $DAOS_BASE" 2>&1 | dshbak -c
    if [ ${PIPESTATUS[0]} = 0 ]; then
        i=0
    fi
    let i-=1
done' EXIT

DAOS_BASE=${SL_PREFIX%/install}
if ! pdsh -l jenkins -R ssh -S -w "$(IFS=','; echo "${nodes[*]}")" "set -ex
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
sudo mkdir -p $DAOS_BASE
sudo ed <<EOF /etc/fstab
\\\$a
$NFS_SERVER:$PWD $DAOS_BASE nfs defaults 0 0 # added by ftest.sh
.
wq
EOF
sudo mount $DAOS_BASE
rm -rf /tmp/Functional_$TEST_TAG/
mkdir -p /tmp/Functional_$TEST_TAG/
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
if ! ssh -i ci_key jenkins@"${nodes[0]}" "set -ex
ulimit -c unlimited
rm -rf $DAOS_BASE/install/tmp
mkdir -p $DAOS_BASE/install/tmp
cd $DAOS_BASE
export CRT_ATTACH_INFO_PATH=$DAOS_BASE/install/tmp
export DAOS_SINGLETON_CLI=1
export CRT_CTX_SHARE_ADDR=1
export CRT_PHY_ADDR_STR=ofi+sockets
export ABT_ENV_MAX_NUM_XSTREAMS=100
export ABT_MAX_NUM_XSTREAMS=100
export OFI_INTERFACE=eth0
export OFI_PORT=23350
# At Oct2018 Longmond F2F it was decided that per-server logs are preferred
# But now we need to collect them!
export DD_LOG=/tmp/Functional_$TEST_TAG/daos.log
export DD_SUBSYS=\"\"
export DD_MASK=all
export D_LOG_FILE=/tmp/Functional_$TEST_TAG/daos.log
export D_LOG_MASK=DEBUG,RPC=ERR,MEM=ERR

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

# make sure no lingering corefiles exist
rm -f core.*

# now run it!
export PYTHONPATH=./util:../../utils/py/:./util/apricot
if ! ./launch.py -s \"$TEST_TAG\"; then
    rc=\${PIPESTATUS[0]}
else
    rc=0
fi

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
else
    rc=0
fi

# collect the logs
if ! rpdcp -l jenkins -R ssh -w "$(IFS=','; echo "${nodes[*]}")" \
    /tmp/Functional_"$TEST_TAG"/\*daos.log "$PWD"/; then
    echo "Copying daos.logs from remote nodes failed"
    # pass
fi
exit "$rc"
