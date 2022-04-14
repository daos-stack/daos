#!/bin/bash
# Copyright 2019-2022 Intel Corporation
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

NFS_SERVER=${NFS_SERVER:-${HOSTNAME%%.*}}

trap 'echo "encountered an unchecked return code, exiting with error"' ERR

# shellcheck disable=SC1091
. .build_vars-Linux.sh

if [ -z "$CART_TEST_MODE"  ]; then
  CART_TEST_MODE="native"
fi

if [ "$CART_TEST_MODE" == "memcheck" ]; then
  CART_DIR="${1}vgd"
else
  CART_DIR="${1}"
fi

IFS=" " read -r -a nodes <<< "${2//,/ }"

# shellcheck disable=SC1004
# shellcheck disable=SC2154
trap 'set +e
i=5
while [ $i -gt 0 ]; do
    pdsh -l jenkins -R ssh -S \
         -w "$(IFS=','; echo ${nodes[*]:0:$1})" "set -x
    x=0
    rc=0
    while [ \$x -lt 30 ] &&
          grep $CART_BASE /proc/mounts &&
          ! sudo umount $CART_BASE; do
        # CART-558 - cart tests leave orte processes around
        if pgrep -a cart_ctl; then
            echo "FAIL: cart_ctl found left running on \$HOSTNAME"
            rc=1
        fi
        pgrep -a \(orte-dvm\|orted\|cart_ctl\)
        pkill \(orte-dvm\|orted\|cart_ctl\)
        sleep 1
        let x+=1
    done
    sudo sed -i -e \"/added by multi-node-test-$1.sh/d\" /etc/fstab
    sudo rmdir $CART_BASE || find $CART_BASE || true
    if [ \$rc != 0 ]; then
        exit \$rc
    fi" 2>&1 | dshbak -c
    if [ ${PIPESTATUS[0]} = 0 ]; then
        i=0
    fi
    let i-=1
done' EXIT

CART_BASE="${SL_PREFIX%/install/*}"
if ! pdsh -l jenkins -R ssh -S \
          -w "$(IFS=','; echo "${nodes[*]:0:$1}")" "set -ex
ulimit -c unlimited
sudo mkdir -p $CART_BASE
sudo ed <<EOF /etc/fstab
\\\$a
$NFS_SERVER:$PWD $CART_BASE nfs defaults 0 0 # added by multi-node-test-$1.sh
.
wq
EOF
sudo mount $CART_BASE

# TODO: package this in to an RPM
pip3 install --user tabulate

df -h" 2>&1 | dshbak -c; then
    echo "Cluster setup (i.e. provisioning) failed"
    exit 1
fi

TEST_TAG="${3:-quick}"

TESTDIR=${SL_PREFIX}/TESTING

LOGDIR="$TESTDIR/avocado/job-results/CART_${CART_DIR}node"

# shellcheck disable=SC2029
if ! ssh -i ci_key jenkins@"${nodes[0]}" "set -ex
ulimit -c unlimited
cd $CART_BASE

mkdir -p ~/.config/avocado/
cat <<EOF > ~/.config/avocado/avocado.conf
[datadir.paths]
logs_dir = $LOGDIR

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
/testsuite.setAttribute('name', 'avocado')/s/'avocado'/\
os.path.basename(os.path.dirname(result.logfile))/
wq
EOF
fi

# put yaml files back
restore_dist_files() {
    local dist_files=\"\$*\"

    for file in \$dist_files; do
        if [ -f \"\$file\".dist ]; then
            mv -f \"\$file\".dist \"\$file\"
        fi
    done

}

# set our machine names
mapfile -t yaml_files < <(find \"$TESTDIR\" -name \\*.yaml)

trap 'set +e; restore_dist_files \"\${yaml_files[@]}\"' EXIT

# shellcheck disable=SC2086
sed -i.dist -e \"s/- boro-A/- ${nodes[0]}/g\" \
            -e \"s/- boro-B/- ${nodes[1]}/g\" \
            -e \"s/- boro-C/- ${nodes[2]}/g\" \
            -e \"s/- boro-D/- ${nodes[3]}/g\" \
            -e \"s/- boro-E/- ${nodes[4]}/g\" \
            -e \"s/- boro-F/- ${nodes[5]}/g\" \
            -e \"s/- boro-G/- ${nodes[6]}/g\" \
            -e \"s/- boro-H/- ${nodes[7]}/g\" \"\${yaml_files[@]}\"

# let's output to a dir in the tree
rm -rf \"$TESTDIR/avocado\" \"./*_results.xml\"
mkdir -p \"$LOGDIR\"

# shellcheck disable=SC2154
trap 'set +e restore_dist_files \"\${yaml_files[@]}\"' EXIT

pushd \"$TESTDIR\"

# now run it!
export PYTHONPATH=./util
export CART_TEST_MODE=\"$CART_TEST_MODE\"
if ! ./launch.py -s \"$TEST_TAG\"; then
    rc=\${PIPESTATUS[0]}
else
    rc=0
fi

mkdir -p \"testLogs-${CART_DIR}_node\"

cp -r testLogs/* \"testLogs-${CART_DIR}_node\"

exit \$rc"; then
    rc=${PIPESTATUS[0]}
else
    rc=0
fi

mkdir -p install/Linux/TESTING/avocado/job-results

scp -i ci_key -r jenkins@${nodes[0]}:"$TESTDIR/testLogs-${CART_DIR}_node" \
                                      install/Linux/TESTING/

scp -i ci_key -r jenkins@${nodes[0]}:"$LOGDIR" \
                                      install/Linux/TESTING/avocado/job-results

exit "$rc"
