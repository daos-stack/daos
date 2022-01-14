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


readonly yaml_path="/etc/daos"
readonly meta_path="/usr/share/daos/gcp_metadata.sh"
readonly systemd_file="/usr/lib/systemd/system/daos_server.service"

echo "Setting up DAOS server version ${DAOS_VERSION}"

# Template config files have been copied by packer to /tmp/daos_configs
cp -f /tmp/configs/* ${yaml_path}
chown -f daos_server.daos_server ${yaml_path}/*.yml
rm -rf /tmp/configs

# Copy script parsing instance metadata
cp -f /tmp/gcp_metadata.sh ${meta_path}
chown -f daos_server.daos_server ${meta_path}
chmod +x ${meta_path}

# Create directory for engine logs and tmpfs mount point
mkdir -p /var/daos
chown -f daos_server.daos_server /var/daos

# Modify systemd script for GCP
# First, run daos_server as root since GCP does not support VFIO
sed -i "s/User=daos_server/User=root/; s/Group=daos_server/Group=root/" ${systemd_file}
# Then, run gcp_metadata.sh before starting the service
# by using ExecStartPre in systemd unit file
sed -i "/^ExecStart.*/a ExecStartPre=${meta_path}" ${systemd_file}

# enable daos_server in systemd (will be started automatically at boot time)
systemctl enable daos_server

# TODO:
# - somehow enable certificates
