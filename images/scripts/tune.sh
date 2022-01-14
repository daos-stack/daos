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


readonly TUNED_PROFILE="network-latency"
readonly SYSCTL_CONF="/etc/sysctl.conf"
readonly NIC="eth0"
readonly SELINUX_CONFIG="/etc/selinux/config"

update_sysctl()
{
  local key=$1
  shift
  local value=$*
  echo "Updating sysctl key=$key, value=$value"
  touch $SYSCTL_CONF
  local regex="s/^\s*${key}\s*=.*$/${key} = ${value}/g"
  sed -i "$regex" "$SYSCTL_CONF"
  if ! grep -Fq "$key" "$SYSCTL_CONF"; then
    echo "sysctl $key not found, appending to $SYSCTL_CONF"
    echo "$key = $value" >> "$SYSCTL_CONF"
  fi
}

echo "Use $TUNED_PROFILE profile in tuned-adm"
tuned-adm profile $TUNED_PROFILE

echo "Tune TCP Memory"
update_sysctl net.ipv4.tcp_rmem 4096 87380 16777216
update_sysctl net.ipv4.tcp_wmem 4096 16384 16777216
sysctl -p

echo "Tune NIC queues"
driver=$(ethtool -i $NIC | grep driver | cut -d':' -f2 | xargs)
if [[ "$driver" == "virtio_net" ]]; then
  nr_cpus=$(lscpu -p | grep -v '^#' | sort -t, -k 2,4 -u | wc -l)
  hw_queues=$(ethtool -l ${NIC} | grep -A4 maximums | tail -n 1 | cut -d':' -f2)
  queues=$(( $nr_cpus > $hw_queues ? $hw_queues : $nr_cpus))
  ethtool -L $NIC combined $queues
fi

echo "Disable SELinux"
sed -i 's/^SELINUX=.*$/SELINUX=disabled/g' $SELINUX_CONFIG

echo "Disable firewalld"
systemctl stop firewalld
systemctl disable firewalld
