#!/bin/bash
set -e

DAOSSRC=~/daos/
DMG=${DMG:-$DAOSSRC/install/bin/dmg}
DAOS=${DAOS:-$DAOSSRC/install/bin/daos}
DFUSE=${DFUSE:-$DAOSSRC/install/bin/dfuse}
racer=$DAOSSRC/src/tests/dfuse_racer/racer.sh
MNTDIR=/mnt/dfuse
MAX_FILES=50
DURATION=${DURATION:-300}
NUM_RACER_THREADS=56

POOL_NAME=test_pool
CONT_NAME=test_cont

FILE_CLASS=EC_2P1GX
DIR_CLASS=RP_2GX

racer_setup() {
	echo "setup racer"
	$DMG -i pool create --scm-size 1GB $POOL_NAME -o $DAOSSRC/daos_control.yml|| exit 1
	$DAOS cont create $POOL_NAME $CONT_NAME --type posix			\
			--properties=rd_fac:0,cksum:crc64,srv_cksum:on,rd_lvl:1 \
			--oclass="${FILE_CLASS}" --dir-oclass="${DIR_CLASS}"|| exit 2

	mkdir -p $MNTDIR
	chmod 777 $MNTDIR

	DD_MASK=all D_LOG_MASK=debug D_LOG_FILE=/tmp/daos_client.log $DFUSE -m $MNTDIR \
		--thread-count=20 --eq-count=10 --disable-wb-cache \
			--pool=$POOL_NAME --container=$CONT_NAME --multi-user || exit 3
}

racer_cleanup() {
	fusermount3 -u $MNTDIR
	$DAOS cont destroy $POOL_NAME $CONT_NAME
	$DMG -i pool destroy $POOL_NAME -r -f -o $DAOSSRC/daos_control.yml || exit 4
}

trap "
	echo 'Cleaning up racer'
	racer_cleanup
	exit 0
" INT TERM EXIT

rm -rf /tmp/daos_client.log
racer_setup

# run racer
DURATION=$DURATION NUM_THREADS=$NUM_THREADS MAX_FILES=$MAX_FILES DAOS=$DAOS DMG=$DMGi \
CONT_NAME=$CONT_NAME POOL_NAME=$POOL_NAME $racer $MNTDIR $NUM_RACER_THREADS
