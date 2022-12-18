#!/usr/bin/env bash

## Wrap spdk setup script. This script will be called by daos_server_helper process which will be
## running with elevated privileges.

set +e

thisscriptname="$(basename "$0")"
thisscriptpath="$(dirname "$(readlink -f "$0")")"

echo "start of script: $thisscriptpath/$thisscriptname"

rootdir="$(readlink -f "$(dirname "$0")")"/../..
scriptpath="$rootdir/spdk/scripts/setup.sh"
if [ ! -f "$scriptpath" ]; then
    if [ -f /usr/share/spdk/scripts/setup.sh ]; then
        scriptpath=/usr/share/spdk/scripts/setup.sh
	else
	    echo "Could not find the SPDK setup.sh script" >&2
		exit 1
	fi
fi

echo "calling into script: $scriptpath"

# avoid shadowing by prefixing input envars
if [[ $1 == reset ]]; then
	set -x
	PCI_ALLOWED=${_PCI_ALLOWED}		\
	PCI_BLOCKED=${_PCI_BLOCKED}		\
	PATH=/sbin:${PATH}			\
	${scriptpath} reset
	set +x
else
	set -x
	PCI_ALLOWED=${_PCI_ALLOWED}		\
	PCI_BLOCKED=${_PCI_BLOCKED}		\
	NRHUGE=${_NRHUGE} 			\
	HUGENODE=${_HUGENODE} 			\
	TARGET_USER=${_TARGET_USER}		\
	DRIVER_OVERRIDE=${_DRIVER_OVERRIDE}	\
	PATH=/sbin:${PATH}			\
	${scriptpath}
	set +x
fi
