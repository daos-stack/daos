#!/bin/bash

set -euxo pipefail

env | sort
reqid=${REQID:-$(reqidgen)}
echo "CLUSTER_REQUEST_reqid=$reqid" >> "$GITHUB_ENV"
url='https://build.hpdd.intel.com/job/Get%20a%20cluster/buildWithParameters?token=mytoken&LABEL=stage_vm9&'"REQID=$reqid"
if ! queue_url=$(curl -D - -f -v -X POST --user "$JENKINS_TOKEN" "$url" |
                 sed -ne 's/\r//' -e '/Location:/s/.*: //p'); then
    echo "Failed to request a cluster."
    exit 1
fi
set +x
while [ ! -f /scratch/Get\ a\ cluster/"$reqid" ]; do
    if [ $((SECONDS % 60)) -eq 0 ]; then
        { read -r cancelled; read -r why; } < \
            <(curl -sf --user "$JENKINS_TOKEN" "${queue_url}api/json/" |
              jq -r .cancelled,.why)
        if [ "$cancelled" == "true" ]; then
            echo "Cluster request cancelled from Jenkins"
            exit 1
        fi
        echo "$why"
    fi
    sleep 1
done
NODESTRING=$(cat /scratch/Get\ a\ cluster/"$reqid")
if [ "$NODESTRING" = "cancelled" ]; then
    echo "Cluster request cancelled from Jenkins"
    exit 1
fi
{ echo "NODESTRING=$NODESTRING"
  echo "NODELIST=$NODESTRING"; } >> "$GITHUB_ENV"
echo "NODE_COUNT=$(echo "$NODESTRING" | tr ',' ' ' | wc -w)" >> "$GITHUB_ENV"
cat "$GITHUB_ENV"
ssh -oPasswordAuthentication=false -v root@"${NODESTRING%%vm*}" \
    "POOL=${CP_PROVISIONING_POOL:-}
     NODESTRING=$NODESTRING
     NODELIST=$NODESTRING
     DISTRO=$DISTRO_WITH_VERSION
     $(cat ci/provisioning/provision_cluster.sh)"