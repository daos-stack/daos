#!/bin/bash
trap 'kill $(jobs -p)' EXIT

DIR=$1
MAX=$2

while true; do
	old_path=$DIR/$((RANDOM % MAX))
	new_path=$DIR/$((RANDOM % MAX))
	ln $old_path $new_path 2> /dev/null
done
