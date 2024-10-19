#!/bin/bash
trap 'kill $(jobs -p)' EXIT

DIR=$1
MAX=$2

while /bin/true ; do
    ls -R $DIR/ > /dev/null 2> /dev/null &
    ls -R $DIR/ > /dev/null 2> /dev/null &
    ls -R $DIR/ > /dev/null 2> /dev/null &
    ls -R $DIR/ > /dev/null 2> /dev/null &
    ls -R $DIR/ > /dev/null 2> /dev/null &

    ls -R $DIR/ > /dev/null 2> /dev/null &
    ls -R $DIR/ > /dev/null 2> /dev/null &
    ls -R $DIR/ > /dev/null 2> /dev/null &
    ls -R $DIR/ > /dev/null 2> /dev/null &
    ls -R $DIR/ > /dev/null 2> /dev/null &

    wait
    sleep 1
done
