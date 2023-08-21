#!/bin/bash

set -euxo pipefail

env | sort
reqid=${REQID:-$(reqidgen)}
echo "CLUSTER_REQUEST_reqid=$reqid" >> "$GITHUB_ENV"
trap 'rm -f $cookiejar' EXIT
cookiejar="$(mktemp)"
crumb="$(curl --cookie-jar "$cookiejar" "${JENKINS_URL}crumbIssuer/api/xml?xpath=concat(//crumbRequestField,\":\",//crumb)")"
url="${JENKINS_URL}job/Get%20a%20cluster/buildWithParameters?token=mytoken&LABEL=stage_vm9&REQID=$reqid"
curl -D - -f -v -X POST --cookie "$cookiejar" -H "$crumb" "$url"
if ! queue_url=$(curl -D - -f -v -X POST "$url" |
                 sed -ne 's/\r//' -e '/Location:/s/.*: //p'); then
    echo "Failed to request a cluster."
    exit 1
fi
set +x
while [ ! -f /scratch/Get\ a\ cluster/"$reqid" ]; do
    if [ $((SECONDS % 60)) -eq 0 ]; then
        { read -r cancelled; read -r why; } < \
            <(curl -sf --cookie "$cookiejar" -H "$crumb" "${queue_url}api/json/" |
              jq -r .cancelled,.why)
        if [ "$cancelled" == "true" ]; then
            echo "Cluster request cancelled from Jenkins"
            exit 1
        fi
        echo "$why"
    fi
    sleep 1
done
set -x
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