#!/bin/bash

set -e

CLIENT=../samples/client
SERVER=../samples/server

test -n "$1" && CLIENT="$1"
test -n "$2" && SERVER="$2"

#
# ASAN and valgrind, understandably, don't get along.
#
if [ "$WITH_ASAN" = 1 ]; then
    valgrind=""
else
    valgrind="valgrind --quiet --trace-children=yes --error-exitcode=1 --leak-check=full --read-inline-info=yes --read-var-info=yes --track-origins=yes"
fi

sock="/tmp/vfio-user.sock"
rm -f ${sock}*
${valgrind} $SERVER -v ${sock} &
while [ ! -S ${sock} ]; do
	sleep 0.1
done
${valgrind} $CLIENT ${sock} || {
    kill $(jobs -p)
    exit 1
}
wait
