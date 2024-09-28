#!/bin/bash
trap 'kill $(jobs -p)' EXIT

DIR=$1
MAX=$2

concat(){
    cat $DIR/$file >> $DIR/$new_file
    cat $DIR/$file/$file/$file >> $DIR/$new_file

}

while /bin/true ; do
    file=$((RANDOM % MAX))
    new_file=$((RANDOM % MAX))
    concat 2> /dev/null
done
