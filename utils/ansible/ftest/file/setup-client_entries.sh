#!/bin/bash

# set -x
set -euo pipefail

DAOS_AGENT_UUID=667
declare -A DAOS_GROUP_ENTRIES=(
	[daos_agent]=667
	[daos_daemons]=668
	[daos_metrics]=669
)

function in_group() {
	local user=$1
	local group=$2

	for grp in $(id -nG "$user"); do
		if [[ "$grp" == "$group" ]]; then
			return 0
		fi
	done
	return 1
}

if [[ $(id -u) -ne 0 ]] ; then
	echo "[ERROR] Could only be used by root"
	exit 1
fi

for group in "${!DAOS_GROUP_ENTRIES[@]}"; do
	if ! getent group "$group" >/dev/null; then
		echo "Adding group $group"
		groupadd -r -g "${DAOS_GROUP_ENTRIES[$group]}" "$group"
	fi
done

if ! getent passwd daos_agent >/dev/null; then
	echo "Adding user daos_agent"
	useradd -s /sbin/nologin -r -u $DAOS_AGENT_UUID -g daos_agent daos_agent
fi

if ! in_group daos_agent daos_daemons ; then
	echo "Adding daos_agent to daos_daemons group"
	usermod -aG daos_daemons daos_agent
fi
if ! in_group daos_agent daos_metrics ; then
	echo "Adding daos_agent to daos_metrics group"
	usermod -aG daos_metrics daos_agent
fi
