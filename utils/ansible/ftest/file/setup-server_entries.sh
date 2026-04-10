#!/bin/bash

# set -x
set -euo pipefail

DAOS_SERVER_UID=666
declare -A DAOS_GROUP_ENTRIES=(
	[daos_server]=666
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

for group in "${!DAOS_GROUP_ENTRIES[@]}" ; do
	if ! getent group "$group" >/dev/null; then
		echo "Adding group $group"
		groupadd -r -g "${DAOS_GROUP_ENTRIES[$group]}" "$group"
	fi
done

if ! getent passwd daos_server >/dev/null; then
	echo "Adding user daos_server"
	useradd -s /sbin/nologin -r -u $DAOS_SERVER_UID -g daos_server daos_server
fi

if ! in_group daos_server daos_metrics ; then
	echo "Adding daos_server to daos_metrics group"
	usermod -aG daos_metrics daos_server
fi
if ! in_group daos_server daos_daemons ; then
	echo "Adding daos_server to daos_daemons group"
	usermod -aG daos_daemons daos_server
fi
