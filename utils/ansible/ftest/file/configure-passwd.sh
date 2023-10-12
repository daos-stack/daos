#!/bin/bash

# set -x
set -u -e -o pipefail

if [[ $(id -u) -ne 0 ]] ; then
	echo "[ERROR] Could only be used by root"
	exit 1
fi

USERNAME="${1:?Missing user name}"

if cut -d: -f1 /etc/passwd | grep --silent "^$USERNAME\$" ; then
	exit 0
fi

if ! getent passwd "$USERNAME" > /dev/null 2>&1 ; then
	echo "Invalid user $USERNAME"
	exit 1
fi

echo "Add user $USERNAME to /etc/passwd"
getent passwd "$USERNAME" >> /etc/passwd
