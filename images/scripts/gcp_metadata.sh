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


readonly srv_yaml="/etc/daos/daos_server.yml"
readonly agt_yaml="/etc/daos/daos_agent.yml"
readonly ctl_yaml="/etc/daos/daos_control.yml"

readonly URL="http://metadata.google.internal/computeMetadata/v1/instance/attributes"

fetch_attr()
{
  local name=$*
  echo `curl -s ${URL}/${name} -H "Metadata-Flavor: Google"`
}

# Update access points only once
if ! grep -q "changeap" "$srv_yaml" "$agt_yaml"; then
  echo "Access points already populated, skipping"
  exit 0
fi

echo "Extracting instance metadata ..."

inst_type=`fetch_attr inst_type`
if [[ ! "$inst_type" == "daos-server" ]]; then
  echo "Instance type is ${inst_type} and not daos-server, skipping"
  exit 1
fi

base_name=`fetch_attr inst_base_name`
inst_nr=`fetch_attr inst_nr`
# generate list of hosts
hosts=\'`printf "%s-[%04d-%04d]" "${base_name}" 1 ${inst_nr}`\'

echo "Selecting access points among ${hosts}..."

if [[ ${inst_nr} -ge 5 ]]; then
  # Support up to 2 instance failures
  apnr=5
elif [[ ${inst_nr} -ge 3 ]]; then
  # Support single instance failure
  apnr=3
else
  # single-replica, no failure supported
  apnr=1
fi
# choose contiguous access points until we know more about fault domains
# host range not supported in the yaml file yet
# ap=\'`printf "%s-[%04d-%04d]" "${base_name}" 0 $((apnr-1))`\'
# so list each node individually
ap=""
for i in `seq 1 ${apnr}`; do
  name=`printf "%s-%04d" "${base_name}" ${i}`
  if [[ "$ap" == "" ]]; then
    ap=\'${name}\'
  else
    ap=$ap,\'${name}\'
  fi
done
echo "${ap} selected as access points"

echo "Updating yaml files ..."
sed -i "s/hostlist.*/hostlist: [${hosts}]/g" ${ctl_yaml}
sed -i "s/access_points.*/access_points: [${ap}]/g" ${srv_yaml}
sed -i "s/access_points.*/access_points: [${ap}]/g" ${agt_yaml}

echo "All done"
