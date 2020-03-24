#!/usr/bin/bash

# Copyright (C) 2016 Intel Corporation
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


function fuse3_check {
  local CORAL_ARTIFACTS="/scratch/jenkins-1/artifacts"
  local f3_tar1="tar -C /opt --strip-components=1 --owner=root -xzf"
  f3_tar1="${f3_tar1} ${CORAL_ARTIFACTS}/fuse-3_0_bugfix-opt/distro=e17"
  local f3_tar2="/latest/fuse-3_0_bugfix.tar.gz"

  rm -rf /opt/fuse3
  mkdir -p /opt/fuse3

  echo ${f3_tar1} ${f3_tar2}
  ${f3_tar1} ${f3_tar2}
  ln -s -f /opt/fuse3/bin/fusermount3 /usr/bin/fusermount3
}

function rm_check {
 yum -y remove compat-openmpi16
 yum -y remove openmpi
 rm -f /etc/profile.d/openmpi.sh
}

function yum_check {
 yum -y install fuse fuse-devel openssl-devel
 yum -y install python34 python34-devel
 yum -y install python34-libs python34-tools
 yum -y install PyYAML libyaml
 yum -y install valgrind
 yum -y install libffi-devel
}

function pip3_check {
  hostname
  if [ ! -e /usr/bin/python3 ] ; then
    ln -s /usr/bin/python3.4 /usr/bin/python3
  else
    echo "python3 link exists."
  fi

  if [ ! -e /usr/bin/pip3 ] ; then
    curl -k "https://bootstrap.pypa.io/get-pip.py" -o "get-pip.py"
    python3 get-pip.py
    cp /usr/bin/pip2 /usr/bin/pip

    pip_additions="pyopenssl ndg-httpsclient pyasn1 paramiko"
    pip_additions+="flake8 sphinx pytest pytest-cov gcovr requests pylint "
    pip_additions+="netifaces astroid pyyaml numpy GitPython"
    pip3 install -U ${pip_additions}
  else
    echo "pip3 exists."
  fi
}

if [[  $(whoami) != "root" ]]; then
    echo "This script needs to be executed as root"
    exit 1
fi
rm_check
yum_check
pip3_check
