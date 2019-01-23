#!/usr/bin/env bash

## Wrap spdk setup script so we can isolate commands that require elevated
## privileges including changing directory permissions (which enables spdk
## to be run by an unprivileged user).
## These sudo commands can be granted using visudo by a system administrator.

set -e

rootdir=$(readlink -f $(dirname "$0"))/..
scriptpath="$rootdir/spdk/scripts/setup.sh"

if [[ $1 == reset ]]; then
	sudo "$scriptpath" reset
else
	# avoid shadowing by prefixing input envars
	sudo NRHUGE="$_NRHUGE" TARGET_USER="$_TARGET_USER" "$scriptpath"

	sudo chmod 777 /dev/hugepages
	sudo chmod 666 /dev/uio*
	sudo chmod 666 /sys/class/uio/uio*/device/config
	sudo chmod 666 /sys/class/uio/uio*/device/resource*
fi

