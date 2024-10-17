#!/bin/bash
trap 'kill $(jobs -p)' EXIT

DIR=$1
MAX=$2

while true; do
	file=$DIR/$((RANDOM % MAX))
	attr=user.$((RANDOM % MAX))
	setfattr -x $attr $file 2>/dev/null
done
