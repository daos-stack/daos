#!/bin/sh

set +e

. utils/sl/setup_local.sh

max_len=`dfuse --help | wc -L`
if [ $max_len -gt 90 ]
then
    exit 1
fi

if [ $max_len -lt 80 ]
then
    exit 1
fi

out=`dfuse -t 20 -e 21 /tmp`
if [ "$out" != "Dfuse needs at least one fuse thread." ]
then
    exit 1
fi

exit 0
