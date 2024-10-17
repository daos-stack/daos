#!/bin/bash
trap 'kill $(jobs -p)' EXIT

org_LANG=$LANG
export LANG=C

DIR=$1
MAX=$2
PROG=/bin/sleep

while /bin/true ; do
	file=$((RANDOM % MAX))
	cp -p $PROG $DIR/$file > /dev/null 2>&1
	$DIR/$file 0.$((RANDOM % 5 + 1)) 2> /dev/null
	sleep $((RANDOM % 3))
done 2>&1 | egrep -v "Segmentation fault|Bus error"

export LANG=$org_LANG
