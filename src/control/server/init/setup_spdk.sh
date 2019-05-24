#!/usr/bin/env bash

## Wrap spdk setup script so we can isolate commands that require elevated
## privileges including changing directory permissions (which enables spdk
## to be run by an unprivileged user).
## These sudo commands can be granted using visudo by a system administrator.

set -e

rootdir="$(readlink -f "$(dirname "$0")")"/..
scriptpath="$rootdir/spdk/scripts/setup.sh"

if [[ $1 == reset ]]; then
	"$scriptpath" reset
else
	# avoid shadowing by prefixing input envars
	PCI_WHITELIST="$_PCI_WHITELIST" NRHUGE="$_NRHUGE" \
	TARGET_USER="$_TARGET_USER" "$scriptpath"

	chmod 777 /dev/hugepages
	chmod 666 /dev/uio*
	chmod 666 /sys/class/uio/uio*/device/config
	chmod 666 /sys/class/uio/uio*/device/resource*
fi

