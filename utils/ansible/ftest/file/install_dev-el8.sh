#!/bin/bash

# set -x
set -u -e -o pipefail

CWD="$(realpath "${0%}")"
CWD="${CWD%/*}"

if [[ $(id -u) -ne 0 ]] ; then
	echo "[ERROR] Could only be used by root"
	exit 1
fi

dnf --assumeyes install dnf-plugins-core
dnf config-manager --save --setopt=assumeyes=True

bash "$CWD/install-el8.sh"

dnf build-dep --skip-unavailable --spec "$CWD/daos.spec"
