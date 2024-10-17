#!/bin/bash
trap 'kill $(jobs -p)' EXIT

DIR=$1
MAX=$2

create(){
    echo "asdf" > $DIR/$file/$file/$file
}

while /bin/true ; do
    file=$((RANDOM % MAX))
    mkdir -p $DIR/$file/$file/ 2> /dev/null
    create 2> /dev/null
done
