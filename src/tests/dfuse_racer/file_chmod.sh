#!/bin/bash
trap 'kill $(jobs -p)' EXIT

DIR=$1
MAX=$2

while true; do
	file=$DIR/$((RANDOM % MAX))
	mode=$(printf '%o' $((RANDOM % 010000)))
	chmod $mode $file 2> /dev/null
done
