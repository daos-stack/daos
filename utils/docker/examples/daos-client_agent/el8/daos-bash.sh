#!/bin/bash

# set -x
set -u -e -o pipefail

if [[ "$(id -u)" != "0" ]] ; then
	echo "[ERROR] daos-bash can only be run as root"
fi

nohup sudo --user=root --group=root /usr/local/sbin/run-daos_agent start < /dev/null &> /dev/null &

exec sudo --user=@DAOS_CLIENT_UNAME@ --group=@DAOS_CLIENT_GNAME@ /bin/bash "$@"
