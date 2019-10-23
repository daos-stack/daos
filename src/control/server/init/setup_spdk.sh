#!/usr/bin/env bash

## Wrap spdk setup script so we can isolate commands that require elevated
## privileges including changing directory permissions (which enables spdk
## to be run by an unprivileged user).
## These sudo commands can be granted using visudo by a system administrator.

set -e

rootdir="$(readlink -f "$(dirname "$0")")"/../..
scriptpath="$rootdir/spdk/scripts/setup.sh"

if [[ $1 == reset ]]; then
	"$scriptpath" reset
else
	# avoid shadowing by prefixing input envars
	PCI_WHITELIST="$_PCI_WHITELIST" NRHUGE="$_NRHUGE" \
	TARGET_USER="$_TARGET_USER" "$scriptpath"

	# build arglist manually to filter missing directories/files
	# so we don't error on non-existent entities
	for glob in '/dev/hugepages' '/dev/uio*'		\
		'/sys/class/uio/uio*/device/config'	\
		'/sys/class/uio/uio*/device/resource*'; do

		if list=$(ls $glob); then
			echo "RUN: ls $glob | xargs -r chown -R $_TARGET_USER"
			echo "$list" | xargs -r chown -R "$_TARGET_USER"
		fi
	done
fi

