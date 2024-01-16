#!/bin/bash

set -e
cd /root/daos
first=$1
shift
caller=$1

if [[ "jvm" == "$caller" ]]
then
        shift
        out="$1"
        err="$2"
        shift 2
        props="$*"
        echo "$props"

        java -Xmx2048m -cp ./hadoop-daos-0.0.1-SNAPSHOT-shaded.jar:\
./hadoop-daos-0.0.1-SNAPSHOT-tests.jar "$props"  \
io.daos.fs.hadoop.perf.Main "$first" 1>"$out" 2>"$err"

else
        props="$*"
        echo "$props"

        java -Xmx2048m -cp ./hadoop-daos-0.0.1-SNAPSHOT-shaded.jar:\
./hadoop-daos-0.0.1-SNAPSHOT-tests.jar "$props" \
io.daos.fs.hadoop.perf.Main "$first" 1>stdout 2>stderr

fi
