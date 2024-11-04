#!/bin/bash
trap 'kill $(jobs -p)' EXIT

DIR=$1
MAX=$2

RENAME=${RENAME:-$(which mrename 2> /dev/null)}
RENAME=${RENAME:-"mv"}

while /bin/true ; do
	file=$((RANDOM % MAX))
	((RANDOM % 100 < 10)) && file+="/$((RANDOM % MAX))"
	new_file=$((RANDOM % MAX))
	((RANDOM % 100 < 10)) && new_file+="/$((RANDOM % MAX))"
	$RENAME $DIR/$file $DIR/$new_file 2> /dev/null
done
