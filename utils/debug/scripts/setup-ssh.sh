#!/bin/bash

# set -x
set -e -o pipefail

NODESET=${1:?Missing nodeset parameter}
USERNAME=${2:?Missing username}
PASSWD=${3:?Missing password}

echo "[INFO] Setup ssh authorization"
for host in $(nodeset -e $NODESET) ; do
	echo -e "[INFO]\t- host $host"
	if ! ssh -o BatchMode=yes $USERNAME@$host true ; then
		sshpass -p $PASSWD ssh-copy-id -i "$HOME/.ssh/id_ed25519" $USERNAME@$host ;
	fi
done
