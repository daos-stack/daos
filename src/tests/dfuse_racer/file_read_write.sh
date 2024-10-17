#!/bin/bash
trap 'kill $(jobs -p)' EXIT

DIR=$1
MAX=$2

while /bin/true ; do
	file=$((RANDOM % MAX))
	ls -R $DIR > $DIR/$file 1> /dev/null 2> /dev/null

	if [ -f $DIR/$file ]; then
		cat $DIR/$file 1> /dev/null 2> /dev/null || true
	fi
	rm -rf $DIR/$file 2> /dev/null
done
