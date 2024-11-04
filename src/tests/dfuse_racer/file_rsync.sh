#!/bin/bash
trap 'kill $(jobs -p)' EXIT

DIR=$1

RSYNC=${RSYNC:-"rsync -pa"}
while /bin/true ; do
	mkdir -p $DIR/rsync
	$RSYNC /usr/include $DIR/rsync 2> /dev/null
	rm -rf $DIR/rsync 2> /dev/null
	wait
	sleep 1
done
