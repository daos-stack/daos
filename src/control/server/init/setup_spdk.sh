#!/bin/bash

## Wrap spdk setup script. This script will be called by daos_server_helper process which will be
## running with elevated privileges. Activities include changing directory permissions (which
## enables spdk to be run by an unprivileged user).

set +e

#
# Sets appropriate permissions on /dev/vfio/* files
#
set_vfio_permissions()
{
	# make sure regular users can read /dev/vfio
	echo "RUN: chmod /dev/vfio"
	if ! chmod a+x /dev/vfio; then
		echo "FAIL"
		return
	fi
	echo "OK"

	# make sure regular user can access everything inside /dev/vfio
	echo "RUN: chmod /dev/vfio/*"
	if ! chmod 0666 /dev/vfio/*; then
		echo "FAIL"
		return
	fi
	echo "OK"

	# since permissions are only to be set when running as
	# regular user, we only check ulimit here
	#
	# warn if regular user is only allowed
	# to memlock <64M of memory
	MEMLOCK_AMNT="$(ulimit -l)"

	if [ "$MEMLOCK_AMNT" != "unlimited" ] ; then
		MEMLOCK_MB="$((MEMLOCK_AMNT / 1024))"
		echo ""
		echo "Current user memlock limit: ${MEMLOCK_MB} MB"
		echo ""
		echo "This is the maximum amount of memory you will be"
		echo "able to use with DPDK and VFIO if run as current user."
		echo -n "To change this, please adjust limits.conf memlock "
		echo "limit for current user."

		if [ "$MEMLOCK_AMNT" -lt 65536 ] ; then
			echo ""
			echo "## WARNING: memlock limit is less than 64MB"
			echo -n "## DPDK with VFIO may not be able to "
			echo "initialize if run as current user."
		fi
	fi
}

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

	if [ -d "/dev/hugepages/" ]; then
		echo "RUN: chown -R ${_TARGET_USER} /dev/hugepages"
		chown -R "${_TARGET_USER}" "/dev/hugepages"
	fi

	echo "Setting VFIO file permissions for unprivileged access"
	set_vfio_permissions
fi

