#!/bin/bash

set -euxo pipefail

wait_seconds=600

reqid=${REQID:-$(reqidgen)}
echo "CLUSTER_REQUEST_reqid=$reqid" >> "$GITHUB_ENV"
trap 'rm -f $cookiejar' EXIT
cookiejar="$(mktemp)"
crumb="$(curl --cookie-jar "$cookiejar" "${JENKINS_URL}crumbIssuer/api/xml?xpath=concat(//crumbRequestField,\":\",//crumb)")"
url="${JENKINS_URL}job/Get%20a%20cluster/buildWithParameters?token=mytoken&LABEL=$LABEL&REQID=$reqid&BuildPriority=${PRIORITY:-3}"
trap 'set -x; rm -f $cookiejar $headers_file' EXIT
headers_file="$(mktemp)"
if ! queue_url=$(curl -D "$headers_file" -v -f -X POST --cookie "$cookiejar" -H "$crumb" "$url" &&
                 sed -ne 's/\r//' -e '/Location:/s/.*: //p' "$headers_file") || [ -z "$queue_url" ]; then
    echo "Failed to request a cluster."
    cat "$headers_file"
    exit 1
fi
echo QUEUE_URL="$queue_url" >> "$GITHUB_ENV"
# disable xtrace here as this could loop for a long time
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
if [[ $NODESTRING = *vm* ]]; then
    ssh -oPasswordAuthentication=false -v root@"${NODESTRING%%vm*}" \
        "POOL=${CP_PROVISIONING_POOL:-}
         NODESTRING=$NODESTRING
         NODELIST=$NODESTRING
         DISTRO=$DISTRO_WITH_VERSION
         $(cat ci/provisioning/provision_vm_cluster.sh)"
else
    START=$SECONDS
    while [ $((SECONDS-START)) -lt $wait_seconds ]; do
        if clush -B -S -l root -w "$NODESTRING" '[ -d /var/chef/reports ]'; then
            # shellcheck disable=SC2016
            clush -B -S -l root -w "$NODESTRING" --connect_timeout 30 --command_timeout 600 'if [ -e /root/job_info ]; then
                    cat /root/job_info
                fi
                if ! POOL="" restore_partition.sh daos_ci-el8 noreboot; then
                    rc=${PIPESTATUS[0]}
                    distro=el8
                    while [[ $distro = *.* ]]; do
                        distro=${distro%.*}
                            if ! restore_partition.sh daos_ci-${distro} noreboot; then
                                rc=${PIPESTATUS[0]}
                                continue
                            else
                                exit 0
                            fi
                    done
                    exit "$rc"
                    fi'
                clush -B -S -l root -w "$NODESTRING" --connect_timeout 30 --command_timeout 120 -S 'init 6' || true
                START=$SECONDS
                while [ $((SECONDS-START)) -lt $wait_seconds ]; do
                    if clush -B -S -l root -w "$NODESTRING" '[ -d /var/chef/reports ]'; then
                        exit 0
                    fi
                    sleep 1
                done
                exit 1
        fi
        sleep 1
    done
    exit 1
fi