#!/bin/bash
trap 'kill $(jobs -p)' EXIT
DIR=$1
MAX=$2
MAX_MB=${RACER_MAX_MB:-8}

while /bin/true; do
	file=$((RANDOM % MAX))
	(( RANDOM % 100 < 25 )) && file+="/$((RANDOM % MAX))"
	# $RANDOM is between 0 and 32767, and we want $blockcount in 64kB units
	blockcount=$((RANDOM * MAX_MB / 32 / 64))

	# offset between 0 and 16MB (256 64k chunks), with 1/2 at offset 0
	seek=$((RANDOM / 64)); [ $seek -gt 256 ] && seek=0
	dd if=/dev/zero of=$DIR/$file bs=64k count=$blockcount \
		seek=$seek 2> /dev/null || true
done

