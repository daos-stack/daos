#!/bin/bash

# set -x
set -euo pipefail

DAOS_USERS=(
	daos_server
	daos_agent
)

DAOS_GROUPS=(
	daos_daemons
	daos_server
	daos_agent
	daos_metrics
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

gid=666
for group in "${DAOS_GROUPS[@]}"; do
    	if ! getent group "$group" >/dev/null; then
		echo "Adding group $group"
		groupadd -r -g $gid "$group"
    	fi
    	gid=$((gid + 1))
done

uid=666
for user in "${DAOS_USERS[@]}"; do
    	if ! getent passwd "$user" >/dev/null; then
		echo "Adding user $user"
		useradd -s /sbin/nologin -r -u $uid -g "$user" -G daos_daemons "$user"
    	fi
    	uid=$((uid + 1))
done
if ! $(in_group daos_server daos_metrics); then
	echo "Adding daos_server to daos_metrics group"
	usermod -aG daos_metrics daos_server
fi
if ! $(in_group daos_server daos_daemons); then
	echo "Adding daos_server to daos_daemons group"
	usermod -aG daos_daemons daos_server
fi
if ! $(in_group daos_agent daos_metrics); then
	echo "Adding daos_agent to daos_metrics group"
	usermod -aG daos_metrics daos_agent
fi
if ! $(in_group daos_agent daos_daemons); then
	echo "Adding daos_agent to daos_daemons group"
	usermod -aG daos_daemons daos_agent
fi
