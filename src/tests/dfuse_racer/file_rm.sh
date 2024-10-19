#!/bin/bash
trap 'kill $(jobs -p)' EXIT

DIR=$1
MAX=$2

while /bin/true ; do
    file=$((RANDOM % MAX))
    rm -rf $DIR/$file 2> /dev/null
    sleep 1
done


