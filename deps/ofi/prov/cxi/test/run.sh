#!/usr/bin/env bash
#
# Run a command in a VM. Start a new VM if necessary.

RUNCMD=$@
DIR=`dirname $0`

if ! [ -c /dev/cxi0 ]; then
	echo "Cassini device not present; attempting to launch netsim VM"
	RUNCMD="$RUNCMD" $DIR/startvm.sh
else
	if [ -z "$RUNCMD" ]; then
		RUNCMD=${SHELL}
	fi
	${RUNCMD}
fi
