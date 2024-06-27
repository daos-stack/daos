#!/bin/bash

bdis=`awk '{if ($9=="fuse.daos") {print $3}}' /proc/self/mountinfo`

for bdi in $bdis; do
	echo "Setting tunables for $bdi"
	echo "... bumping readahead to 4M"
	echo 4096 > /sys/class/bdi/$bdi/read_ahead_kb
	echo "... bumping max dirty ratio to 50%"
	echo 50 > /sys/class/bdi/$bdi/max_ratio
done
