#!/bin/bash
# Copyright 2021 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


echo "Installing stack driver"

# install stack driver
curl -sSO https://dl.google.com/cloudagents/add-monitoring-agent-repo.sh
bash add-monitoring-agent-repo.sh
yum install -y stackdriver-agent

echo "Installing DAOS version ${DAOS_VERSION}"

# Install 1.2.0 RPMs from official site
tee /etc/yum.repos.d/daos.repo > /dev/null <<EOF
[daos-packages]
name=DAOS v1.2 Packages
baseurl=https://packages.daos.io/v1.2/CentOS7/packages/x86_64/
enabled=1
gpgcheck=1
protect=1
gpgkey=https://packages.daos.io/RPM-GPG-KEY
EOF

# Install DAOS RPMs
yum install -y daos-server

# TODO:
# - enable gvnic
