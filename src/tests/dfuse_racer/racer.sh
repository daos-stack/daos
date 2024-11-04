#!/bin/bash
#set -x

DIR="$1"
MAX_FILES=${MAX_FILES:-20}
DURATION=${DURATION:-$((60*5))}

NUM_THREADS=${NUM_THREADS:-$2}
NUM_THREADS=${NUM_THREADS:-3}

RACER_MAX_CLEANUP_WAIT=${RACER_MAX_CLEANUP_WAIT:-$DURATION}

mkdir -p $DIR

if [[ -z "$RACER_PROGS" ]]; then
	RACER_PROGS="file_create dir_create file_rm file_rename file_link"
	RACER_PROGS+=" file_symlink file_list file_concat file_exec file_chown"
	RACER_PROGS+=" file_chmod file_delxattr file_getxattr file_setxattr"
	RACER_PROGS+=" file_rsync file_read_write snap"
fi
RACER_PROGS=${RACER_PROGS//[,+]/ }

racer_cleanup()
{
	echo "racer cleanup"
	for P in $RACER_PROGS; do
		killall -q $P.sh
	done
	trap 0

	local TOT_WAIT=0
	local SHORT_WAIT=5

	local rc
	while [[ $TOT_WAIT -le $RACER_MAX_CLEANUP_WAIT ]]; do
		rc=0
		echo sleeping $SHORT_WAIT sec ...
		sleep $SHORT_WAIT
		# this only checks whether processes exist
		for P in $RACER_PROGS; do
			killall -0 $P.sh
			[[ $? -eq 0 ]] && (( rc+=1 ))
		done

		# Kill dd processes to speedup cleanup
		local pids=$(ps uax | grep "$DIR" | grep "dd " | grep -v grep | awk '{print $2}')
		for pid in $pids; do
			kill $pid
		done

		if [[ $rc -eq 0 ]]; then
			echo there should be NO racer processes:
			ps uww -C "${RACER_PROGS// /.sh,}.sh"
			return 0
		fi
		(( TOT_WAIT+=SHORT_WAIT ))
		echo -n "Waited $TOT_WAIT, rc=$rc "
		(( SHORT_WAIT+=SHORT_WAIT ))
	done
	ps uww -C "${RACER_PROGS// /.sh,}.sh"
	return 1
}

RC=0

echo "Running $0 for $DURATION seconds. CTRL-C to exit"
trap "
	echo 'Cleaning up'
	racer_cleanup
	exit 0
" INT TERM

cd $(dirname $0)
for ((N = 1; N <= $NUM_THREADS; N++)); do
	for P in $RACER_PROGS; do
		DAOS=$DAOS DMG=$DMG CONT_NAME=$CONT_NAME POOL_NAME=$POOL_NAME ./$P.sh $DIR $MAX_FILES &
	done
done

sleep $DURATION
racer_cleanup || RC=$?

# Check our to see whether our test DIR is still available.
df $DIR
(( RC+=$? ))
(( $RC == 0 )) && echo "We survived $0 for $DURATION seconds."
exit $RC
